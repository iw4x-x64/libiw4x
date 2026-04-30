#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#    include <windows.h>
#    undef NOMINMAX
#  else
#    include <windows.h>
#  endif
#  undef WIN32_LEAN_AND_MEAN
#else
#  ifndef NOMINMAX
#    define NOMINMAX
#    include <windows.h>
#    undef NOMINMAX
#  else
#    include <windows.h>
#  endif
#endif

#include <libiw4x/export.hxx>

namespace iw4x
{
  extern "C"
  {
    // Note that MinGW and MSVC differ in how they treat exports for DLLs.
    //
    // Specifically, MinGW (via GNU ld) allows us to produce a DLL with no
    // explicitly exported symbols. DllMain is treated as a special entry point
    // discovered by name and doesn't need to appear in the export table.
    //
    // MSVC's linker, however, requires at least one symbol to be placed in the
    // DLL export table. If we don't explicitly mark any symbol for export (for
    // example, via __declspec(dllexport)), MSVC will refuse to link the DLL and
    // complain that no exported symbols exist.
    //
    LIBIW4X_SYMEXPORT BOOL WINAPI
    DllMain (HINSTANCE, DWORD reason, LPVOID);
  }
}
