#include "tests.hpp"

#include <mutex>
#include <vector>

#include <logging/logger.hpp>

TEST(LoggerTests, InitializeAndShutdownAreSafe)
{
  EXPECT_NO_THROW(Logger::initialize());
  EXPECT_NO_THROW(Logger::shutdown());
  EXPECT_NO_THROW(Logger::initialize());
  EXPECT_NO_THROW(Logger::shutdown());
}

TEST(LoggerTests, CallbackSinkReceivesCategorySeverityAndMessage)
{
  Logger::initialize();
  Logger::setMinimumLevel(LogLevel::Trace);

  std::mutex              mutex;
  std::vector<LogMessage> messages;
  const auto handle = Logger::addCallbackSink([&](const LogMessage& message) {
    const std::lock_guard lock(mutex);
    messages.push_back(message);
  });

  Logger log("simulation");
  EXPECT_NO_THROW(log.warning("Circuit contains a combinational loop"));

  Logger::removeSink(handle);

  ASSERT_EQ(messages.size(), 1u);
  EXPECT_EQ(messages.front().level, LogLevel::Warning);
  EXPECT_EQ(messages.front().category, "simulation");
  EXPECT_EQ(messages.front().message, "Circuit contains a combinational loop");
  EXPECT_NE(messages.front().formatted.find("simulation"), std::string::npos);
  EXPECT_NE(messages.front().formatted.find("combinational loop"), std::string::npos);

  Logger::shutdown();
}
