/*
 * Minimal built-in mDNS / DNS-SD responder (RFC 6762, RFC 6763).
 * See mdns_responder.h. Written from the RFCs.
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 *
 * Design notes
 *  - One UDP socket on 0.0.0.0:5353 (SO_REUSEADDR, so it coexists with Windows'
 *    own mDNS and with Bonjour), joined to 224.0.0.251 on every IPv4 interface.
 *  - One thread receives queries and runs the probe/announce timers; all
 *    shared state and every send is guarded by one mutex.
 *  - A records carry the address of the interface the query arrived on (by
 *    subnet), so a phone always gets an address it can reach.
 *  - Names are written uncompressed (allowed); received names may be compressed.
 *  - Conflict: another responder answering for one of our instance names with a
 *    different SRV record. We then rename to "<name> (2)", "(3)", ... and probe
 *    again, as Bonjour does when no NoAutoRename flag is given.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
# include <iphlpapi.h>
# include <windows.h>
typedef SOCKET mdns_sock_t;
# define MDNS_BAD_SOCK INVALID_SOCKET
# define mdns_close closesocket
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <sys/select.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <net/if.h>
# include <ifaddrs.h>
# include <unistd.h>
# include <time.h>
typedef int mdns_sock_t;
# define MDNS_BAD_SOCK (-1)
# define mdns_close close
#endif

#include "mdns_responder.h"

#define MDNS_PORT        5353
#define MDNS_GROUP       "224.0.0.251"
#define MAX_IFACES       16
#define MAX_LABELS       16
#define MAX_LABEL        63
#define PKT_MAX          4096

#define T_A     1
#define T_PTR   12
#define T_TXT   16
#define T_SRV   33
#define T_ANY   255
#define C_IN    1
#define C_FLUSH 0x8000   /* cache-flush bit on unique records in responses */
#define C_QU    0x8000   /* unicast-response bit in questions */

#define TTL_HOST  120    /* SRV, A (RFC 6762 section 10) */
#define TTL_OTHER 4500   /* PTR, TXT */

enum { ST_PROBING, ST_ANNOUNCING, ST_ESTABLISHED };

struct mdns_service_s {
    char     base[MAX_LABEL + 1];    /* requested instance name */
    char     name[MAX_LABEL + 1];    /* current instance name */
    int      rename_n;
    char     svc[2][MAX_LABEL + 1];  /* "_airplay", "_tcp" */
    uint16_t port;                   /* host byte order */
    unsigned char *txt;
    int      txt_len;
    int      state;
    int      count;                  /* probes or announcements sent */
    double   due;                    /* next probe / announcement */
    struct mdns_service_s *next;
};

typedef struct { uint32_t addr, mask; } iface_t;   /* network byte order */

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_t       g_thread;
static int             g_running = 0;
static volatile int    g_stop = 0;
static mdns_sock_t     g_sock = MDNS_BAD_SOCK;
static iface_t         g_ifs[MAX_IFACES];
static int             g_nifs = 0;
static double          g_next_scan = 0;
static char            g_host[MAX_LABEL + 1] = "UxPlay";
static mdns_service_t *g_list = NULL;

/* ── time, random ─────────────────────────────────────────────────────────── */

static double now_s(void)
{
#ifdef _WIN32
    return (double) GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

static double jitter(double max_s) { return max_s * (double) (rand() % 1000) / 1000.0; }

/* ── names ────────────────────────────────────────────────────────────────── */

typedef struct { int n; char l[MAX_LABELS][MAX_LABEL + 1]; } dname_t;

static int label_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

static int dname_eq(const dname_t *a, const dname_t *b)
{
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) if (!label_eq(a->l[i], b->l[i])) return 0;
    return 1;
}

static void dname_set(dname_t *d, int n, const char *const *labels)
{
    d->n = n;
    for (int i = 0; i < n; i++) {
        size_t len = strlen(labels[i]);
        if (len > MAX_LABEL) len = MAX_LABEL;
        memcpy(d->l[i], labels[i], len);
        d->l[i][len] = 0;
    }
}

