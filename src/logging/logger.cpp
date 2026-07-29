#include "logger.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include <boost/core/null_deleter.hpp>
#include <boost/log/attributes/current_thread_id.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/keywords/channel.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/make_shared.hpp>

namespace SILICON::logging {

namespace logging  = boost::log;
namespace sinks    = boost::log::sinks;
namespace src      = boost::log::sources;
namespace expr     = boost::log::expressions;
namespace attrs    = boost::log::attributes;
namespace keywords = boost::log::keywords;

namespace {

using Severity      = logging::trivial::severity_level;
using ChannelLogger = src::severity_channel_logger_mt<Severity, std::string>;
using ConsoleSink   = sinks::synchronous_sink<sinks::text_ostream_backend>;

LogLevel fromBoostSeverity(Severity level);

class CallbackSinkBackend
  : public sinks::basic_formatted_sink_backend<char, sinks::synchronized_feeding> {
public:
  explicit CallbackSinkBackend(Logger::CallbackSink callback)
    : callback_(std::move(callback))
  {
  }

  void consume(const logging::record_view& rec, const string_type& formatted)
  {
    if (!callback_)
      return;

    const auto severityRef   = rec[logging::trivial::severity];
    const auto channelRef    = rec.attribute_values()["Channel"].extract<std::string>();
    const auto rawMessageRef = rec.attribute_values()["Message"].extract<std::string>();

    const LogMessage logMessage{
        .level     = severityRef ? fromBoostSeverity(*severityRef) : LogLevel::Info,
        .category  = channelRef ? *channelRef : std::string{},
        .message   = rawMessageRef ? *rawMessageRef : formatted,
        .formatted = formatted,
    };

    callback_(logMessage);
  }

private:
  Logger::CallbackSink callback_;
};

using CallbackFrontendSink = sinks::synchronous_sink<CallbackSinkBackend>;

Severity toBoostSeverity(const LogLevel level)
{
  switch (level) {
    case LogLevel::Trace: return logging::trivial::trace;
    case LogLevel::Debug: return logging::trivial::debug;
    case LogLevel::Info: return logging::trivial::info;
    case LogLevel::Warning: return logging::trivial::warning;
    case LogLevel::Error: return logging::trivial::error;
    case LogLevel::Critical: return logging::trivial::fatal;
  }

  return logging::trivial::info;
}

LogLevel fromBoostSeverity(const Severity level)
{
  switch (level) {
    case logging::trivial::trace: return LogLevel::Trace;
    case logging::trivial::debug: return LogLevel::Debug;
    case logging::trivial::info: return LogLevel::Info;
    case logging::trivial::warning: return LogLevel::Warning;
    case logging::trivial::error: return LogLevel::Error;
    case logging::trivial::fatal: return LogLevel::Critical;
  }

  return LogLevel::Info;
}

struct LoggingState {
  std::mutex                     mutex;
  bool                           initialized      = false;
  bool                           commonAttrsAdded = false;
  LogLevel                       minimumLevel     = LogLevel::Info;
  boost::shared_ptr<ConsoleSink> consoleSink;
  std::map<Logger::SinkHandle, boost::shared_ptr<logging::sinks::sink>> callbackSinks;
  std::atomic<Logger::SinkHandle>                                       nextSinkHandle{1};
};

LoggingState& state()
{
  static LoggingState value;
  return value;
}

const char* toLevelString(const LogLevel level)
{
  switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARNING";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Critical: return "CRITICAL";
  }

