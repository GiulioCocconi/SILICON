/*
 Copyright (c) 2026. Giulio Cocconi

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
*/

#include "binaryEditor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include <QAbstractScrollArea>
#include <QApplication>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <ui/common/theme.hpp>

namespace SILICON::ui {

namespace {

  constexpr std::size_t BytesPerRow = 16;

  [[nodiscard]] QFont monospacedFont()
  {
    auto font = QFont("NovaMono");
    font.setFixedPitch(true);
    return font;
  }

  [[nodiscard]] QString inspectorLabel(const SILICON::core::BusValueFormat format,
                                       const std::size_t                   width)
  {
    switch (format) {
      case SILICON::core::BusValueFormat::Signed:
        return QObject::tr("Signed %1 bit").arg(width);
      case SILICON::core::BusValueFormat::Unsigned:
        return QObject::tr("Unsigned %1 bit").arg(width);
      case SILICON::core::BusValueFormat::Bin: return QObject::tr("Binary");
      case SILICON::core::BusValueFormat::Oct: return QObject::tr("Octal");
      case SILICON::core::BusValueFormat::Hex: return QObject::tr("Hexadecimal");
      default: return {};
    }
  }

}  // namespace

class BinaryHexView final : public QAbstractScrollArea {
public:
  explicit BinaryHexView(BinaryEditor* editor)
    : QAbstractScrollArea(editor), editor(editor)
  {
    setFocusPolicy(Qt::StrongFocus);
    setFont(monospacedFont());
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    verticalScrollBar()->setSingleStep(1);
    recalculateGeometry();
  }

  void dataChanged()
  {
    recalculateGeometry();
    ensureCaretVisible();
    viewport()->update();
  }

  void caretChanged()
  {
    ensureCaretVisible();
    viewport()->update();
  }

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().brush(QPalette::Base));
    painter.setFont(font());

    const auto metrics  = fontMetrics();
    const int  baseline = metrics.ascent() + 2;
    const int  offsetX  = 4;
    const int  hexX     = offsetX + static_cast<int>(offsetDigits + 2) * charWidth;
    const int  asciiX   = hexX + static_cast<int>(bytesPerRow * 3 + 2) * charWidth;

    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(offsetX, baseline, tr("Offset"));
    painter.drawText(hexX, baseline, tr("Hex"));
    painter.drawText(asciiX, baseline, tr("ASCII"));

    if (editor->bytes.isEmpty())
      return;

    const auto firstRow = static_cast<std::size_t>(verticalScrollBar()->value());
    const int  visibleRows =
        std::max(0, (viewport()->height() - headerHeight) / rowHeight + 1);
    const auto byteCount = static_cast<std::size_t>(editor->bytes.size());