static void name_instance(const mdns_service_t *s, dname_t *d)
{
    const char *l[4] = { s->name, s->svc[0], s->svc[1], "local" };
    dname_set(d, 4, l);
}
static void name_type(const mdns_service_t *s, dname_t *d)
{
    const char *l[3] = { s->svc[0], s->svc[1], "local" };
    dname_set(d, 3, l);
}
static void name_host(dname_t *d)
{
    const char *l[2] = { g_host, "local" };
    dname_set(d, 2, l);
}
static void name_meta(dname_t *d)
{
    const char *l[4] = { "_services", "_dns-sd", "_udp", "local" };
    dname_set(d, 4, l);
}

/* Reads a possibly compressed name at *off; advances *off past it. */
static int read_name(const unsigned char *p, int len, int *off, dname_t *d)
{
    int o = *off, jumped = 0, hops = 0;
    d->n = 0;
    for (;;) {
        if (o >= len) return -1;
        unsigned c = p[o];
        if (c == 0) { o++; break; }
        if ((c & 0xC0) == 0xC0) {
            if (o + 1 >= len || ++hops > 16) return -1;
            int ptr = ((c & 0x3F) << 8) | p[o + 1];
            if (!jumped) *off = o + 2;
            jumped = 1;
            o = ptr;
            continue;
        }
        if (c > MAX_LABEL || o + 1 + (int) c > len || d->n >= MAX_LABELS) return -1;
        memcpy(d->l[d->n], p + o + 1, c);
        d->l[d->n][c] = 0;
        d->n++;
        o += 1 + c;
    }
    if (!jumped) *off = o;
    return 0;
}

/* ── packet building ──────────────────────────────────────────────────────── */

typedef struct {
    unsigned char b[PKT_MAX];
    int len, err;
    int qd, an, ns, ar;
} pkt_t;

static void pk_init(pkt_t *p, uint16_t id, uint16_t flags)
{
    memset(p, 0, sizeof(*p));
    p->b[0] = id >> 8;    p->b[1] = id & 0xFF;
    p->b[2] = flags >> 8; p->b[3] = flags & 0xFF;
    p->len = 12;
}

static void pk_bytes(pkt_t *p, const void *d, int n)
{
    if (p->err || p->len + n > PKT_MAX) { p->err = 1; return; }
    memcpy(p->b + p->len, d, n);
    p->len += n;
}
static void pk_u16(pkt_t *p, unsigned v) { unsigned char b[2] = { (unsigned char) (v >> 8), (unsigned char) v }; pk_bytes(p, b, 2); }
static void pk_u32(pkt_t *p, uint32_t v) { pk_u16(p, v >> 16); pk_u16(p, v & 0xFFFF); }

static void pk_name(pkt_t *p, const dname_t *d)
{
    for (int i = 0; i < d->n; i++) {
        unsigned char n = (unsigned char) strlen(d->l[i]);
        pk_bytes(p, &n, 1);
        pk_bytes(p, d->l[i], n);
    }
    unsigned char z = 0;
    pk_bytes(p, &z, 1);
}

/* Writes the record header; returns the offset of RDLENGTH for pk_rr_end. */
static int pk_rr_begin(pkt_t *p, const dname_t *name, int type, int cls, uint32_t ttl)
{
    pk_name(p, name);
    pk_u16(p, type);
    pk_u16(p, cls);
    pk_u32(p, ttl);
    int at = p->len;
    pk_u16(p, 0);
    return at;
}
static void pk_rr_end(pkt_t *p, int at)
{
    if (p->err) return;
    int n = p->len - at - 2;
    p->b[at] = n >> 8; p->b[at + 1] = n & 0xFF;
}

static void pk_finish(pkt_t *p)
{
    p->b[4] = p->qd >> 8;  p->b[5] = p->qd & 0xFF;
    p->b[6] = p->an >> 8;  p->b[7] = p->an & 0xFF;
    p->b[8] = p->ns >> 8;  p->b[9] = p->ns & 0xFF;
    p->b[10] = p->ar >> 8; p->b[11] = p->ar & 0xFF;
}

