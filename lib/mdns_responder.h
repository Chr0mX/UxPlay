/*
 * Minimal built-in mDNS / DNS-SD responder (RFC 6762, RFC 6763).
 *
 * Advertise-only: answers queries for the services registered here (PTR, SRV,
 * TXT) and for this host's IPv4 address (A), probes new names and renames on a
 * conflict, announces on registration and sends goodbyes on removal. It is used
 * on Windows when the Apple Bonjour service is not available, so UxPlay can be
 * discovered without installing Bonjour. Written from the RFCs; portable C
 * (Winsock or BSD sockets).
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 */

#ifndef MDNS_RESPONDER_H
#define MDNS_RESPONDER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mdns_service_s mdns_service_t;

/* Registers "<name>.<regtype>.local" (regtype like "_airplay._tcp") on TCP
 * port (host byte order) with a TXT record in DNS wire format. Starts the
 * responder thread on first use. Returns 0 on success, or a negative
 * kDNSServiceErr_* style code (-65539 no memory, -65537 network unavailable). */
int  mdns_register(const char *name, const char *regtype, unsigned short port,
                   const unsigned char *txt, int txt_len, mdns_service_t **out);

/* Sends goodbye packets and removes the service; stops the responder when the
 * last service is gone. */
void mdns_unregister(mdns_service_t *service);

/* Current instance name (may differ from the requested one after a conflict). */
const char *mdns_service_name(const mdns_service_t *service);

#ifdef __cplusplus
}
#endif
#endif