    for (int visibleRow = 0; visibleRow < visibleRows; ++visibleRow) {
      const auto row      = firstRow + static_cast<std::size_t>(visibleRow);
      const auto rowStart = row * bytesPerRow;
      if (rowStart >= byteCount)
        break;

      const int top          = headerHeight + visibleRow * rowHeight;
      const int textBaseline = top + metrics.ascent() + 2;
      painter.setPen(palette().color(QPalette::Text));
      painter.drawText(offsetX, textBaseline,
                       QStringLiteral("%1")
                           .arg(static_cast<qulonglong>(rowStart),
                                static_cast<int>(offsetDigits), 16, QLatin1Char('0'))
                           .toUpper());

      const auto count = std::min(bytesPerRow, byteCount - rowStart);
      for (std::size_t column = 0; column < count; ++column) {
        const auto byteIndex = rowStart + column;
        const auto byte      = static_cast<unsigned char>(
            editor->bytes.at(static_cast<qsizetype>(byteIndex)));
        const QString value =
            QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
        const int byteX = hexX + static_cast<int>(column * 3) * charWidth;

        const QRect cell(byteX, top, 2 * charWidth, rowHeight);
        if (byteIndex == editor->selectedByte) {
          painter.fillRect(cell, palette().brush(QPalette::Highlight));
          painter.setPen(palette().color(QPalette::HighlightedText));
        } else {
          painter.setPen(byte == 0 ? ThemeEngine::getColor("SILICON_GREY")
                                   : palette().color(QPalette::Text));
        }
        painter.drawText(cell.left(), textBaseline, value);

        const QRect asciiCell(asciiX + static_cast<int>(column) * charWidth, top,
                              charWidth, rowHeight);
        painter.setPen(palette().color(QPalette::Text));
        const QChar character = byte >= 0x20 && byte <= 0x7e
                                    ? QChar(static_cast<char>(byte))
                                    : QLatin1Char('.');
        painter.drawText(asciiCell.left(), textBaseline, QString(character));
      }
    }
  }

  void resizeEvent(QResizeEvent* event) override
  {
    QAbstractScrollArea::resizeEvent(event);
    recalculateGeometry();
  }

  void mousePressEvent(QMouseEvent* event) override
  {
    if (event->button() != Qt::LeftButton || editor->bytes.isEmpty()
        || event->position().y() < headerHeight) {
      QAbstractScrollArea::mousePressEvent(event);
      return;
    }

    const auto row =
        static_cast<std::size_t>(verticalScrollBar()->value())
        + static_cast<std::size_t>((event->position().y() - headerHeight) / rowHeight);
    const int offsetX = 4;
    const int hexX    = offsetX + static_cast<int>(offsetDigits + 2) * charWidth;
    const int asciiX  = hexX + static_cast<int>(bytesPerRow * 3 + 2) * charWidth;
    const int x       = static_cast<int>(event->position().x());

    std::optional<std::size_t> byteOffset;
    if (x >= hexX && x < hexX + static_cast<int>(bytesPerRow * 3) * charWidth) {
      const auto totalBytes   = static_cast<std::size_t>(editor->bytes.size());
      const auto rowStartByte = row * bytesPerRow;
      const auto byteCount    = rowStartByte >= totalBytes
                                    ? std::size_t{0}
                                    : std::min(bytesPerRow, totalBytes - rowStartByte);
      if (byteCount != 0) {
        const double relative        = event->position().x() - hexX;
        std::size_t  nearest         = 0;
        double       nearestDistance = std::numeric_limits<double>::max();
        for (std::size_t candidate = 0; candidate < byteCount; ++candidate) {
          const double center   = (static_cast<double>(candidate * 3) + 1.0) * charWidth;
          const double distance = std::abs(relative - center);
          if (distance < nearestDistance) {
            nearest         = candidate;
            nearestDistance = distance;
          }
        }
        byteOffset = rowStartByte + nearest;
      }
    } else if (x >= asciiX && x < asciiX + static_cast<int>(bytesPerRow) * charWidth) {
      const auto column = static_cast<std::size_t>((x - asciiX) / charWidth);
      byteOffset        = row * bytesPerRow + column;
    }

    if (byteOffset && editor->setByteOffset(*byteOffset)) {
      setFocus();
      event->accept();
      return;
    }
    QAbstractScrollArea::mousePressEvent(event);
  }

  void keyPressEvent(QKeyEvent* event) override
  {
    if (editor->bytes.isEmpty()) {
      QAbstractScrollArea::keyPressEvent(event);
      return;
    }

    const auto byteCount = static_cast<std::size_t>(editor->bytes.size());
    auto       target    = editor->selectedByte;
    switch (event->key()) {
      case Qt::Key_Left:
        if (target > 0)
          --target;
        break;
      case Qt::Key_Right:
        if (target + 1 < byteCount)
          ++target;
        break;
      case Qt::Key_Up: {
        const auto distance = bytesPerRow;
        target              = target >= distance ? target - distance : target;
        break;
      }
      case Qt::Key_Down: target = std::min(byteCount - 1, target + bytesPerRow); break;
      case Qt::Key_Home: target = (target / bytesPerRow) * bytesPerRow; break;
      case Qt::Key_End:
        target = std::min(byteCount - 1, (target / bytesPerRow + 1) * bytesPerRow - 1);
        break;
      default: {
        constexpr auto shortcutModifiers =
            Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
        if (!(event->modifiers() & shortcutModifiers)) {
          const auto text = event->text().toUpper();
          if (text.size() == 1) {
            const QChar character = text.front();
            const int   digit =
                character.isDigit() ? character.digitValue()
                  : character >= QLatin1Char('A') && character <= QLatin1Char('F')
                      ? character.unicode() - QLatin1Char('A').unicode() + 10
                      : -1;
            if (digit >= 0 && digit < 16) {
              editor->overwriteHexDigit(static_cast<unsigned>(digit));
              event->accept();
              return;
            }
          }
        }
        QAbstractScrollArea::keyPressEvent(event);
        return;
      }
    }

    editor->setByteOffset(target);
    event->accept();
  }