/* Record writers. flush: set the cache-flush bit (not in legacy unicast). */
static void rr_ptr_type(pkt_t *p, const mdns_service_t *s, uint32_t ttl)
{
    dname_t t, i;
    name_type(s, &t); name_instance(s, &i);
    int at = pk_rr_begin(p, &t, T_PTR, C_IN, ttl);
    pk_name(p, &i);
    pk_rr_end(p, at);
}
static void rr_ptr_meta(pkt_t *p, const mdns_service_t *s, uint32_t ttl)
{
    dname_t m, t;
    name_meta(&m); name_type(s, &t);
    int at = pk_rr_begin(p, &m, T_PTR, C_IN, ttl);
    pk_name(p, &t);
    pk_rr_end(p, at);
}
static void rr_srv(pkt_t *p, const mdns_service_t *s, uint32_t ttl, int flush)
{
    dname_t i, h;
    name_instance(s, &i); name_host(&h);
    int at = pk_rr_begin(p, &i, T_SRV, C_IN | (flush ? C_FLUSH : 0), ttl);
    pk_u16(p, 0); pk_u16(p, 0); pk_u16(p, s->port);
    pk_name(p, &h);
    pk_rr_end(p, at);
}
static void rr_txt(pkt_t *p, const mdns_service_t *s, uint32_t ttl, int flush)
{
    dname_t i;
    name_instance(s, &i);
    int at = pk_rr_begin(p, &i, T_TXT, C_IN | (flush ? C_FLUSH : 0), ttl);
    if (s->txt_len > 0) pk_bytes(p, s->txt, s->txt_len);
    else { unsigned char z = 0; pk_bytes(p, &z, 1); }   /* empty TXT = one empty string */
    pk_rr_end(p, at);
}
static void rr_a(pkt_t *p, uint32_t addr, uint32_t ttl, int flush)
{
    dname_t h;
    name_host(&h);
    int at = pk_rr_begin(p, &h, T_A, C_IN | (flush ? C_FLUSH : 0), ttl);
    pk_bytes(p, &addr, 4);   /* already network order */
    pk_rr_end(p, at);
}

/* ── sending ──────────────────────────────────────────────────────────────── */

static void send_to(const pkt_t *p, uint32_t addr, uint16_t port, const iface_t *via)
{
    if (p->err || g_sock == MDNS_BAD_SOCK) return;
    if (via) {
        struct in_addr ia; ia.s_addr = via->addr;
        setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_IF, (const char *) &ia, sizeof(ia));
    }
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = addr;
    sendto(g_sock, (const char *) p->b, p->len, 0, (const struct sockaddr *) &to, sizeof(to));
}

static uint32_t group_addr(void) { return inet_addr(MDNS_GROUP); }

/* Unsolicited response (announcement or goodbye) for one service on every interface. */
static void send_all_records(const mdns_service_t *s, int goodbye)
{
    for (int i = 0; i < g_nifs; i++) {
        pkt_t p;
        pk_init(&p, 0, 0x8400);   /* response, authoritative */
        uint32_t t1 = goodbye ? 0 : TTL_OTHER, t2 = goodbye ? 0 : TTL_HOST;
        rr_ptr_type(&p, s, t1); p.an++;
        rr_srv(&p, s, t2, 1);   p.an++;
        rr_txt(&p, s, t1, 1);   p.an++;
        if (!goodbye) { rr_ptr_meta(&p, s, t1); p.an++; rr_a(&p, g_ifs[i].addr, t2, 1); p.an++; }
        pk_finish(&p);
        send_to(&p, group_addr(), MDNS_PORT, &g_ifs[i]);
    }
}

static void send_probe(const mdns_service_t *s)
{
    for (int i = 0; i < g_nifs; i++) {
        pkt_t p;
        dname_t n;
        name_instance(s, &n);
        pk_init(&p, 0, 0);
        pk_name(&p, &n); pk_u16(&p, T_ANY); pk_u16(&p, C_IN | C_QU); p.qd++;
        rr_srv(&p, s, TTL_HOST, 0); p.ns++;    /* proposed record in the authority section */
        pk_finish(&p);
        send_to(&p, group_addr(), MDNS_PORT, &g_ifs[i]);
    }
}

/* ── interfaces ───────────────────────────────────────────────────────────── */

