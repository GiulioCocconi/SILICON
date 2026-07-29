#pragma once

#include <QWidget>

class QTextEdit;


namespace SILICON {
namespace ui {

class LogSideView : public QWidget {
  Q_OBJECT

public:
  explicit LogSideView(QWidget* parent = nullptr);
  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

public slots:
  void appendLine(const QString& line);
  void clear();

protected:
  void changeEvent(QEvent* event) override;

private:
  void repaintLogText();

  QTextEdit* logOutput;
};

}  // namespace ui
}  // namespace SILICON
