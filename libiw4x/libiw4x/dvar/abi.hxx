#pragma once

#include <cstddef>
#include <cstdint>

#include <libiw4x/dvar/types.hxx>
#include <libiw4x/dvar/utility.hxx>

#include <libiw4x/import.hxx>

namespace iw4x::dvar
{
  // Index into the TLS slot array.
  //
  // We use this to retrieve our engine-specific thread-local storage.
  //
  inline constexpr uintptr_t k_tls_index (0x148CA3A28);

  // Offset for the accumulated modified flags.
  //
  // The idea here is that the engine uses this field to track which dvar
  // flags have been modified during the current execution phase.
  //
  inline constexpr ptrdiff_t k_tls_modified_off (0x20);

  // Offset for the allowed flags mask.
  //
  // This mask restricts which flags we are actually permitted to track or
  // modify within the current thread context.
  //
  inline constexpr ptrdiff_t k_tls_allowed_off (0x24);

  // Global subsystem state flags.
  //
  inline constexpr uintptr_t k_inited_flag   (0x14673D271);
  inline constexpr uintptr_t k_autoexec_flag (0x14673D272);
  inline constexpr uintptr_t k_cheats_flag   (0x14046CFB0);

  // Function pointers for engine notifications.
  //
  // We need to call these to alert the server subsystem when certain dvars
  // change, or right before a variant's value is explicitly updated.
  //
  inline constexpr uintptr_t k_sv_notify       (0x140240EB0); // void (*)()
  inline constexpr uintptr_t k_pre_set_variant (0x1401FB230); // void (*)()

  // Console command slot addresses.
  //
  // The engine normally reserves these on its own, but we'll want to bypass the
  // usual registration machinations.
  //
  inline constexpr uintptr_t k_cmd_toggle      (0x141C98ED0);
  inline constexpr uintptr_t k_cmd_togglep     (0x141C98EE8);
  inline constexpr uintptr_t k_cmd_set         (0x141C98F00);
  inline constexpr uintptr_t k_cmd_seta        (0x141C98F18);
  inline constexpr uintptr_t k_cmd_reset       (0x141C98F30);
  inline constexpr uintptr_t k_cmd_setfromdvar (0x141C98F48);

  // Get the engine per-thread TLS block.
  //
  // Notice that this returns a null pointer if the TLS subsystem has not
  // been set up yet. This makes it perfectly safe to call from early
  // initialization routines. The callers, of course, must null-check the
  // result.
  //
  // The GS:0x58 read is quite optimal. It compiles down to a single MOV
  // instruction on both MSVC (via the __readgsqword intrinsic) and
  // GCC/Clang (using inline assembly).
  //
  DVAR_ALWAYS_INLINE void*
  engine_tls () noexcept
  {
    void** slots (nullptr);

#if defined(_MSC_VER)
    slots = reinterpret_cast<void**> (__readgsqword (0x58));
#elif defined(__GNUC__)
    asm volatile ("movq %%gs:0x58, %0" : "=r" (slots));
#endif

    if (slots == nullptr)
      return nullptr;

    int idx (*reinterpret_cast<const int*> (k_tls_index));
    return slots[idx];
  }

  // Record flag categories used at registration time.
  //
  // Notice that this is silently skipped if the TLS is not yet ready. Also
  // keep in mind that it must be called before every register_variant()
  // so that later subsystems inspecting the modified-flags mask see the
  // correct state.
  //
  void
  mark_registration_flags (DvarFlags flags) noexcept;

  // Notify that a dvar's value has changed.
  //
  // The idea here is to update the TLS modified_flags slot, masked by the
  // allowed_flags. Additionally, if we are on the main thread and the flags
  // include USERINFO or SYSTEMINFO, this will notify the server subsystem.
  //
  void
  mark_modified_flags (dvar_t* d) noexcept;

  // RAII lock guards.
  //
  // The engine relies on a custom reader-writer spinlock (internally known
  // as fast_critical_section) for all dvar hash-table accesses. We wrap
  // it in RAII guards to guarantee that every lock acquisition has a
  // corresponding release.
  //
  // Note that neither of these supports recursive acquisition, so be
  // careful not to nest locks of the same type.
  //

  // Read lock guard.
  //
  // This lock allows concurrent readers. It simply spins (using a PAUSE
  // hint) until no writer is actively holding the section.
  //
  struct read_lock final
  {
    read_lock () noexcept;
    ~read_lock ();

    read_lock (const read_lock&) = delete;
    read_lock& operator= (const read_lock&) = delete;
  };

  // Write lock guard.
  //
  // This completely excludes all readers and writers. Furthermore, it
  // temporarily raises the thread priority to normal in order to reduce
  // any potential priority-inversion stalls.
  //
  struct write_lock final
  {
    write_lock () noexcept;
    ~write_lock ();

    write_lock (const write_lock&) = delete;
    write_lock& operator= (const write_lock&) = delete;

  private:
    HANDLE thread_ {};
    int old_priority_ {};
  };
}
