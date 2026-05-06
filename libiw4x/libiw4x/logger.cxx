#include <libiw4x/logger.hxx>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogFunctions.h>
#include <quill/Logger.h>

#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/RotatingFileSink.h>

#include <libiw4x/version.hxx>

using namespace std;
using namespace quill;

namespace iw4x
{
  namespace log
  {
    namespace detail
    {
      inline atomic<Logger*>&
      logger () noexcept
      {
        static atomic<Logger*> instance (nullptr);
        return instance;
      }

      static constexpr LogLevel
      to_quill_level (level l) noexcept
      {
        switch (l)
        {
          case level::trace_l3: return LogLevel::TraceL3;
          case level::trace_l2: return LogLevel::TraceL2;
          case level::trace_l1: return LogLevel::TraceL1;
          case level::debug:    return LogLevel::Debug;
          case level::info:     return LogLevel::Info;
          case level::notice:   return LogLevel::Notice;
          case level::warning:  return LogLevel::Warning;
          case level::error:    return LogLevel::Error;
          case level::critical: return LogLevel::Critical;
          default:              return LogLevel::Info;
        }
      }

      bool
      should_log_statement (level l) noexcept
      {
        Logger* logger_ptr (logger ().load (memory_order_acquire));

        return logger_ptr != nullptr &&
               logger_ptr->should_log_statement (to_quill_level (l));
      }

      void
      emit (level l, const source_location& loc, const string& msg)
      {
        Logger* logger_ptr (logger ().load (memory_order_acquire));

        if (logger_ptr != nullptr)
        {
          SourceLocation quill_loc {loc.file_name (),
                                           loc.function_name (),
                                           static_cast<uint32_t> (loc.line ())};

          quill::log (logger_ptr, "", to_quill_level (l), "{}", quill_loc, msg);
        }
      }
    }
  }

  class logger* logger (nullptr);

  logger::
  logger ()
  {
    Backend::start ({
      .enable_yield_when_idle               = true,
      .sleep_duration                       = 0ns,
      .wait_for_queues_to_empty_before_exit = false,
      .check_printable_char                 = {},
      .log_level_short_codes                =
      {
        "3", "2", "1", "D", "I", "N", "W", "E", "C", "B", "_"
      }
    });

    ConsoleSinkConfig c;
    c.set_colour_mode (ConsoleSinkConfig::ColourMode::Always);

    FileSinkConfig r;
    r.set_filename_append_option (FilenameAppendOption::StartDateTime);

    auto cs (Frontend::create_or_get_sink<ConsoleSink> ("cs",       c));
    auto fs (Frontend::create_or_get_sink<FileSink>    ("iw4x.log", r));

    PatternFormatterOptions pf (
      "[%(log_level_short_code)] %(short_source_location:<24) %(message)");

    Logger* l (Frontend::create_or_get_logger ("iw4x", {cs, fs}, pf));

    l->set_log_level (LogLevel::Info);
#if LIBIW4X_DEVELOP
    l->set_log_level (LogLevel::TraceL3);
#endif

    log::detail::logger ().store (l, memory_order_release);

    log::notice << "IW4x " LIBIW4X_VERSION_FULL;
  }
}