static int scan_ifaces(iface_t *out)
{
    int n = 0;
#ifdef _WIN32
    ULONG size = 16 * 1024;
    IP_ADAPTER_ADDRESSES *aa = NULL;
    for (int tries = 0; tries < 3; tries++) {
        aa = (IP_ADAPTER_ADDRESSES *) malloc(size);
        if (!aa) return 0;
        ULONG r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                       GAA_FLAG_SKIP_DNS_SERVER, NULL, aa, &size);
        if (r == NO_ERROR) break;
        free(aa); aa = NULL;
        if (r != ERROR_BUFFER_OVERFLOW) return 0;
    }
    for (IP_ADAPTER_ADDRESSES *a = aa; a && n < MAX_IFACES; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (a->Flags & IP_ADAPTER_NO_MULTICAST) continue;
        for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u && n < MAX_IFACES; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            struct sockaddr_in *sin = (struct sockaddr_in *) u->Address.lpSockaddr;
            int pl = u->OnLinkPrefixLength;
            out[n].addr = sin->sin_addr.s_addr;
            out[n].mask = pl <= 0 ? 0 : htonl(pl >= 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> pl));
            n++;
        }
    }
    free(aa);
#else
    struct ifaddrs *ifa = NULL;
    if (getifaddrs(&ifa) != 0) return 0;
    for (struct ifaddrs *i = ifa; i && n < MAX_IFACES; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
        out[n].addr = ((struct sockaddr_in *) i->ifa_addr)->sin_addr.s_addr;
        out[n].mask = i->ifa_netmask ? ((struct sockaddr_in *) i->ifa_netmask)->sin_addr.s_addr : 0;
        n++;
    }
    freeifaddrs(ifa);
#endif
    return n;
}

static void membership(uint32_t ifaddr, int add)
{
    struct ip_mreq m;
    m.imr_multiaddr.s_addr = group_addr();
    m.imr_interface.s_addr = ifaddr;
    setsockopt(g_sock, IPPROTO_IP, add ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP, (const char *) &m, sizeof(m));
}

/* Rescans interfaces; on a change updates memberships and re-announces. */
static void update_ifaces(void)
{
    iface_t now[MAX_IFACES];
    int n = scan_ifaces(now);
    int same = n == g_nifs;
    for (int i = 0; same && i < n; i++)
        same = now[i].addr == g_ifs[i].addr && now[i].mask == g_ifs[i].mask;
    if (same) return;
    for (int i = 0; i < g_nifs; i++) membership(g_ifs[i].addr, 0);
    memcpy(g_ifs, now, sizeof(now));
    g_nifs = n;
    for (int i = 0; i < g_nifs; i++) membership(g_ifs[i].addr, 1);
    for (mdns_service_t *s = g_list; s; s = s->next)
        if (s->state != ST_PROBING) { s->state = ST_ANNOUNCING; s->count = 0; s->due = now_s(); }
}

static const iface_t *iface_for(uint32_t src)
{
    for (int i = 0; i < g_nifs; i++)
        if (g_ifs[i].mask && (src & g_ifs[i].mask) == (g_ifs[i].addr & g_ifs[i].mask)) return &g_ifs[i];
    return NULL;
}

/* ── receiving ────────────────────────────────────────────────────────────── */

static void rename_service(mdns_service_t *s)
{
    char suffix[16];
    s->rename_n = s->rename_n < 2 ? 2 : s->rename_n + 1;
    snprintf(suffix, sizeof(suffix), " (%d)", s->rename_n);
    size_t keep = strlen(s->base);
    if (keep + strlen(suffix) > MAX_LABEL) {
        keep = MAX_LABEL - strlen(suffix);
        while (keep > 0 && ((unsigned char) s->base[keep] & 0xC0) == 0x80) keep--;   /* whole UTF-8 chars */
    }
    memcpy(s->name, s->base, keep);
    strcpy(s->name + keep, suffix);
    s->state = ST_PROBING;
    s->count = 0;
    s->due = now_s() + jitter(0.25);
}

/* Someone answered for one of our instance names: a conflict unless the SRV
 * record is identical to ours (e.g. our own looped-back packet). */
