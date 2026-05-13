#pragma once

#include <QWidget>

class QTextEdit;

class LogSideView : public QWidget {
  Q_OBJECT

public:
  explicit LogSideView(QWidget* parent = nullptr);
  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

public slots:
  void appendLine(const QString& line);
  void clear();

private:
  QTextEdit* logOutput;
};
