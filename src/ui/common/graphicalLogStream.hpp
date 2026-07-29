#pragma once

#include <QObject>

#include <boost/log/sinks/sink.hpp>
#include <boost/shared_ptr.hpp>


namespace SILICON {
namespace ui {

class GraphicalLogStream : public QObject {
  Q_OBJECT

public:
  explicit GraphicalLogStream(QObject* parent = nullptr);
  ~GraphicalLogStream() override;

  void attachToBoostLog();
  void detachFromBoostLog();

signals:
  void lineReceived(QString line);

private:
  boost::shared_ptr<boost::log::sinks::sink> sink;
};

}  // namespace ui
}  // namespace SILICON
