# Portable release helper for Windows/MSYS2 builds.
# Usage:
#   cmake -DPORTABLE_PREFIX=<install-prefix> -DPORTABLE_OUT=<zip-path> \
#         -P cmake/windows/PortableRelease.cmake

if(NOT WIN32)
  message(FATAL_ERROR "PortableRelease.cmake is intended for WIN32 builds.")
endif()

if(NOT DEFINED PORTABLE_PREFIX)
  message(FATAL_ERROR "PORTABLE_PREFIX is required")
endif()
if(NOT DEFINED PORTABLE_OUT)
  message(FATAL_ERROR "PORTABLE_OUT is required")
endif()

file(TO_CMAKE_PATH "${PORTABLE_PREFIX}" PORTABLE_PREFIX)
file(TO_CMAKE_PATH "${PORTABLE_OUT}" PORTABLE_OUT)

set(_stage "${CMAKE_BINARY_DIR}/portable-stage")
file(REMOVE_RECURSE "${_stage}")
file(MAKE_DIRECTORY "${_stage}")

# Copy installed tree first (uxplay.exe, docs, manpages, beacon scripts, etc.).
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PORTABLE_PREFIX}" "${_stage}"
  RESULT_VARIABLE _copy_rv
)
if(NOT _copy_rv EQUAL 0)
  message(FATAL_ERROR "Failed to copy install tree from ${PORTABLE_PREFIX}")
endif()

# Bundle runtime DLLs from the same prefix used for build/install.
set(_runtime_globs
  "${PORTABLE_PREFIX}/bin/*.dll"
  "${PORTABLE_PREFIX}/bin/gst-plugins-1.0/*.dll"
  "${PORTABLE_PREFIX}/lib/gstreamer-1.0/*.dll"
  "${PORTABLE_PREFIX}/lib/gio/modules/*.dll"
  "${PORTABLE_PREFIX}/lib/graphene-1.0/*.dll"
)

foreach(_glob IN LISTS _runtime_globs)
  file(GLOB _matches "${_glob}")
  foreach(_f IN LISTS _matches)
    file(RELATIVE_PATH _rel "${PORTABLE_PREFIX}" "${_f}")
    get_filename_component(_rel_dir "${_rel}" DIRECTORY)
    file(MAKE_DIRECTORY "${_stage}/${_rel_dir}")
    file(COPY "${_f}" DESTINATION "${_stage}/${_rel_dir}")
  endforeach()
endforeach()

# Include the Bonjour runtime DLL if available in SDK location.
if(DEFINED ENV{BONJOUR_SDK_HOME})
  file(TO_CMAKE_PATH "$ENV{BONJOUR_SDK_HOME}" _bonjour_home)
  foreach(_cand
      "${_bonjour_home}/Lib/x64/dnssd.dll"
      "${_bonjour_home}/Lib/Win64/dnssd.dll"
      "${_bonjour_home}/Bin/dnssd.dll")
    if(EXISTS "${_cand}")
      file(COPY "${_cand}" DESTINATION "${_stage}/bin")
      break()
    endif()
  endforeach()
endif()

file(ARCHIVE_CREATE
  OUTPUT "${PORTABLE_OUT}"
  PATHS "${_stage}"
  FORMAT zip
)

message(STATUS "Portable ZIP created: ${PORTABLE_OUT}")
