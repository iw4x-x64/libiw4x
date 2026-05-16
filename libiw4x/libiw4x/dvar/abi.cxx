#include <libiw4x/dvar/abi.hxx>

#include <libiw4x/import.hxx>

namespace iw4x::dvar
{
  void
  mark_registration_flags (DvarFlags flags) noexcept
  {
    // First, we need to grab the thread-local storage pointer from the engine.
    // If we do not have a valid TLS block, there is nothing we can do, so we
    // just bail out early.
    //
    void* tls (engine_tls ());

    if (tls == nullptr)
      return;

    // Now, we calculate the offset into the TLS block to find the modified
    // flags integer. We cast the pointer appropriately and bitwise-OR our new
    // flags into it to ensure they are registered.
    //
    int& modified (*reinterpret_cast<int*> (
      static_cast<unsigned char*> (tls) + k_tls_modified_off));

    modified |= static_cast<int> (flags);
  }

  void
  mark_modified_flags (dvar_t* d) noexcept
  {
    // Make sure we actually have a valid dvar instance before proceeding.
    //
    if (d == nullptr)
      return;

    // Grab the engine's thread-local storage. Again, if it is not initialized,
    // we cannot safely proceed with the flag modification.
    //
    void* tls (engine_tls ());

    if (tls == nullptr)
      return;

    // Cache the flags and the base pointers. This makes the subsequent offset
    // math a bit easier to follow and keeps things neatly aligned.
    //
    int   flags    (static_cast<int>              (d->flags));
    auto  base     (static_cast<unsigned char*>   (tls));
    auto& modified (*reinterpret_cast<int*>       (base + k_tls_modified_off));
    auto  allowed  (*reinterpret_cast<const int*> (base + k_tls_allowed_off));

    // If all the flags are already in the allowed set, then we are on the fast
    // path. There is no need to notify the server in this case, so we just
    // update the modified mask and return immediately.
    //
    if ((flags & ~allowed) == 0)
    {
      modified |= flags;
      return;
    }

    // It looks like we have some flags that fall outside the allowed set. If we
    // are executing on the main thread, we have to prepare the server subsystem
    // for potential changes. Notice that if we are off-thread, it is simply not
    // safe to notify the server, so we skip it.
    //
    if (!Sys_IsMainThread ())
      return;

    // We specifically ignore DVAR_USERINFO and DVAR_SYSTEMINFO flags here,
    // leaving their notification or handling to other parts of the engine.
    //
    if ((flags & (DVAR_USERINFO | DVAR_SYSTEMINFO)) != 0)
      return;

    // Perform the actual server notification by calling the engine's internal
    // notify function.
    //
    reinterpret_cast<void (*) ()> (k_sv_notify) ();

    // Finally, update the server's tracking of modified dvar flags, provided
    // the underlying pointer is valid.
    //
    if (*sv_dvar_modifiedFlags != nullptr)
      **sv_dvar_modifiedFlags |= flags;
  }

  read_lock::
  read_lock () noexcept
  {
    // We begin by registering our intent to read. This is done by incrementing
    // the read count so writers know we are holding the lock.
    //
    InterlockedIncrement (&g_dvarCritSect->readCount);

    // Now we need to spin here until no writer is holding the critical section.
    // Notice that we use the PAUSE hint instead of a plain busy-wait. This
    // helps avoids memory-order pipeline stalls.
    //
    while (g_dvarCritSect->writeCount != 0)
      _mm_pause ();
  }

  read_lock::
  ~read_lock ()
  {
    // We are done reading, so we decrement the read count. This signals any
    // waiting writers that we have safely released our hold on the lock.
    //
    InterlockedDecrement (&g_dvarCritSect->readCount);
  }

  write_lock::
  write_lock () noexcept
    : thread_       (GetCurrentThread ()),
      old_priority_ (GetThreadPriority (GetCurrentThread ()))
  {
    // Let's temporarily elevate our thread to THREAD_PRIORITY_NORMAL while we
    // hold this lock. The idea is to reduce priority-inversion stalls. We only
    // bump the priority if we are currently below normal, because we absolutely
    // do not want to accidentally lower the priority of a real-time thread.
    //
    if (old_priority_ < THREAD_PRIORITY_NORMAL)
      SetThreadPriority (thread_, THREAD_PRIORITY_NORMAL);

    // Time to acquire the lock. We first check if the write count is zero, and
    // if it is, we try to increment it. Then we verify that we are the sole
    // writer and there are absolutely no readers left.
    //
    for (;;)
    {
      if (g_dvarCritSect->writeCount == 0)
      {
        long prev (InterlockedExchangeAdd (&g_dvarCritSect->writeCount, 1));

        if (prev == 0 && g_dvarCritSect->readCount == 0)
          break;

        // It seems somebody else snuck in and grabbed the lock before we could,
        // or some readers are still active. We need to back off, decrement our
        // write count, and retry the whole process.
        //
        InterlockedDecrement (&g_dvarCritSect->writeCount);
      }

      // Do a brief backoff. We use Sleep(1) here because it nicely matches the
      // engine's own implementation and it avoids hammering the cache line when
      // the lock is heavily contested.
      //
      Sleep (1);
    }
  }

  write_lock::
  ~write_lock ()
  {
    // We are finished with the critical section, so we decrement the write
    // count to let others acquire the lock.
    //
    InterlockedDecrement (&g_dvarCritSect->writeCount);

    // Finally, if we bumped our thread priority earlier, we need to make sure
    // we restore it to its original state so we do not leave the thread running
    // too high.
    //
    if (old_priority_ < THREAD_PRIORITY_NORMAL)
      SetThreadPriority (thread_, old_priority_);
  }
}