private:
  void recalculateGeometry()
  {
    const QFontMetrics metrics(font());
    charWidth    = std::max(1, metrics.horizontalAdvance(QLatin1Char('0')));
    rowHeight    = metrics.height() + 4;
    headerHeight = rowHeight;

    const auto byteCount     = static_cast<std::size_t>(editor->bytes.size());
    const auto maximumOffset = byteCount == 0 ? 0 : byteCount - 1;
    offsetDigits             = 8;
    for (auto value = maximumOffset; value >= (std::uint64_t{1} << 32); value >>= 4)
      ++offsetDigits;

    bytesPerRow = BytesPerRow;

    const auto rowCount =
        byteCount == 0 ? std::size_t{0} : (byteCount + bytesPerRow - 1) / bytesPerRow;
    const int visibleRows =
        std::max(1, (viewport()->height() - headerHeight) / rowHeight);
    const auto maximumRow = rowCount > static_cast<std::size_t>(visibleRows)
                                ? rowCount - static_cast<std::size_t>(visibleRows)
                                : 0;
    verticalScrollBar()->setPageStep(visibleRows);
    verticalScrollBar()->setRange(0, static_cast<int>(std::min<std::size_t>(
                                         maximumRow, std::numeric_limits<int>::max())));
    viewport()->update();
  }

  void ensureCaretVisible()
  {
    if (editor->bytes.isEmpty())
      return;
    const auto row     = editor->selectedByte / bytesPerRow;
    const auto first   = static_cast<std::size_t>(verticalScrollBar()->value());
    const auto visible = static_cast<std::size_t>(
        std::max(1, (viewport()->height() - headerHeight) / rowHeight));
    if (row < first)
      verticalScrollBar()->setValue(static_cast<int>(row));
    else if (row >= first + visible)
      verticalScrollBar()->setValue(static_cast<int>(row - visible + 1));
  }

  BinaryEditor* editor;
  int           charWidth    = 1;
  int           rowHeight    = 1;
  int           headerHeight = 1;
  std::size_t   offsetDigits = 8;
  std::size_t   bytesPerRow  = 1;
};

class BinaryNibbleEditCommand final : public QUndoCommand {
public:
  BinaryNibbleEditCommand(BinaryEditor* editor, const std::size_t offset,
                          std::vector<unsigned> before, std::vector<unsigned> after,
                          QString text)
    : QUndoCommand(std::move(text)),
      editor(editor),
      offset(offset),
      before(std::move(before)),
      after(std::move(after))
  {
  }

  void undo() override { editor->applyNibbles(offset, before); }
  void redo() override { editor->applyNibbles(offset, after); }

private:
  BinaryEditor*         editor;
  std::size_t           offset;
  std::vector<unsigned> before;
  std::vector<unsigned> after;
};

BinaryEditor::BinaryEditor(QWidget* parent)
  : QWidget(parent),
    hexView(new BinaryHexView(this)),
    offsetEdit(new QLineEdit(this)),
    undoHistory(new QUndoStack(this))
{
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(hexView, 1);

  auto* inspectorLayout = new QGridLayout();
  layout->addLayout(inspectorLayout);

  const std::array definitions{
      std::pair{SILICON::core::BusValueFormat::Signed, std::size_t{8}},
      std::pair{SILICON::core::BusValueFormat::Signed, std::size_t{16}},
      std::pair{SILICON::core::BusValueFormat::Signed, std::size_t{32}},
      std::pair{SILICON::core::BusValueFormat::Signed, std::size_t{64}},
      std::pair{SILICON::core::BusValueFormat::Unsigned, std::size_t{8}},
      std::pair{SILICON::core::BusValueFormat::Unsigned, std::size_t{16}},
      std::pair{SILICON::core::BusValueFormat::Unsigned, std::size_t{32}},
      std::pair{SILICON::core::BusValueFormat::Unsigned, std::size_t{64}},
      std::pair{SILICON::core::BusValueFormat::Bin, std::size_t{8}},
      std::pair{SILICON::core::BusValueFormat::Oct, std::size_t{8}},
      std::pair{SILICON::core::BusValueFormat::Hex, std::size_t{8}},
  };

  for (std::size_t index = 0; index < definitions.size(); ++index) {
    const auto [format, width] = definitions[index];
    auto* container            = new QWidget(this);
    auto* fieldLayout          = new QVBoxLayout(container);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(2);
    fieldLayout->addWidget(new QLabel(inspectorLabel(format, width), container));
    auto* edit = new QLineEdit(container);
    edit->setProperty("class", "mono");
    edit->setFont(monospacedFont());
    fieldLayout->addWidget(edit);
    inspectorLayout->addWidget(container, static_cast<int>(index / 4),
                               static_cast<int>(index % 4));
    inspectors.push_back({.edit = edit, .format = format, .width = width});
  }

  for (std::size_t index = 0; index < inspectors.size(); ++index) {
    connect(inspectors[index].edit, &QLineEdit::editingFinished, this,
            [this, index] { commitInspector(inspectors[index]); });
  }

  auto* offsetContainer = new QWidget(this);
  auto* offsetLayout    = new QVBoxLayout(offsetContainer);
  offsetLayout->setContentsMargins(0, 0, 0, 0);
  offsetLayout->setSpacing(2);
  offsetLayout->addWidget(new QLabel(tr("Offset"), offsetContainer));
  offsetEdit->setProperty("class", "mono");
  offsetEdit->setFont(monospacedFont());
  offsetEdit->setPlaceholderText(tr("Decimal or 0x-prefixed hexadecimal"));
  offsetLayout->addWidget(offsetEdit);
  inspectorLayout->addWidget(offsetContainer, 2, 3);
  connect(offsetEdit, &QLineEdit::editingFinished, this, &BinaryEditor::commitOffset);

  connect(undoHistory, &QUndoStack::indexChanged, this, [this] {
    highHexDigitEntered = false;
    updateEditorState();
  });
  refreshInspectors();
  refreshOffset();
}