static void check_conflicts(const unsigned char *b, int len, int off, int count)
{
    for (int r = 0; r < count; r++) {
        dname_t n;
        if (read_name(b, len, &off, &n) < 0 || off + 10 > len) return;
        int type = (b[off] << 8) | b[off + 1];
        int rdlen = (b[off + 8] << 8) | b[off + 9];
        int rd = off + 10;
        off = rd + rdlen;
        if (off > len) return;
        if (type != T_SRV || rdlen < 7) continue;
        for (mdns_service_t *s = g_list; s; s = s->next) {
            dname_t mine;
            name_instance(s, &mine);
            if (!dname_eq(&n, &mine)) continue;
            int port = (b[rd + 4] << 8) | b[rd + 5];
            dname_t target, host;
            int t = rd + 6;
            name_host(&host);
            int same = port == s->port && read_name(b, len, &t, &target) == 0 && dname_eq(&target, &host);
            if (!same) rename_service(s);
        }
    }
}

static void handle_packet(const unsigned char *b, int len, uint32_t src, uint16_t sport)
{
    if (len < 12) return;
    uint16_t id = (b[0] << 8) | b[1];
    uint16_t flags = (b[2] << 8) | b[3];
    int qd = (b[4] << 8) | b[5], an = (b[6] << 8) | b[7], ns = (b[8] << 8) | b[9], ar = (b[10] << 8) | b[11];
    int off = 12;

    if (flags & 0x8000) {   /* response: look for conflicts */
        for (int i = 0; i < qd; i++) {
            dname_t n;
            if (read_name(b, len, &off, &n) < 0) return;
            off += 4;
        }
        check_conflicts(b, len, off, an + ns + ar);
        return;
    }
    if ((flags & 0x7800) != 0) return;   /* only standard queries */

    int legacy = sport != MDNS_PORT;     /* one-shot resolver: unicast reply, echo questions */
    int unicast = legacy;
    const iface_t *via = iface_for(src);

    /* What to send, per service. */
    enum { W_PTR = 1, W_SRV = 2, W_TXT = 4, W_META = 8, A_SRV = 16, A_TXT = 32 };
    int want[8] = { 0 };
    int want_a = 0, answers_a = 0;
    dname_t meta, host;
    name_meta(&meta); name_host(&host);

    for (int q = 0; q < qd; q++) {
        dname_t n;
        if (read_name(b, len, &off, &n) < 0 || off + 4 > len) return;
        int qtype = (b[off] << 8) | b[off + 1];
        int qclass = (b[off + 2] << 8) | b[off + 3];
        off += 4;
        if ((qclass & 0x7FFF) != C_IN && (qclass & 0x7FFF) != T_ANY) continue;
        if (qclass & C_QU) unicast = 1;
        int i = 0;
        for (mdns_service_t *s = g_list; s && i < 8; s = s->next, i++) {
            if (s->state == ST_PROBING) continue;
            dname_t t, in;
            name_type(s, &t); name_instance(s, &in);
            if (dname_eq(&n, &meta) && (qtype == T_PTR || qtype == T_ANY)) want[i] |= W_META;
            if (dname_eq(&n, &t) && (qtype == T_PTR || qtype == T_ANY)) { want[i] |= W_PTR | A_SRV | A_TXT; want_a = 1; }
            if (dname_eq(&n, &in)) {
                if (qtype == T_SRV || qtype == T_ANY) { want[i] |= W_SRV; want_a = 1; }
                if (qtype == T_TXT || qtype == T_ANY) want[i] |= W_TXT;
            }
        }
        if (dname_eq(&n, &host) && (qtype == T_A || qtype == T_ANY)) answers_a = 1;
    }
    int any = answers_a;
    for (int i = 0; i < 8; i++) any |= want[i] & (W_PTR | W_SRV | W_TXT | W_META);
    if (!any) return;

    int nif = via ? 1 : g_nifs;
    for (int k = 0; k < nif; k++) {
        const iface_t *ifc = via ? via : &g_ifs[k];
        pkt_t p;
        int fl = !legacy;
        uint32_t cap = legacy ? 10 : 0xFFFFFFFF;   /* legacy unicast: TTL at most 10 s */
        #define TTL(x) ((x) < cap ? (x) : cap)
        pk_init(&p, legacy ? id : 0, 0x8400);
        if (legacy) { pk_bytes(&p, b + 12, off - 12); p.qd = qd; }   /* same offsets: pointers stay valid */
        int i = 0;
        for (mdns_service_t *s = g_list; s && i < 8; s = s->next, i++) {
            if (want[i] & W_META) { rr_ptr_meta(&p, s, TTL(TTL_OTHER)); p.an++; }
            if (want[i] & W_PTR)  { rr_ptr_type(&p, s, TTL(TTL_OTHER)); p.an++; }
            if (want[i] & W_SRV)  { rr_srv(&p, s, TTL(TTL_HOST), fl); p.an++; }
            if (want[i] & W_TXT)  { rr_txt(&p, s, TTL(TTL_OTHER), fl); p.an++; }
        }
        if (answers_a) { rr_a(&p, ifc->addr, TTL(TTL_HOST), fl); p.an++; }
        i = 0;
        for (mdns_service_t *s = g_list; s && i < 8; s = s->next, i++) {
            if ((want[i] & A_SRV) && !(want[i] & W_SRV)) { rr_srv(&p, s, TTL(TTL_HOST), fl); p.ar++; }
            if ((want[i] & A_TXT) && !(want[i] & W_TXT)) { rr_txt(&p, s, TTL(TTL_OTHER), fl); p.ar++; }
        }
        if (want_a && !answers_a) { rr_a(&p, ifc->addr, TTL(TTL_HOST), fl); p.ar++; }
        #undef TTL
        pk_finish(&p);
        if (unicast) send_to(&p, src, sport, NULL);
        else send_to(&p, group_addr(), MDNS_PORT, ifc);
        if (unicast) break;   /* one reply is enough */
    }
}

