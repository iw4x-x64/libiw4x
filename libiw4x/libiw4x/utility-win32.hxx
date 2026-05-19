#pragma once

// clang-format off

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#    include <winsock2.h>
#    include <windows.h>
#    include <psapi.h>
#    include <ws2tcpip.h>
#    undef NOMINMAX
#  else
#    include <winsock2.h>
#    include <windows.h>
#    include <psapi.h>
#    include <ws2tcpip.h>
#  endif
#  undef WIN32_LEAN_AND_MEAN
#else
#  ifndef NOMINMAX
#    define NOMINMAX
#    include <winsock2.h>
#    include <windows.h>
#    include <psapi.h>
#    include <ws2tcpip.h>
#    undef NOMINMAX
#  else
#    include <winsock2.h>
#    include <windows.h>
#    include <psapi.h>
#    include <ws2tcpip.h>
#  endif
#endif

// clang-format on

// Linker pseudo-variable.
//
extern "C" IMAGE_DOS_HEADER __ImageBase;