BinaryEditor::~BinaryEditor()
{
  const QSignalBlocker blocker(undoHistory);
  undoHistory->clear();
}

void BinaryEditor::setData(QByteArray data)
{
  bytes               = std::move(data);
  selectedByte        = 0;
  highHexDigitEntered = false;
  undoHistory->clear();
  undoHistory->setClean();
  updateEditorState();
  refreshOffset();
}

const QByteArray& BinaryEditor::data() const noexcept
{
  return bytes;
}

std::size_t BinaryEditor::byteOffset() const noexcept
{
  return selectedByte;
}

bool BinaryEditor::setByteOffset(const std::size_t offset)
{
  if (offset >= static_cast<std::size_t>(bytes.size()))
    return false;
  selectedByte        = offset;
  highHexDigitEntered = false;
  hexView->caretChanged();
  refreshInspectors();
  refreshOffset();
  return true;
}

bool BinaryEditor::goToByteOffset(const std::size_t offset)
{
  return setByteOffset(offset);
}

bool BinaryEditor::canReadValue(const std::size_t bitWidth) const
{
  return bitWidth >= 8 && bitWidth <= 64 && bitWidth % 8 == 0
         && selectedByte + bitWidth / 8 <= static_cast<std::size_t>(bytes.size());
}

std::optional<std::uint64_t> BinaryEditor::readValue(const std::size_t bitWidth) const
{
  if (!canReadValue(bitWidth))
    return std::nullopt;

  std::uint64_t result = 0;
  for (std::size_t byteIndex = 0; byteIndex < bitWidth / 8; ++byteIndex) {
    const auto high = nibbleAt((selectedByte + byteIndex) * 2);
    const auto low  = nibbleAt((selectedByte + byteIndex) * 2 + 1);
    result |= static_cast<std::uint64_t>((high << 4) | low) << (byteIndex * 8);
  }
  return result;
}

bool BinaryEditor::overwriteValue(const std::uint64_t value, const std::size_t bitWidth,
                                  const QString& undoText)
{
  if (!canReadValue(bitWidth))
    return false;
  std::vector<unsigned> nibbles;
  nibbles.reserve(bitWidth / 4);
  for (std::size_t byteIndex = 0; byteIndex < bitWidth / 8; ++byteIndex) {
    const auto byte = static_cast<unsigned>((value >> (byteIndex * 8)) & 0xffU);
    nibbles.push_back(byte >> 4);
    nibbles.push_back(byte & 0x0fU);
  }
  highHexDigitEntered = false;
  pushNibbleEdit(selectedByte * 2, std::move(nibbles),
                 undoText.isEmpty() ? tr("Edit binary value") : undoText);
  return true;
}

bool BinaryEditor::overwriteHexDigit(const unsigned digit)
{
  if (digit > 0x0f || bytes.isEmpty())
    return false;
  const bool enteringLowDigit = highHexDigitEntered;
  const auto editOffset       = selectedByte * 2 + (enteringLowDigit ? 1 : 0);
  pushNibbleEdit(editOffset, {digit}, tr("Edit hexadecimal byte"));
  if (!enteringLowDigit) {
    highHexDigitEntered = true;
  } else {
    highHexDigitEntered = false;
    if (selectedByte + 1 < static_cast<std::size_t>(bytes.size()))
      setByteOffset(selectedByte + 1);
  }
  return true;
}