/* ── timers and thread ────────────────────────────────────────────────────── */

static void run_timers(double t)
{
    for (mdns_service_t *s = g_list; s; s = s->next) {
        if (s->state == ST_ESTABLISHED || t < s->due) continue;
        if (s->state == ST_PROBING) {
            if (s->count < 3) { send_probe(s); s->count++; s->due = t + 0.25; }
            else { s->state = ST_ANNOUNCING; s->count = 0; s->due = t; }
        }
        if (s->state == ST_ANNOUNCING && t >= s->due) {
            send_all_records(s, 0);
            s->due = t + (s->count == 0 ? 1.0 : 2.0);
            if (++s->count >= 3) s->state = ST_ESTABLISHED;
        }
    }
}

static void *responder_thread(void *arg)
{
    (void) arg;
    unsigned char buf[PKT_MAX];
    while (!g_stop) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(g_sock, &rd);
        struct timeval tv = { 0, 100000 };   /* 100 ms: timers and stop requests */
        int r = select((int) g_sock + 1, &rd, NULL, NULL, &tv);
        if (r > 0 && FD_ISSET(g_sock, &rd)) {
            struct sockaddr_in from;
            socklen_t fl = sizeof(from);
            int n = recvfrom(g_sock, (char *) buf, sizeof(buf), 0, (struct sockaddr *) &from, &fl);
            if (n > 0) {
                pthread_mutex_lock(&g_mtx);
                handle_packet(buf, n, from.sin_addr.s_addr, ntohs(from.sin_port));
                pthread_mutex_unlock(&g_mtx);
            }
        }
        pthread_mutex_lock(&g_mtx);
        double t = now_s();
        if (t >= g_next_scan) { update_ifaces(); g_next_scan = t + 5.0; }
        run_timers(t);
        pthread_mutex_unlock(&g_mtx);
    }
    return NULL;
}

static void set_host_name(void)
{
    char h[256] = "";
    if (gethostname(h, sizeof(h)) != 0 || !h[0]) strcpy(h, "UxPlay");
    char *dot = strchr(h, '.');
    if (dot) *dot = 0;
    int j = 0;
    for (int i = 0; h[i] && j < MAX_LABEL; i++) {
        char c = h[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') g_host[j++] = c;
    }
    g_host[j] = 0;
    if (!j) strcpy(g_host, "UxPlay");
}

/* Called with g_mtx held. */
static int start_responder(void)
{
    if (g_running) return 0;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    srand((unsigned) (now_s() * 1000.0));
    set_host_name();
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == MDNS_BAD_SOCK) return -1;
    int one = 1;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof(one));
