#include <libiw4x/mod/window.hxx>

#include <cstdint>

#include <windows.h>

#include <tracy/Tracy.hpp>

#include <libiw4x/detour.hxx>
#include <libiw4x/logger.hxx>

using namespace std;

namespace iw4x
{
  namespace mod
  {
    namespace
    {
      using CreateWindowExA_t = HWND (WINAPI*) (DWORD     ex_style,
                                                LPCSTR    class_name,
                                                LPCSTR    window_name,
                                                DWORD     style,
                                                int       x,
                                                int       y,
                                                int       width,
                                                int       height,
                                                HWND      parent_window,
                                                HMENU     menu,
                                                HINSTANCE instance,
                                                LPVOID    param);
      CreateWindowExA_t CreateWindowExA;

      HWND WINAPI
      create_window (DWORD     ex_style,
                     LPCSTR    class_name,
                     LPCSTR    window_name,
                     DWORD     style,
                     int       x,
                     int       y,
                     int       width,
                     int       height,
                     HWND      parent_window,
                     HMENU     menu,
                     HINSTANCE instance,
                     LPVOID    param)
      {
        if (class_name != nullptr && !IS_INTRESOURCE (class_name))
        {
          using namespace literals;

          if (string_view (class_name) == "IW4"sv)
            window_name = "Modern Warfare 2 (IW4x)";
        }

        return CreateWindowExA (ex_style,
                                class_name,
                                window_name,
                                style,
                                x,
                                y,
                                width,
                                height,
                                parent_window,
                                menu,
                                instance,
                                param);
      }
    }

    window_module::
    window_module ()
    {
      ZoneScoped;

      CreateWindowExA = reinterpret_cast<CreateWindowExA_t> (
        GetProcAddress (GetModuleHandleA ("user32.dll"), "CreateWindowExA"));

      {
        DWORD discard (0);
        VirtualProtect (reinterpret_cast<void*> (CreateWindowExA),
                        64, PAGE_EXECUTE_READWRITE, &discard);
      }

      detour (CreateWindowExA, &create_window);
    }
  }
}