QUndoStack* BinaryEditor::history() const noexcept
{
  return undoHistory;
}

bool BinaryEditor::isModified() const
{
  return !undoHistory->isClean();
}

void BinaryEditor::setModified(const bool modified)
{
  if (!modified)
    undoHistory->setClean();
}

unsigned BinaryEditor::nibbleAt(const std::size_t offset) const
{
  const auto byte =
      static_cast<unsigned char>(bytes.at(static_cast<qsizetype>(offset / 2)));
  return offset % 2 == 0 ? byte >> 4 : byte & 0x0fU;
}

void BinaryEditor::applyNibbles(const std::size_t            offset,
                                const std::vector<unsigned>& values)
{
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto nibbleOffset = offset + index;
    const auto byteIndex    = static_cast<qsizetype>(nibbleOffset / 2);
    auto       byte         = static_cast<unsigned char>(bytes.at(byteIndex));
    if (nibbleOffset % 2 == 0)
      byte = static_cast<unsigned char>((byte & 0x0fU) | (values[index] << 4));
    else
      byte = static_cast<unsigned char>((byte & 0xf0U) | values[index]);
    bytes[byteIndex] = static_cast<char>(byte);
  }
  updateEditorState();
}

void BinaryEditor::pushNibbleEdit(const std::size_t offset, std::vector<unsigned> values,
                                  const QString& text)
{
  std::vector<unsigned> before;
  before.reserve(values.size());
  for (std::size_t index = 0; index < values.size(); ++index)
    before.push_back(nibbleAt(offset + index));
  if (before == values)
    return;
  undoHistory->push(new BinaryNibbleEditCommand(this, offset, std::move(before),
                                                std::move(values), text));
}

void BinaryEditor::commitInspector(Inspector& inspector)
{
  if (!inspector.edit->isEnabled() || !inspector.edit->isModified())
    return;
  const auto parsed = SILICON::core::parseInteger(inspector.edit->text().toStdString(),
                                                  inspector.format, inspector.width);
  if (!parsed) {
    QApplication::beep();
    inspector.edit->setToolTip(tr("The value is invalid or does not fit this width."));
    inspector.edit->selectAll();
    return;
  }
  inspector.edit->setModified(false);
  inspector.edit->setToolTip({});
  overwriteValue(*parsed, inspector.width,
                 tr("Edit %1").arg(inspectorLabel(inspector.format, inspector.width)));
}

void BinaryEditor::refreshInspectors()
{
  for (auto& inspector : inspectors) {
    const auto value = readValue(inspector.width);
    inspector.edit->setEnabled(value.has_value());
    inspector.edit->setToolTip({});
    auto text = value ? QString::fromStdString(SILICON::core::formatInteger(
                            *value, inspector.format, inspector.width))
                      : QString();
    if (value && inspector.format == SILICON::core::BusValueFormat::Hex)
      text = text.rightJustified(static_cast<int>((inspector.width + 3) / 4),
                                 QLatin1Char('0'));
    inspector.edit->setText(text);
    inspector.edit->setModified(false);
  }
}

void BinaryEditor::commitOffset()
{
  if (!offsetEdit->isEnabled() || !offsetEdit->isModified())
    return;

  const auto text   = offsetEdit->text().trimmed();
  const auto format = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                          ? SILICON::core::BusValueFormat::Hex
                          : SILICON::core::BusValueFormat::Unsigned;
  const auto offset = SILICON::core::parseInteger(text.toStdString(), format, 64);
  if (!offset || *offset >= static_cast<std::uint64_t>(bytes.size())) {
    QApplication::beep();
    offsetEdit->setToolTip(tr("Enter an offset within the current binary file."));
    offsetEdit->selectAll();
    return;
  }

  offsetEdit->setModified(false);
  offsetEdit->setToolTip({});
  setByteOffset(static_cast<std::size_t>(*offset));
  hexView->setFocus();
}

void BinaryEditor::refreshOffset()
{
  const bool available = !bytes.isEmpty();
  offsetEdit->setEnabled(available);
  offsetEdit->setToolTip({});
  offsetEdit->setText(
      available
          ? QStringLiteral("0x%1").arg(
                QString::number(static_cast<qulonglong>(selectedByte), 16).toUpper())
          : QString());
  offsetEdit->setModified(false);
}

void BinaryEditor::updateEditorState()
{
  hexView->dataChanged();
  refreshInspectors();
}

}  // namespace SILICON::ui