#ifdef SO_REUSEPORT
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEPORT, (const char *) &one, sizeof(one));
#endif
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(MDNS_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_sock, (struct sockaddr *) &a, sizeof(a)) != 0) {
        mdns_close(g_sock);
        g_sock = MDNS_BAD_SOCK;
        return -1;
    }
    unsigned char ttl = 255, loop = 1;
    int ttl_i = 255, loop_i = 1;
#ifdef _WIN32
    setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char *) &ttl_i, sizeof(ttl_i));
    setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *) &loop_i, sizeof(loop_i));
    (void) ttl; (void) loop;
#else
    setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    (void) ttl_i; (void) loop_i;
#endif
    g_nifs = 0;
    update_ifaces();
    g_next_scan = now_s() + 5.0;
    g_stop = 0;
    if (pthread_create(&g_thread, NULL, responder_thread, NULL) != 0) {
        mdns_close(g_sock);
        g_sock = MDNS_BAD_SOCK;
        return -1;
    }
    g_running = 1;
    return 0;
}

/* ── public API ───────────────────────────────────────────────────────────── */

int mdns_register(const char *name, const char *regtype, unsigned short port,
                  const unsigned char *txt, int txt_len, mdns_service_t **out)
{
    if (out) *out = NULL;
    const char *dot = regtype ? strchr(regtype, '.') : NULL;
    if (!name || !dot || txt_len < 0) return -65540;   /* kDNSServiceErr_BadParam */

    mdns_service_t *s = (mdns_service_t *) calloc(1, sizeof(*s));
    if (!s) return -65539;   /* kDNSServiceErr_NoMemory */
    size_t n = strlen(name);
    if (n > MAX_LABEL) {
        n = MAX_LABEL;
        while (n > 0 && ((unsigned char) name[n] & 0xC0) == 0x80) n--;
    }
    memcpy(s->base, name, n);
    memcpy(s->name, name, n);
    size_t l0 = (size_t) (dot - regtype);
    if (l0 > MAX_LABEL) l0 = MAX_LABEL;
    memcpy(s->svc[0], regtype, l0);
    const char *t1 = dot + 1, *t2 = strchr(t1, '.');
    size_t l1 = t2 ? (size_t) (t2 - t1) : strlen(t1);
    if (l1 > MAX_LABEL) l1 = MAX_LABEL;
    memcpy(s->svc[1], t1, l1);
    s->port = port;
    if (txt_len > 0) {
        s->txt = (unsigned char *) malloc(txt_len);
        if (!s->txt) { free(s); return -65539; }
        memcpy(s->txt, txt, txt_len);
        s->txt_len = txt_len;
    }
    s->state = ST_PROBING;

    pthread_mutex_lock(&g_mtx);
    if (start_responder() != 0) {
        pthread_mutex_unlock(&g_mtx);
        free(s->txt);
        free(s);
        return -65537;   /* kDNSServiceErr_Unknown: port 5353 unusable */
    }
    s->due = now_s() + jitter(0.25);
    s->next = g_list;
    g_list = s;
    pthread_mutex_unlock(&g_mtx);
    if (out) *out = s;
    return 0;
}

void mdns_unregister(mdns_service_t *service)
{
    if (!service) return;
    int stop = 0;
    pthread_mutex_lock(&g_mtx);
    mdns_service_t **pp = &g_list;
    while (*pp && *pp != service) pp = &(*pp)->next;
    if (*pp) {
        *pp = service->next;
        if (service->state != ST_PROBING) send_all_records(service, 1);   /* goodbye */
    }
    stop = g_running && !g_list;
    if (stop) g_stop = 1;
    pthread_mutex_unlock(&g_mtx);
    free(service->txt);
    free(service);
    if (stop) {
        pthread_join(g_thread, NULL);
        pthread_mutex_lock(&g_mtx);
        if (g_sock != MDNS_BAD_SOCK) {
            for (int i = 0; i < g_nifs; i++) membership(g_ifs[i].addr, 0);
            mdns_close(g_sock);
        }
        g_sock = MDNS_BAD_SOCK;
        g_nifs = 0;
        g_running = 0;
        pthread_mutex_unlock(&g_mtx);
    }
}

const char *mdns_service_name(const mdns_service_t *service)
{
    return service ? service->name : "";
}
