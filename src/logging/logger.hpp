#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace SILICON::logging {

enum class LogLevel {
  Trace,
  Debug,
  Info,
  Warning,
  Error,
  Critical,
};

struct LogMessage {
  LogLevel    level;
  std::string category;
  std::string message;
  std::string formatted;
};

class Logger {
public:
  using SinkHandle   = std::size_t;
  using CallbackSink = std::function<void(const LogMessage&)>;

  explicit Logger(std::string category);

  static void       initialize();
  static void       shutdown();
  static void       addConsoleSink(std::ostream& stream);
  static void       setMinimumLevel(LogLevel level);
  static SinkHandle addCallbackSink(CallbackSink callback);
  static void       removeSink(SinkHandle handle);

  static void trace(std::string_view category, std::string_view message);
  static void debug(std::string_view category, std::string_view message);
  static void info(std::string_view category, std::string_view message);
  static void warning(std::string_view category, std::string_view message);
  static void error(std::string_view category, std::string_view message);
  static void critical(std::string_view category, std::string_view message);

  void trace(std::string_view message) const;
  void debug(std::string_view message) const;
  void info(std::string_view message) const;
  void warning(std::string_view message) const;
  void error(std::string_view message) const;
  void critical(std::string_view message) const;

private:
  std::string category;

  static void log(LogLevel level, std::string_view category, std::string_view message);
};

}  // namespace SILICON::logging
