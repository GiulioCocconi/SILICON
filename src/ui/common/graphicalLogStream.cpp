#include "graphicalLogStream.hpp"

#include <mutex>
#include <string>

#include <QMetaObject>
#include <QString>

#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/trivial.hpp>
#include <boost/make_shared.hpp>

namespace logging = boost::log;
namespace expr    = boost::log::expressions;

namespace {

class GraphicalLogBackend : public boost::log::sinks::basic_formatted_sink_backend<
                                char, boost::log::sinks::synchronized_feeding> {
public:
  explicit GraphicalLogBackend(GraphicalLogStream* owner) : owner(owner) {}

  void disconnect()
  {
    const std::lock_guard lock(mutex);
    owner = nullptr;
  }

  void consume(const logging::record_view&, const string_type& formatted)
  {
    GraphicalLogStream* owner = nullptr;
    {
      const std::lock_guard lock(mutex);
      owner = this->owner;
    }

    if (!owner)
      return;

    const auto line =
        QString::fromUtf8(formatted.c_str(), static_cast<qsizetype>(formatted.size()));
    QMetaObject::invokeMethod(
        owner, [owner, line] { emit owner->lineReceived(line); }, Qt::QueuedConnection);
  }

private:
  std::mutex          mutex;
  GraphicalLogStream* owner;
};

using GraphicalSink = boost::log::sinks::synchronous_sink<GraphicalLogBackend>;

auto makeFormatter()
{
  return expr::stream << "["
                      << expr::format_date_time<boost::posix_time::ptime>(
                             "TimeStamp", "%Y-%m-%d %H:%M:%S")
                      << "]" << " [" << logging::trivial::severity << "]" << " ["
                      << expr::attr<std::string>("Channel") << "] " << expr::smessage;
}

}  // namespace

GraphicalLogStream::GraphicalLogStream(QObject* parent) : QObject(parent) {}

GraphicalLogStream::~GraphicalLogStream()
{
  detachFromBoostLog();
}

void GraphicalLogStream::attachToBoostLog()
{
  if (sink)
    return;

  auto backend = boost::make_shared<GraphicalLogBackend>(this);
  auto sinkPtr = boost::make_shared<GraphicalSink>(backend);
  sinkPtr->set_formatter(makeFormatter());
  logging::core::get()->add_sink(sinkPtr);
  sink = sinkPtr;
}

void GraphicalLogStream::detachFromBoostLog()
{
  if (!sink)
    return;

  logging::core::get()->remove_sink(sink);

  if (const auto typedSink = boost::dynamic_pointer_cast<GraphicalSink>(sink)) {
    typedSink->locked_backend()->disconnect();
    typedSink->flush();
  }

  sink.reset();
}
