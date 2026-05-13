#include "logSideView.hpp"

#include <QFont>
#include <QFrame>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

#include <ui/common/theme.hpp>

namespace {

QString getLevel(const QString& line)
{
  static const QRegularExpression levelPattern(R"(^\[[^\]]+\]\s+\[([^\]]+)\])",
                                               QRegularExpression::CaseInsensitiveOption);

  const auto match = levelPattern.match(line);

  if (match.hasMatch())
    return match.captured(1).toUpper();

  return "INFO";
}

bool isBold(const QString& level)
{
  return (level != "TRACE") && (level != "DEBUG") && (level != "INFO");
}

QColor levelColor(const QString& level)
{
  if (level == "TRACE")
    return ThemeEngine::getColor("SILICON_BACKGROUND").darker(160);
  if (level == "DEBUG")
    return ThemeEngine::getColor("SILICON_BLUE");
  if (level == "INFO")
    return ThemeEngine::getColor("SILICON_INK").lighter(145);
  if (level == "WARNING")
    return ThemeEngine::getColor("SILICON_ORANGE");
  if (level == "ERROR")
    return ThemeEngine::getColor("SILICON_VIOLET");
  if (level == "FATAL" || level == "CRITICAL")
    return ThemeEngine::getColor("SILICON_VIOLET").darker(150);

  return ThemeEngine::getColor("SILICON_INK");
}

}  // namespace

LogSideView::LogSideView(QWidget* parent) : QWidget(parent)
{
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 10, 10, 0);

  logOutput = new QTextEdit(this);
  logOutput->setReadOnly(true);
  logOutput->setUndoRedoEnabled(false);
  logOutput->setFrameStyle(QFrame::NoFrame);
  logOutput->document()->setMaximumBlockCount(5000);
  logOutput->document()->setDocumentMargin(6);
  logOutput->setProperty("class", "mono");
  logOutput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  layout->addWidget(logOutput);
}

QSize LogSideView::sizeHint() const
{
  return {260, 220};
}

QSize LogSideView::minimumSizeHint() const
{
  return {180, 120};
}

void LogSideView::appendLine(const QString& line)
{
  QTextCharFormat format;
  const QString   level = getLevel(line);

  format.setForeground(levelColor(level));

  if (isBold(level))
    format.setFontWeight(QFont::Bold);

  QTextCursor cursor(logOutput->document());
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(line, format);
  cursor.insertBlock();

  logOutput->setTextCursor(cursor);
  logOutput->ensureCursorVisible();
}

void LogSideView::clear()
{
  logOutput->clear();
}
