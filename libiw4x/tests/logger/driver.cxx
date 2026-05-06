
#include <chrono>
#include <string>
#include <thread>

#undef NDEBUG
#include <cassert>

#include <libiw4x/logger.hxx>

int
main ()
{
  using namespace iw4x;
  using namespace std;

  logger = new class logger;

  // Rate limiter behavior.
  //
  // We set up a limiter with a tight 10ms threshold to keep the test
  // fast but easily verifiable.
  //
  log::rate_limiter limiter (10ms);

  // The first invocation must succeed since the last_time_ atomic
  // initializes to time_point::min().
  //
  assert (limiter ());

  // Banging the limiter immediately afterwards must fail since the 10ms
  // threshold has definitely not elapsed yet.
  //
  assert (!limiter ());
  assert (!limiter ());

  // Now we wait slightly longer than our interval to cross the threshold.
  //
  this_thread::sleep_for (15ms);

  // The next attempt must succeed and advance the atomic timestamp.
  //
  assert (limiter ());
}
