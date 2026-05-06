#include <chrono>
#include <thread>

#undef NDEBUG
#include <cassert>

#include <libiw4x/scheduler.hxx>

using namespace std;
using namespace iw4x;

int
main ()
{
  // Basics (Immediate Execution).
  //
  // The idea here is to verify the fast path. We post a task from the
  // owning thread and tick the scheduler. The task should execute exactly
  // once and then be discarded.
  //
  {
    logical_scheduler s;
    int c (0);

    s.post ([&c] () { c++; });

    // Note that nothing executes before the first tick.
    //
    assert (c == 0);

    s.tick ();
    assert (c == 1);

    // Ensure the task was properly discarded from the active queue.
    //
    s.tick ();
    assert (c == 1);
  }

  // Asynchronous Ingress.
  //
  // Here we test the lock-free MPSC queue. We spawn a foreign thread
  // to push a task, then drain and execute it on the main thread's tick.
  //
  {
    logical_scheduler s;
    int c (0);

    // Scope the thread so that it automatically joins before we proceed
    // to tick the scheduler. This guarantees the task is actually queued
    // and eliminates the need for manual join management.
    //
    {
      std::jthread t (
        [&s, &c] ()
        {
          // We explicitly use the asynchronous tag to force the allocation
          // of an async_node and atomic exchange.
          //
          s.post ([&c] () { c++; }, asynchronous);
        });
    }

    // The first tick claims ownership of the scheduler and drains the
    // ingress queue.
    //
    assert (c == 0);
    s.tick ();
    assert (c == 1);
  }

  // Persistent Modes (Repeat Every Tick).
  //
  // This verifies the retention policy mechanism. The task should survive
  // the active queue clearing and re-enqueue itself into the pending list.
  //
  {
    logical_scheduler s;
    int c (0);

    s.post ([&c] () { c++; }, repeat_every_tick);

    s.tick ();
    assert (c == 1);

    s.tick ();
    assert (c == 2);

    s.tick ();
    assert (c == 3);
  }

  // Conditional Retention (Repeat Until Predicate).
  //
  // We want to make sure the predicate is evaluated correctly after
  // execution. Note that the task will execute one last time on the tick
  // where the condition transitions to true.
  //
  {
    logical_scheduler s;
    int c (0);
    bool stop (false);

    s.post ([&c] () { c++; },
            repeat_until_predicate ([&stop] () { return stop; }));

    s.tick ();
    assert (c == 1);

    s.tick ();
    assert (c == 2);

    // Signal the retention policy to drop the entry.
    //
    stop = true;

    // It executes here, then the predicate evaluates to true, causing it
    // to fall out of the queue.
    //
    s.tick ();
    assert (c == 3);

    // It is now gone.
    //
    s.tick ();
    assert (c == 3);
  }

  // Deferred Execution (Execute After Duration).
  //
  // This tests the readiness gate. The task must remain in the pending
  // queue without executing until the wall-clock duration has elapsed.
  //
  {
    logical_scheduler s;
    int c (0);

    // We use a reasonably small duration so the test does not stall, but
    // large enough to avoid spurious test failures on loaded machines.
    //
    s.post ([&c] () { c++; },
            execute_after_duration (std::chrono::milliseconds (50)));

    s.tick ();

    // The activation time has not arrived yet.
    //
    assert (c == 0);

    std::this_thread::sleep_for (std::chrono::milliseconds (60));

    // The gate should now be open.
    //
    s.tick ();
    assert (c == 1);

    // And since this is a one-shot task, it should not persist.
    //
    s.tick ();
    assert (c == 1);
  }

  // Domain Registry.
  //
  // Finally, we test the global static mapping of domain tags to scheduler
  // instances.
  //
  {
    int c (0);

    scheduler::post (com_frame_domain, [&c] () { c++; });

    // We resolve the same underlying scheduler instance implicitly via
    // the tag type and tick it.
    //
    scheduler::get<com_frame_domain_t> ().tick ();

    assert (c == 1);
  }
}