  return "INFO";
}

auto makeFormatter()
{
  return expr::stream << "["
                      << expr::format_date_time<boost::posix_time::ptime>(
                             "TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
                      << "]" << " [" << logging::trivial::severity << "]" << " ["
                      << expr::attr<std::string>("Channel") << "] " << expr::smessage;
}

void applyFilter()
{
  logging::core::get()->set_filter(logging::trivial::severity
                                   >= toBoostSeverity(state().minimumLevel));
}

void ensureInitializedLocked(LoggingState& loggingState)
{
  if (loggingState.initialized)
    return;

  if (!loggingState.commonAttrsAdded) {
    logging::add_common_attributes();
    loggingState.commonAttrsAdded = true;
  }

  applyFilter();
  loggingState.initialized = true;
}

void writeRecord(const LogLevel level, std::string_view category,
                 std::string_view message)
{
  ChannelLogger logger(keywords::channel = std::string(category));
  BOOST_LOG_SEV(logger, toBoostSeverity(level)) << message;
}

}  // namespace

std::ostream& operator<<(std::ostream& stream, const LogLevel level)
{
  return stream << toLevelString(level);
}

Logger::Logger(std::string category) : category(std::move(category)) {}

void Logger::initialize()
{
  auto&                 loggingState = state();
  const std::lock_guard lock(loggingState.mutex);
  ensureInitializedLocked(loggingState);
}

void Logger::shutdown()
{
  auto&                 loggingState = state();
  const std::lock_guard lock(loggingState.mutex);

  auto& core = *logging::core::get();
  if (loggingState.consoleSink) {
    core.remove_sink(loggingState.consoleSink);
    loggingState.consoleSink->flush();
    loggingState.consoleSink.reset();
  }

  for (auto& [handle, sink] : loggingState.callbackSinks) {
    (void)handle;
    core.remove_sink(sink);
    sink->flush();
  }
  loggingState.callbackSinks.clear();
  loggingState.initialized = false;
}

void Logger::addConsoleSink(std::ostream& stream)
{
  auto&                 loggingState = state();
  const std::lock_guard lock(loggingState.mutex);
  ensureInitializedLocked(loggingState);

  auto& core = *logging::core::get();
  if (loggingState.consoleSink) {
    core.remove_sink(loggingState.consoleSink);
    loggingState.consoleSink->flush();
    loggingState.consoleSink.reset();
  }

  auto backend = boost::make_shared<sinks::text_ostream_backend>();
  backend->add_stream(boost::shared_ptr<std::ostream>(&stream, boost::null_deleter{}));
  backend->auto_flush(true);

  auto sink = boost::make_shared<ConsoleSink>(backend);
  sink->set_formatter(makeFormatter());
  core.add_sink(sink);
  loggingState.consoleSink = sink;
}

void Logger::setMinimumLevel(const LogLevel level)
{
  auto&                 loggingState = state();
  const std::lock_guard lock(loggingState.mutex);
  loggingState.minimumLevel = level;
  ensureInitializedLocked(loggingState);
  applyFilter();
}

Logger::SinkHandle Logger::addCallbackSink(CallbackSink callback)
{
  auto&                 loggingState = state();
  const std::lock_guard lock(loggingState.mutex);
  ensureInitializedLocked(loggingState);

  auto backend = boost::make_shared<CallbackSinkBackend>(std::move(callback));
  auto sink    = boost::make_shared<CallbackFrontendSink>(backend);
  sink->set_formatter(makeFormatter());

  logging::core::get()->add_sink(sink);

  const SinkHandle handle =
      loggingState.nextSinkHandle.fetch_add(1, std::memory_order_relaxed);
  loggingState.callbackSinks.emplace(handle, sink);
  return handle;
}

void Logger::removeSink(const SinkHandle handle)
{
  auto&                 loggingState = state();
  const std::lock_guard lock(loggingState.mutex);

  const auto it = loggingState.callbackSinks.find(handle);
  if (it == loggingState.callbackSinks.end())
    return;

  logging::core::get()->remove_sink(it->second);
  it->second->flush();
  loggingState.callbackSinks.erase(it);
}

void Logger::trace(std::string_view category, std::string_view message)
{
  log(LogLevel::Trace, category, message);
}

void Logger::debug(std::string_view category, std::string_view message)
{
  log(LogLevel::Debug, category, message);
}

void Logger::info(std::string_view category, std::string_view message)
{
  log(LogLevel::Info, category, message);
}

void Logger::warning(std::string_view category, std::string_view message)
{
  log(LogLevel::Warning, category, message);
}

void Logger::error(std::string_view category, std::string_view message)
{
  log(LogLevel::Error, category, message);
}

void Logger::critical(std::string_view category, std::string_view message)
{
  log(LogLevel::Critical, category, message);
}

void Logger::trace(std::string_view message) const
{
  log(LogLevel::Trace, category, message);
}

void Logger::debug(std::string_view message) const
{
  log(LogLevel::Debug, category, message);
}

void Logger::info(std::string_view message) const
{
  log(LogLevel::Info, category, message);
}

void Logger::warning(std::string_view message) const
{
  log(LogLevel::Warning, category, message);
}

void Logger::error(const std::string_view message) const
{
  log(LogLevel::Error, category, message);
}

void Logger::critical(const std::string_view message) const
{
  log(LogLevel::Critical, category, message);
}

void Logger::log(const LogLevel level, std::string_view category,
                 std::string_view message)
{
  initialize();
  writeRecord(level, category, message);
}

}  // namespace SILICON::logging
