/*
 Copyright (c) 2026. Giulio Cocconi

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "waveformViewer.hpp"

#include "core/wireUtils.hpp"
#include "utils/ranges_wrapper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#include <QAbstractItemView>
#include <QAction>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTemporaryFile>
#include <QToolBar>
#include <QVBoxLayout>

#include <ui/common/fileDialogUtils.hpp>
#include <ui/common/icons.hpp>
#include <ui/common/inputDialogUtils.hpp>
#include <ui/common/theme.hpp>

namespace SILICON::ui::waveform {
using namespace SILICON::core;
using namespace SILICON::waveform;

namespace {

  constexpr int rulerHeightPx       = 24;
  constexpr int traceRowHeightPx    = 28;
  constexpr int groupHeaderHeightPx = 22;
  constexpr int signalListWidthPx   = 220;
  constexpr int waveformLeftInset   = 16;

  int normalizedIndex(const int index, const int count)
  {
    return index >= 0 && index < count ? index : -1;
  }

  std::pair<quint64, quint64> normalizedInterval(quint64 startTime, quint64 endTime,
                                                 const quint64 duration)
  {
    startTime = std::min(startTime, duration);
    endTime   = std::min(endTime, duration);
    if (endTime < startTime)
      std::swap(startTime, endTime);
    return {startTime, endTime};
  }

  BusValueFormat formatForSignal(const std::vector<BusValueFormat>& formats,
                                 const int                          row)
  {
    return row >= 0 && row < static_cast<int>(formats.size())
               ? formats[static_cast<std::size_t>(row)]
               : BusValueFormat::Hex;
  }

  std::pair<int, int> traceBounds(const int rowY)
  {
    return {rowY + 5, rowY + traceRowHeightPx - 6};
  }

  QColor colorForTraceValue(const BusValue& value)
  {
    if (std::ranges::any_of(value, [](const auto s) { return s == State::ERROR; }))
      return Qt::red;
    if (std::ranges::any_of(value, [](const auto s) { return s == State::UNKNOWN; }))
      return ThemeEngine::getColor("SILICON_VIOLET");
    if (std::ranges::all_of(value, [](const auto s) { return s == State::LOW; }))
      return ThemeEngine::getColor("SILICON_LORANGE");
    return ThemeEngine::getColor("SILICON_ORANGE");
  }

  bool hasInputGroup(int inputCount)
  {
    return inputCount > 0;
  }

  bool hasOutputGroup(int signalCount, int inputCount)
  {
    return inputCount < signalCount;
  }

  int totalGroupHeaderCount(int signalCount, int inputCount)
  {
    return (hasInputGroup(inputCount) ? 1 : 0)
           + (hasOutputGroup(signalCount, inputCount) ? 1 : 0);
  }

  int totalGroupHeaderCountBeforeSignal(int row, int signalCount, int inputCount)
  {
    int count = hasInputGroup(inputCount) ? 1 : 0;
    if (row >= inputCount && hasOutputGroup(signalCount, inputCount))
      ++count;
    return count;
  }

}  // namespace

SignalListWidget::SignalListWidget(QWidget* parent) : QWidget(parent)
{
  setFixedWidth(signalListWidthPx);
}

void SignalListWidget::setTrace(const QStringList& signalNames, int inputCount)
{
  names            = signalNames;
  inputSignalCount = std::clamp<qsizetype>(inputCount, 0, names.size());
  setMinimumSize(signalListWidthPx, signalAreaHeight());
  resize(signalListWidthPx, signalAreaHeight());
  update();
}

void SignalListWidget::setValues(const QStringList& signalValues)
{
  values = signalValues;
  update();
}

void SignalListWidget::setSelectedSignalIndex(const int signalIndex)
{
  const int nextIndex = normalizedIndex(signalIndex, names.size());
  if (selectedSignalIndex == nextIndex)
    return;

  selectedSignalIndex = nextIndex;
  update();
}

int SignalListWidget::signalAreaHeight() const
{
  return totalGroupHeaderCount(names.size(), inputSignalCount) * groupHeaderHeightPx
         + names.size() * traceRowHeightPx;
}

int SignalListWidget::valueColumnX() const
{
  return std::max(96, signalListWidthPx * 33 / 50);
}

int SignalListWidget::yForSignalRow(int row) const
{
  return totalGroupHeaderCountBeforeSignal(row, names.size(), inputSignalCount)
             * groupHeaderHeightPx
         + row * traceRowHeightPx;
}

int SignalListWidget::signalRowAt(const QPoint position) const
{
  if (position.x() < 0 || position.x() >= width())
    return -1;

  for (int row = 0; row < names.size(); ++row) {
    const int y = yForSignalRow(row);
    if (position.y() >= y && position.y() < y + traceRowHeightPx)
      return row;
  }
  return -1;
}

void SignalListWidget::paintEvent(QPaintEvent* event)
{
  QWidget::paintEvent(event);

  QPainter painter(this);
  painter.fillRect(rect(), palette().base());

  const QColor gridColor(220, 220, 220);
  const QColor groupColor       = palette().alternateBase().color();
  const QColor textColor        = palette().text().color();
  const QColor selectedRowColor = ThemeEngine::getColor("SILICON_BLUE").lighter(175);
  const int    valueX           = valueColumnX();

  painter.setPen(gridColor);
  painter.drawLine(valueX - 8, 0, valueX - 8, height());

  auto drawGroup = [&](int y, const QString& label) {
    painter.fillRect(QRect(0, y, width(), groupHeaderHeightPx), groupColor);
    painter.setPen(textColor);
    painter.drawText(QRect(6, y, width() - 12, groupHeaderHeightPx),
                     Qt::AlignVCenter | Qt::AlignLeft, label);
    painter.setPen(gridColor);
    painter.drawLine(0, y + groupHeaderHeightPx - 1, width(),
                     y + groupHeaderHeightPx - 1);
  };

  if (hasInputGroup(inputSignalCount))
    drawGroup(0, tr("Inputs"));
  if (hasOutputGroup(names.size(), inputSignalCount))
    drawGroup(yForSignalRow(inputSignalCount) - groupHeaderHeightPx, tr("Outputs"));

  for (const auto& [row, name] : names | SILICON::views::enumerate) {
    const int     y     = yForSignalRow(static_cast<int>(row));
    const QString value = row < values.size() ? values[row] : QString("x");

    if (static_cast<int>(row) == selectedSignalIndex)
      painter.fillRect(QRect(0, y, width(), traceRowHeightPx), selectedRowColor);

    painter.setPen(textColor);
    painter.drawText(QRect(6, y, valueX - 18, traceRowHeightPx),
                     Qt::AlignVCenter | Qt::AlignLeft, name);
    painter.drawText(QRect(valueX, y, width() - valueX - 6, traceRowHeightPx),
                     Qt::AlignVCenter | Qt::AlignLeft, value);

    painter.setPen(gridColor);
    painter.drawLine(0, y + traceRowHeightPx - 1, width(), y + traceRowHeightPx - 1);
  }
}

void SignalListWidget::mousePressEvent(QMouseEvent* event)
{
  QWidget::mousePressEvent(event);

  const int row = signalRowAt(event->position().toPoint());
  if (row < 0)
    return;

  if (event->button() == Qt::LeftButton) {
    emit signalSelected(row);
  } else if (event->button() == Qt::RightButton) {
    emit signalContextMenuRequested(row, event->globalPosition().toPoint());
  }
}

Canvas::Canvas(QWidget* parent) : QWidget(parent)
{
  setAutoFillBackground(true);
  setMouseTracking(true);
  setMinimumSize(480, rulerHeightPx);
}

void Canvas::setTrace(const QStringList& names, const std::vector<Sample>& samples)
{
  setTrace(names, samples, inputSignalCount);
}

void Canvas::setTrace(const QStringList& names, const std::vector<Sample>& samples,
                      const int inputCount)
{
  if (signalNames != names)
    signalNames = names;

  // The owning Viewer outlives the canvas and keeps the vector stable between
  // GUI-thread refreshes, avoiding an O(history) copy for every new snapshot.
  traceSamples     = &samples;
  inputSignalCount = std::clamp<qsizetype>(inputCount, 0, signalNames.size());
  if (selectedSampleIndex >= static_cast<int>(traceSamples->size()))
    selectedSampleIndex = -1;

  updateCanvasSize();
  update();
}

void Canvas::setSignalFormats(const std::vector<SILICON::core::BusValueFormat>& formats)
{
  signalFormats = formats;
  update();
}

void Canvas::updateCanvasSize()
{
  const int       width         = std::max(480, xForTime(endTime()) + 160);
  const qsizetype minimumHeight = rulerHeightPx + traceRowHeightPx;
  const qsizetype contentHeight =
      rulerHeightPx
      + totalGroupHeaderCount(signalNames.size(), inputSignalCount) * groupHeaderHeightPx
      + signalNames.size() * traceRowHeightPx;
  const qsizetype height = std::max(minimumHeight, contentHeight);
  setMinimumSize(width, height);
  resize(width, height);
}

void Canvas::setPixelsPerTick(const double value)
{
  const double nextPixelsPerTick = std::clamp(value, 2.0, 80.0);
  if (pixelsPerTick == nextPixelsPerTick)
    return;

  pixelsPerTick = nextPixelsPerTick;
  updateCanvasSize();
  update();
}

void Canvas::setSelectedSampleIndex(const int sampleIndex)
{
  const int nextIndex = traceSamples && sampleIndex >= 0
                                && sampleIndex < static_cast<int>(traceSamples->size())
                            ? sampleIndex
                            : -1;
  if (selectedSampleIndex == nextIndex)
    return;

  selectedSampleIndex = nextIndex;
  update();
}

void Canvas::setSelectedSignalIndex(const int signalIndex)
{
  const int nextIndex = normalizedIndex(signalIndex, signalNames.size());
  if (selectedSignalIndex == nextIndex)
    return;

  selectedSignalIndex = nextIndex;
  update();
}

void Canvas::setEditMode(const bool enabled)
{
  if (editMode == enabled)
    return;

  editMode            = enabled;
  editDragSignalIndex = -1;
  updateCanvasSize();
  update();
}

void Canvas::setEditDuration(const quint64 duration)
{
  editDuration           = std::max<quint64>(1, duration);
  editSelectionStartTime = std::min(editSelectionStartTime, editDuration);
  editSelectionEndTime   = std::min(editSelectionEndTime, editDuration);
  updateCanvasSize();
  update();
}

void Canvas::setEditSelection(const int signalIndex, quint64 startTime, quint64 endTime)
{
  const int  nextSignalIndex = normalizedIndex(signalIndex, signalNames.size());
  const auto interval        = normalizedInterval(startTime, endTime, editDuration);
  startTime                  = interval.first;
  endTime                    = interval.second;

  if (editSelectionSignalIndex == nextSignalIndex && editSelectionStartTime == startTime
      && editSelectionEndTime == endTime) {
    return;
  }

  editSelectionSignalIndex = nextSignalIndex;
  editSelectionStartTime   = startTime;
  editSelectionEndTime     = endTime;
  update();
}

void Canvas::setEditSelectionVisible(const bool visible)
{
  if (editSelectionVisible == visible)
    return;

  editSelectionVisible = visible;
  update();
}

quint64 Canvas::endTime() const
{
  if (editMode)
    return editDuration;
  if (!traceSamples || traceSamples->empty())
    return 0;
  return traceSamples->back().time;
}

int Canvas::xForTime(const quint64 time) const
{
  return waveformLeftInset + std::lround(time * pixelsPerTick);
}

quint64 Canvas::timeForX(const int x) const
{
  const double rawTime = std::max(0.0, (x - waveformLeftInset) / pixelsPerTick);
  const auto   rounded = static_cast<quint64>(std::llround(rawTime));
  return std::min(rounded, endTime());
}

int Canvas::yForSignalRow(const int row) const
{
  return rulerHeightPx
         + totalGroupHeaderCountBeforeSignal(row, signalNames.size(), inputSignalCount)
               * groupHeaderHeightPx
         + row * traceRowHeightPx;
}

int Canvas::signalRowAt(const QPoint position) const
{
  if (position.y() < rulerHeightPx)
    return -1;

  for (int row = 0; row < signalNames.size(); ++row) {
    const int y = yForSignalRow(row);
    if (position.y() >= y && position.y() < y + traceRowHeightPx)
      return row;
  }
  return -1;
}

BusValue Canvas::sampleValueAt(const int sampleIndex, const int row) const
{
  if (!traceSamples || sampleIndex < 0
      || sampleIndex >= static_cast<int>(traceSamples->size()))
    return {State::UNKNOWN};

  const auto& values = (*traceSamples)[static_cast<std::size_t>(sampleIndex)].values;
  return row >= 0 && row < static_cast<int>(values.size())
             ? values[static_cast<std::size_t>(row)]
             : BusValue{State::UNKNOWN};
}

void Canvas::paintEvent(QPaintEvent* event)
{
  QWidget::paintEvent(event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.fillRect(rect(), palette().base());

  const QColor gridColor(220, 220, 220);
  const QColor textColor        = palette().text().color();
  const QColor selectedRowColor = ThemeEngine::getColor("SILICON_BLUE").lighter(185);
  const QRect  visibleRect      = event ? event->rect() : rect();

  painter.setPen(gridColor);
  painter.drawLine(0, rulerHeightPx - 1, width(), rulerHeightPx - 1);

  const auto visibleTimeForX = [this](int x) -> quint64 {
    return std::max(0.0, (x - waveformLeftInset) / pixelsPerTick);
  };
  if (!traceSamples)
    return;

  const auto& samples        = *traceSamples;
  const auto  visibleSamples = [&, traceEnd = samples.cend()] {
    using VisibleSampleRange = std::ranges::subrange<std::vector<Sample>::const_iterator>;

    if (samples.empty())
      return VisibleSampleRange{traceEnd, traceEnd};

    // Keep one sample to the left so the segment entering the viewport is preserved.
    auto firstVisible = std::ranges::lower_bound(
        samples, visibleTimeForX(visibleRect.left()), {}, &Sample::time);
    if (firstVisible != samples.cbegin())
      firstVisible = std::prev(firstVisible);

    // Keep one sample to the right so the last visible segment can be completed.
    auto lastVisible =
        std::ranges::upper_bound(std::ranges::subrange(firstVisible, traceEnd),
                                  visibleTimeForX(visibleRect.right()), {}, &Sample::time);
    if (lastVisible != traceEnd)
      lastVisible = std::next(lastVisible);

    return VisibleSampleRange{firstVisible, lastVisible};
  }();
  const auto firstVisibleIndex = visibleSamples.begin() - samples.cbegin();
  const auto lastVisibleIndex  = visibleSamples.end() - samples.cbegin();

  const bool hasSelectedSignal =
      normalizedIndex(selectedSignalIndex, signalNames.size()) >= 0;
  auto drawTimeMarker = [&](const quint64 time, const QPen& linePen, const QPen& labelPen,
                            const bool clampRight) {
    const int     x     = xForTime(time);
    const QString label = QString::number(time);
    const int     labelWidth =
        std::max(40, painter.fontMetrics().horizontalAdvance(label) + 8);
    const int labelX =
        clampRight ? std::clamp(x - labelWidth / 2, 0, std::max(0, width() - labelWidth))
                   : std::max(0, x - labelWidth / 2);

    painter.setPen(linePen);
    painter.drawLine(x, painter.fontMetrics().height() + 2, x, height());
    painter.setPen(labelPen);
    painter.drawText(QRect(labelX, 1, labelWidth, rulerHeightPx - 4),
                     Qt::AlignHCenter | Qt::AlignTop, label);
  };

  const auto isSelectedSignalTick = [&](const int sampleIndex) {
    if (!hasSelectedSignal)
      return true;
    if (sampleIndex < 0 || sampleIndex >= static_cast<int>(samples.size()))
      return false;
    if (sampleIndex == firstVisibleIndex || sampleIndex == 0)
      return true;
    return this->sampleValueAt(sampleIndex, selectedSignalIndex)
           != this->sampleValueAt(sampleIndex - 1, selectedSignalIndex);
  };

  if (hasSelectedSignal && !samples.empty()) {
    for (auto sampleIt = visibleSamples.begin(); sampleIt != visibleSamples.end();
         ++sampleIt) {
      const int sampleIndex = static_cast<int>(sampleIt - samples.cbegin());
      if (isSelectedSignalTick(sampleIndex))
        drawTimeMarker(sampleIt->time, QPen(gridColor), QPen(textColor), false);
    }
  } else {
    for (const Sample& sample : visibleSamples)
      drawTimeMarker(sample.time, QPen(gridColor), QPen(textColor), false);
  }

  auto drawGroupSpacer = [&](int y) {
    painter.fillRect(QRect(0, y, width(), groupHeaderHeightPx), palette().base());
    painter.setPen(gridColor);
    painter.drawLine(0, y + groupHeaderHeightPx - 1, width(),
                     y + groupHeaderHeightPx - 1);
  };

  if (hasInputGroup(inputSignalCount))
    drawGroupSpacer(rulerHeightPx);
  if (hasOutputGroup(signalNames.size(), inputSignalCount))
    drawGroupSpacer(yForSignalRow(inputSignalCount) - groupHeaderHeightPx);

  for (int row = 0; row < signalNames.size(); ++row) {
    const int y = yForSignalRow(row);
    if (row == selectedSignalIndex)
      painter.fillRect(QRect(0, y, width(), traceRowHeightPx), selectedRowColor);
    painter.setPen(gridColor);
    painter.drawLine(0, y + traceRowHeightPx - 1, width(), y + traceRowHeightPx - 1);
  }

  const int editSignalIndex =
      editDragSignalIndex >= 0 ? editDragSignalIndex : editSelectionSignalIndex;
  const bool drawEditSelection = editDragSignalIndex >= 0 || editSelectionVisible;
  if (editMode && drawEditSelection && editSignalIndex >= 0
      && editSignalIndex < signalNames.size()) {
    const quint64 startTime      = editDragSignalIndex >= 0
                                       ? std::min(editDragStartTime, editDragEndTime)
                                       : editSelectionStartTime;
    const quint64 endTime        = editDragSignalIndex >= 0
                                       ? std::max(editDragStartTime, editDragEndTime)
                                       : editSelectionEndTime;
    const int     x0             = xForTime(startTime);
    const int     x1             = xForTime(endTime);
    const int     y              = yForSignalRow(editSignalIndex);
    const QColor  selectionColor = ThemeEngine::getColor("SILICON_BLUE");
    painter.fillRect(
        QRect(std::min(x0, x1), y, std::max(2, std::abs(x1 - x0)), traceRowHeightPx),
        selectionColor.lighter(170));

    const QPen selectionPen(selectionColor, 2);
    drawTimeMarker(startTime, selectionPen, selectionPen, true);
    if (endTime != startTime)
      drawTimeMarker(endTime, selectionPen, selectionPen, true);
  }

  if (samples.empty())
    return;

  // Iterate only the visible time slice instead of the full simulation history.
  for (int row = 0; row < signalNames.size(); ++row) {
    for (int i = firstVisibleIndex; i + 1 < lastVisibleIndex;) {
      const BusValue value = sampleValueAt(i, row);
      int            end   = i + 1;
      while (end < lastVisibleIndex - 1 && sampleValueAt(end, row) == value)
        ++end;

      const int x0 = xForTime(samples[i].time);
      const int x1 = xForTime(samples[end].time);
      if (value.size() == 1) {
        drawScalar(painter, row, x0, x1, value);
        if (end < lastVisibleIndex - 1) {
          const BusValue nextValue = sampleValueAt(end, row);
          if (nextValue.size() == 1)
            drawScalarTransition(painter, row, x1, value, nextValue);
        }
      } else {
        drawBus(painter, row, x0, x1, value);
      }

      i = end;
    }
  }

  if (selectedSampleIndex >= 0 && selectedSampleIndex < static_cast<int>(samples.size())
      && isSelectedSignalTick(selectedSampleIndex)) {
    const QPen selectionPen(ThemeEngine::getColor("SILICON_BLUE"), 2);
    drawTimeMarker(samples[static_cast<std::size_t>(selectedSampleIndex)].time,
                   selectionPen, selectionPen, false);
  }
}

void Canvas::mouseMoveEvent(QMouseEvent* event)
{
  QWidget::mouseMoveEvent(event);

  if (editMode) {
    if (editDragSignalIndex >= 0 && (event->buttons() & Qt::LeftButton)) {
      editDragEndTime = timeForX(event->position().toPoint().x());
      emit editIntervalChanged(editDragSignalIndex,
                               std::min(editDragStartTime, editDragEndTime),
                               std::max(editDragStartTime, editDragEndTime));
      update();
    }
    return;
  }

  if (!traceSamples || traceSamples->empty())
    return;

  const auto& samples = *traceSamples;
  const int   mouseX  = event->position().toPoint().x();

  const bool hasSelectedSignal =
      normalizedIndex(selectedSignalIndex, signalNames.size()) >= 0;

  // Snap to the timestamp where the selected track changes nearest the pointer,
  // matching the ruler ticks; fall back to every timestamp when no track is selected.
  int closestIndex    = -1;
  int closestDistance = std::numeric_limits<int>::max();
  for (int i = 0; i < static_cast<int>(samples.size()); ++i) {
    if (hasSelectedSignal && i > 0
        && sampleValueAt(i, selectedSignalIndex)
               == sampleValueAt(i - 1, selectedSignalIndex))
      continue;
    const int distance = std::abs(mouseX - xForTime(samples[i].time));
    if (distance < closestDistance) {
      closestDistance = distance;
      closestIndex    = i;
    }
  }

  if (closestIndex >= 0)
    emit timestampHovered(closestIndex);
}

void Canvas::mousePressEvent(QMouseEvent* event)
{
  QWidget::mousePressEvent(event);

  const int row = signalRowAt(event->position().toPoint());
  if (row < 0)
    return;

  if (event->button() == Qt::LeftButton) {
    emit signalSelected(row);
    if (editMode) {
      editDragSignalIndex = row;
      editDragStartTime   = timeForX(event->position().toPoint().x());
      editDragEndTime     = editDragStartTime;
      emit editIntervalChanged(row, editDragStartTime, editDragEndTime);
      update();
    }
  } else if (event->button() == Qt::RightButton) {
    emit signalContextMenuRequested(row, event->globalPosition().toPoint());
  }
}

void Canvas::mouseReleaseEvent(QMouseEvent* event)
{
  QWidget::mouseReleaseEvent(event);

  if (!editMode || event->button() != Qt::LeftButton || editDragSignalIndex < 0)
    return;

  editDragEndTime       = timeForX(event->position().toPoint().x());
  const int signalIndex = editDragSignalIndex;
  editDragSignalIndex   = -1;

  const quint64 startTime = std::min(editDragStartTime, editDragEndTime);
  quint64       endTime   = std::max(editDragStartTime, editDragEndTime);
  if (endTime == startTime)
    endTime = std::min(endTime + 1, this->endTime());

  update();
  emit editIntervalChanged(signalIndex, startTime, endTime);
  if (endTime > startTime)
    emit editIntervalSelected(signalIndex, startTime, endTime);
}

int Canvas::yForScalarValue(int row, const BusValue& value) const
{
  const auto [top, bottom] = traceBounds(yForSignalRow(row));
  const int mid            = std::midpoint(top, bottom);

  if (value == BusValue{State::HIGH})
    return top;
  if (value == BusValue{State::LOW})
    return bottom;
  return mid;
}

void Canvas::drawScalar(QPainter& painter, int row, int x0, int x1,
                        const BusValue& value) const
{
  painter.setPen(QPen(colorForTraceValue(value), 2));
  painter.drawLine(x0, yForScalarValue(row, value), x1, yForScalarValue(row, value));
}

void Canvas::drawScalarTransition(QPainter& painter, int row, int x,
                                  const BusValue& previousValue,
                                  const BusValue& nextValue) const
{
  if (previousValue == nextValue)
    return;

  painter.setPen(QPen(colorForTraceValue(previousValue), 2));
  painter.drawLine(x, yForScalarValue(row, previousValue), x,
                   yForScalarValue(row, nextValue));
}

void Canvas::drawBus(QPainter& painter, int row, int x0, int x1,
                     const BusValue& value) const
{
  const auto [top, bottom] = traceBounds(yForSignalRow(row));
  const int mid            = std::midpoint(top, bottom);
  const int offset         = std::min(5, (x1 - x0) / 2);

  painter.setPen(QPen(colorForTraceValue(value), 2));
  painter.drawLine(x0, mid, x0 + offset, top);
  painter.drawLine(x0, mid, x0 + offset, bottom);
  painter.drawLine(x0 + offset, top, x1 - offset, top);
  painter.drawLine(x0 + offset, bottom, x1 - offset, bottom);
  painter.drawLine(x1 - offset, top, x1, mid);
  painter.drawLine(x1 - offset, bottom, x1, mid);

  if (x1 - x0 > 28) {
    painter.setPen(palette().text().color());
    const QString displayValue =
        QString::fromStdString(formatValue(value, formatForSignal(signalFormats, row)));
    painter.drawText(QRect(x0 + 7, top, x1 - x0 - 14, bottom - top), Qt::AlignCenter,
                     displayValue);
  }
}

Viewer::Viewer(QWidget* parent) : QWidget(parent)
{
  const auto root = new QVBoxLayout(this);
  root->setContentsMargins(4, 4, 4, 4);

  const auto makeToolBar = [this] {
    const auto toolBar = new QToolBar(this);
    toolBar->setFloatable(false);
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    return toolBar;
  };
  const auto makeNumberEdit = [this](const int minimum) {
    const auto edit = new QLineEdit(this);
    edit->setValidator(new QIntValidator(minimum, std::numeric_limits<int>::max(), edit));
    edit->setMaximumWidth(96);
    edit->setAlignment(Qt::AlignRight);
    return edit;
  };

  const auto fileToolBar = makeToolBar();

  const auto newAct  = new QAction(Icon("file"), tr("&New"), this);
  const auto openAct = new QAction(Icon("open"), tr("&Open..."), this);
  const auto saveAct = new QAction(Icon("save"), tr("&Save"), this);
  editAct            = new QAction(Icon("pencil"), tr("Edit Inputs"), this);
  editAct->setCheckable(true);

  fileToolBar->addAction(newAct);
  fileToolBar->addAction(openAct);
  fileToolBar->addAction(saveAct);
  fileToolBar->addSeparator();
  fileToolBar->addAction(editAct);
  root->addWidget(fileToolBar);

  const auto splitter = new QSplitter(this);
  splitter->setHandleWidth(0);
  const auto labelPane   = new QWidget();
  const auto labelLayout = new QVBoxLayout(labelPane);
  labelLayout->setContentsMargins(0, 0, 0, 0);
  labelLayout->setSpacing(0);

  const auto labelRulerSpacer = new QWidget(labelPane);
  labelRulerSpacer->setFixedHeight(rulerHeightPx);
  labelLayout->addWidget(labelRulerSpacer);

  signalList      = new SignalListWidget();
  labelScrollArea = new QScrollArea(labelPane);
  canvas          = new Canvas();
  scrollArea      = new QScrollArea();

  labelScrollArea->setFrameShape(QFrame::NoFrame);
  labelScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  labelScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  labelScrollArea->setWidget(signalList);
  labelScrollArea->setWidgetResizable(false);
  labelLayout->addWidget(labelScrollArea);
  scrollArea->setFrameShape(QFrame::NoFrame);
  scrollArea->setWidget(canvas);
  scrollArea->setWidgetResizable(false);

  splitter->addWidget(labelPane);
  splitter->addWidget(scrollArea);
  splitter->setCollapsible(0, false);
  splitter->setCollapsible(1, false);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  labelPane->setFixedWidth(signalListWidthPx);
  root->addWidget(splitter);

  const auto zoomToolBar = makeToolBar();

  const auto zoomOutAct = new QAction(Icon("minus"), tr("Zoom Out"), this);
  const auto zoomInAct  = new QAction(Icon("plus"), tr("Zoom In"), this);

  zoomToolBar->addAction(zoomOutAct);
  zoomToolBar->addAction(zoomInAct);

  durationEdit = makeNumberEdit(1);
  startEdit    = makeNumberEdit(0);
  endEdit      = makeNumberEdit(0);
  startEdit->installEventFilter(this);
  endEdit->installEventFilter(this);

  const auto bottomBar    = new QWidget(this);
  const auto bottomLayout = new QHBoxLayout(bottomBar);
  bottomLayout->setContentsMargins(0, 0, 0, 0);
  bottomLayout->addWidget(zoomToolBar);
  bottomLayout->addStretch();
  bottomLayout->addWidget(new QLabel(tr("Duration"), bottomBar));
  bottomLayout->addWidget(durationEdit);
  bottomLayout->addWidget(new QLabel(tr("Start"), bottomBar));
  bottomLayout->addWidget(startEdit);
  bottomLayout->addWidget(new QLabel(tr("End"), bottomBar));
  bottomLayout->addWidget(endEdit);
  root->addWidget(bottomBar);

  refreshTimer = new QTimer(this);
  refreshTimer->setSingleShot(true);
  // Limit relayout and repaint work to roughly one frame when snapshots arrive rapidly.
  refreshTimer->setInterval(16);
  connect(refreshTimer, &QTimer::timeout, this, [this]() {
    refreshViews();
    if (keepScrolledToEnd)
      scrollArea->horizontalScrollBar()->setValue(
          scrollArea->horizontalScrollBar()->maximum());
    keepScrolledToEnd = false;
  });

  connect(saveAct, &QAction::triggered, this, &Viewer::saveTrace);
  connect(editAct, &QAction::toggled, this, &Viewer::setEditMode);
  connect(zoomInAct, &QAction::triggered, this, [this]() {
    pixelsPerTick *= 1.25;
    refreshCanvas();
  });
  connect(zoomOutAct, &QAction::triggered, this, [this]() {
    pixelsPerTick /= 1.25;
    refreshCanvas();
  });
  connect(scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
          labelScrollArea->verticalScrollBar(), &QScrollBar::setValue);
  connect(labelScrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
          scrollArea->verticalScrollBar(), &QScrollBar::setValue);
  connect(canvas, &Canvas::timestampHovered, this, [this](int sampleIndex) {
    selectedSampleIndex = sampleIndex;
    refreshViews();
  });
  connect(signalList, &SignalListWidget::signalSelected, this,
          &Viewer::setSelectedSignalIndex);
  connect(canvas, &Canvas::signalSelected, this, &Viewer::setSelectedSignalIndex);
  connect(signalList, &SignalListWidget::signalContextMenuRequested, this,
          &Viewer::showSignalFormatMenu);
  connect(canvas, &Canvas::signalContextMenuRequested, this,
          &Viewer::showSignalFormatMenu);
  connect(canvas, &Canvas::editIntervalChanged, this,
          [this](const int signalIndex, const quint64 startTime, const quint64 endTime) {
            setSelectedSignalIndex(signalIndex);
            setEditIntervalFields(startTime, endTime);
          });
  connect(canvas, &Canvas::editIntervalSelected, this, &Viewer::promptEditIntervalValue);
  connect(durationEdit, &QLineEdit::editingFinished, this, [this]() {
    if (!editMode) {
      updateDurationField();
      return;
    }

    bool       ok       = false;
    const auto duration = durationEdit->text().toULongLong(&ok);
    const auto next     = ok ? std::max<quint64>(1, duration) : editDuration;
    editDuration        = next;
    updateDurationField();
    setEditIntervalFields(selectedEditStart, selectedEditEnd);
    rebuildEditableTrace(trace, editDuration);
    refreshViews();
  });
  connect(startEdit, &QLineEdit::returnPressed, this,
          &Viewer::promptSelectedEditIntervalValue);
  connect(endEdit, &QLineEdit::returnPressed, this,
          &Viewer::promptSelectedEditIntervalValue);
  updateEditControls();
}

void Viewer::resetTrace(const QStringList& signalNames, int inputCount,
                        const QList<int>& widths)
{
  std::vector<Signal> signalDefinitions;
  signalDefinitions.reserve(static_cast<std::size_t>(signalNames.size()));
  for (int i = 0; i < signalNames.size(); ++i) {
    const int width = i < widths.size() ? widths[i] : 1;
    signalDefinitions.push_back(
        {signalNames[i].toStdString(), static_cast<std::size_t>(std::max(1, width))});
  }
  SILICON::waveform::resetTrace(trace, std::move(signalDefinitions), inputCount);

  selectedSampleIndex = -1;
  selectedSignalIndex = editMode ? (trace.inputCount > 0 ? 0 : -1)
                                 : (trace.signalDefinitions.empty() ? -1 : 0);
  signalFormats.assign(trace.signalDefinitions.size(),
                       SILICON::core::BusValueFormat::Hex);
  if (editMode)
    rebuildEditableTrace(trace, editDuration);

  signalList->setTrace(visibleNames(), trace.inputCount);
  signalList->setSelectedSignalIndex(selectedSignalIndex);
  updateDurationField();
  refreshViews();
}

bool Viewer::eventFilter(QObject* watched, QEvent* event)
{
  if (watched == startEdit || watched == endEdit) {
    if (event->type() == QEvent::FocusIn) {
      preciseIntervalEditing = true;
      refreshCanvas();
    } else if (event->type() == QEvent::FocusOut) {
      QTimer::singleShot(0, this, [this]() {
        preciseIntervalEditing = startEdit->hasFocus() || endEdit->hasFocus();
        refreshCanvas();
      });
    }
  }

  return QWidget::eventFilter(watched, event);
}

void Viewer::appendSnapshot(quint64 time, const std::vector<BusValue>& values)
{
  appendSnapshots({{time, values}});
}

void Viewer::appendSnapshots(
    const QList<QPair<qulonglong, std::vector<BusValue>>>& snapshots)
{
  if (trace.signalDefinitions.empty() || editMode)
    return;

  keepScrolledToEnd = keepScrolledToEnd
                      || scrollArea->horizontalScrollBar()->value()
                             == scrollArea->horizontalScrollBar()->maximum();

  std::vector<Sample> coreSnapshots;
  coreSnapshots.reserve(snapshots.size());
  for (const auto& [time, values] : snapshots) {
    coreSnapshots.push_back({time, values});
  }
  SILICON::waveform::appendSnapshots(trace, coreSnapshots);

  if (selectedSampleIndex >= static_cast<int>(trace.samples.size()))
    selectedSampleIndex = -1;

  updateDurationField();
  scheduleRefresh();
}

void Viewer::clearTrace()
{
  clearSamples(trace);
  selectedSampleIndex = -1;
  selectedSignalIndex = trace.signalDefinitions.empty() ? -1 : 0;
  updateDurationField();
  refreshViews();
}

void Viewer::setEditMode(const bool enabled)
{
  if (editMode == enabled) {
    updateEditControls();
    return;
  }

  bool       durationOk    = false;
  const auto typedDuration = durationEdit->text().toULongLong(&durationOk);
  if (durationOk)
    editDuration = std::max<quint64>(1, typedDuration);

  editMode            = enabled;
  selectedSampleIndex = -1;
  selectedSignalIndex = trace.inputCount > 0 ? 0 : -1;
  if (editMode) {
    setEditIntervalFields(0, std::min<quint64>(1, editDuration));
    rebuildEditableTrace(trace, editDuration);
  } else {
    preciseIntervalEditing        = false;
    const auto committedSnapshots = editedInputSamples(trace);
    const auto committedDuration  = editDuration;
    emit       editModeChanged(false);
    emit       editTraceCommitted(committedDuration, committedSnapshots);
    updateEditControls();
    return;
  }

  signalList->setTrace(visibleNames(), trace.inputCount);
  signalList->setSelectedSignalIndex(selectedSignalIndex);
  updateEditControls();
  refreshViews();
  emit editModeChanged(true);
}

void Viewer::refreshSignalList()
{
  const int scrollValue = labelScrollArea->verticalScrollBar()->value();
  signalList->setValues(displayedValues());
  signalList->setSelectedSignalIndex(selectedSignalIndex);
  labelScrollArea->verticalScrollBar()->setValue(scrollValue);
}

void Viewer::refreshCanvas()
{
  canvas->setPixelsPerTick(pixelsPerTick);
  canvas->setEditMode(editMode);
  canvas->setEditDuration(editDuration);
  canvas->setTrace(visibleNames(), trace.samples, trace.inputCount);
  canvas->setSignalFormats(signalFormats);
  canvas->setSelectedSampleIndex(selectedSampleIndex);
  canvas->setSelectedSignalIndex(selectedSignalIndex);
  canvas->setEditSelection(selectedSignalIndex, selectedEditStart, selectedEditEnd);
  canvas->setEditSelectionVisible(preciseIntervalEditing);
}

void Viewer::refreshViews()
{
  refreshSignalList();
  refreshCanvas();
}

void Viewer::applyEditInterval(int signalIndex, quint64 startTime, quint64 endTime,
                               const QString& rawValue)
{
  if (!editMode || signalIndex < 0 || signalIndex >= trace.inputCount
      || endTime <= startTime)
    return;

  const auto parsed = valueFromStr(rawValue.toStdString());
  if (parsed.format == BusValueFormat::Unknown || parsed.value.empty()
      || std::ranges::contains(parsed.value, State::ERROR))
    return;

  const auto resized =
      resizeParsedValue(parsed, SILICON::waveform::signalWidth(trace, signalIndex));
  if (!resized)
    return;

  SILICON::waveform::applyEditInterval(trace, editDuration, signalIndex, startTime,
                                       endTime, *resized);
  refreshViews();
}

void Viewer::promptEditIntervalValue(int signalIndex, quint64 startTime, quint64 endTime)
{
  if (!editMode || signalIndex < 0 || signalIndex >= trace.inputCount)
    return;

  setEditIntervalFields(startTime, endTime);

  const std::size_t      width = SILICON::waveform::signalWidth(trace, signalIndex);
  const QPointer<Viewer> safeThis(this);
  if (width <= 1) {
    SILICON::ui::inputDialog::getItem(
        this, tr("Input Value"),
        tr("Set value from %1 to %2").arg(startTime).arg(endTime),
        QStringList{tr("0"), tr("1")}, 0, false,
        [safeThis, signalIndex, startTime, endTime](const QString& text) {
          if (safeThis)
            safeThis->applyEditInterval(signalIndex, startTime, endTime, text);
        });
    return;
  }

  SILICON::ui::inputDialog::getText(
      this, tr("Bus Input"),
      tr("Set %1-bit value from %2 to %3 "
         "(decimal, 0x..., or 0b...)")
          .arg(width)
          .arg(startTime)
          .arg(endTime),
      QString("0"),
      [safeThis, signalIndex, startTime, endTime, width](const QString& text) {
        if (!safeThis)
          return;

        const auto parsed  = valueFromStr(text.toStdString());
        const auto resized = resizeParsedValue(parsed, width);
        if (!resized) {
          SILICON::ui::inputDialog::warning(
              safeThis, QObject::tr("Bus Input"),
              QObject::tr("The value does not fit in the selected signal width."));
          return;
        }

        safeThis->applyEditInterval(
            signalIndex, startTime, endTime,
            QString::fromStdString(formatValue(*resized, BusValueFormat::Raw)));
      });
}

void Viewer::promptSelectedEditIntervalValue()
{
  commitEditIntervalFields();
  promptEditIntervalValue(selectedSignalIndex, selectedEditStart, selectedEditEnd);
}

void Viewer::setEditIntervalFields(quint64 startTime, quint64 endTime)
{
  const auto [normalizedStart, normalizedEnd] =
      normalizedInterval(startTime, endTime, editDuration);
  selectedEditStart = normalizedStart;
  selectedEditEnd   = normalizedEnd;

  const QSignalBlocker startBlocker(startEdit);
  const QSignalBlocker endBlocker(endEdit);
  startEdit->setText(QString::number(selectedEditStart));
  endEdit->setText(QString::number(selectedEditEnd));
  refreshCanvas();
}

void Viewer::commitEditIntervalFields()
{
  bool       startOk = false;
  bool       endOk   = false;
  const auto start   = startEdit->text().toULongLong(&startOk);
  const auto end     = endEdit->text().toULongLong(&endOk);

  setEditIntervalFields(startOk ? start : selectedEditStart,
                        endOk ? end : selectedEditEnd);
}

void Viewer::updateDurationField()
{
  const QSignalBlocker blocker(durationEdit);
  const auto           displayedDuration =
      editMode ? editDuration : (trace.samples.empty() ? 0 : trace.samples.back().time);
  durationEdit->setText(QString::number(displayedDuration));
}

void Viewer::updateEditControls()
{
  const QSignalBlocker blocker(editAct);
  editAct->setChecked(editMode);
  durationEdit->setEnabled(editMode);
  startEdit->setEnabled(editMode);
  endEdit->setEnabled(editMode);
  updateDurationField();
  startEdit->setText(QString::number(selectedEditStart));
  endEdit->setText(QString::number(selectedEditEnd));
}

void Viewer::scheduleRefresh()
{
  // A running single-shot timer represents an already-scheduled frame and absorbs any
  // additional snapshots received before it fires.
  if (!refreshTimer->isActive())
    refreshTimer->start();
}

int Viewer::visibleSignalCount() const
{
  const int signalCount = static_cast<int>(trace.signalDefinitions.size());
  return editMode ? std::min(trace.inputCount, signalCount) : signalCount;
}

QStringList Viewer::visibleNames() const
{
  const int visibleCount = visibleSignalCount();

  QStringList result;
  result.reserve(visibleCount);
  for (int i = 0; i < visibleCount; ++i)
    result.push_back(QString::fromStdString(
        trace.signalDefinitions[static_cast<std::size_t>(i)].name));
  return result;
}

QStringList Viewer::displayedValues() const
{
  const std::vector<BusValue>* rawValues = nullptr;
  if (selectedSampleIndex >= 0
      && selectedSampleIndex < static_cast<int>(trace.samples.size()))
    rawValues = &trace.samples[static_cast<std::size_t>(selectedSampleIndex)].values;
  else if (!trace.samples.empty())
    rawValues = &trace.samples.back().values;
  else
    return {};

  QStringList values;
  values.reserve(static_cast<qsizetype>(rawValues->size()));
  for (int i = 0; i < static_cast<int>(rawValues->size()); ++i)
    values.push_back(displayValue(i, (*rawValues)[static_cast<std::size_t>(i)]));
  return values;
}

QString Viewer::displayValue(const int signalIndex, const BusValue& value) const
{
  if (SILICON::waveform::signalWidth(trace, signalIndex) <= 1)
    return QString::fromStdString(SILICON::core::formatValue(value, BusValueFormat::Raw));

  return QString::fromStdString(
      SILICON::core::formatValue(value, formatForSignal(signalFormats, signalIndex)));
}

void Viewer::setSelectedSignalIndex(const int signalIndex)
{
  const int nextIndex = normalizedIndex(signalIndex, visibleSignalCount());
  if (selectedSignalIndex == nextIndex)
    return;

  selectedSignalIndex = nextIndex;
  refreshViews();
}

void Viewer::showSignalFormatMenu(const int signalIndex, QPoint globalPosition)
{
  if (SILICON::waveform::signalWidth(trace, signalIndex) <= 1)
    return;

  setSelectedSignalIndex(signalIndex);

#ifdef __EMSCRIPTEN__
  auto* menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);
#else
  QMenu stackMenu(this);
  auto* menu = &stackMenu;
#endif

  auto addFormatAction = [&](const QString& label, SILICON::core::BusValueFormat format) {
    QAction* action = menu->addAction(label);
    action->setCheckable(true);
    action->setChecked(formatForSignal(signalFormats, signalIndex) == format);
    connect(action, &QAction::triggered, this, [this, signalIndex, format]() {
      if (normalizedIndex(signalIndex, static_cast<int>(signalFormats.size())) < 0)
        return;
      signalFormats[static_cast<std::size_t>(signalIndex)] = format;
      refreshViews();
    });
  };

  const std::array formats{
      std::pair{tr("SIGNED"), BusValueFormat::Signed},
      std::pair{tr("UNSIGNED"), BusValueFormat::Unsigned},
      std::pair{tr("HEX"), BusValueFormat::Hex},
      std::pair{tr("OCT"), BusValueFormat::Oct},
      std::pair{tr("BIN"), BusValueFormat::Bin},
  };
  for (const auto& [label, format] : formats)
    addFormatAction(label, format);
#ifdef __EMSCRIPTEN__
  menu->popup(globalPosition);
#else
  menu->exec(globalPosition);
#endif
}

void Viewer::saveTrace()
{
#ifdef __EMSCRIPTEN__
  QTemporaryFile traceFile(this);
  if (!traceFile.open()) {
    QMessageBox::warning(
        this, tr("Save Waveform"),
        tr("Cannot write FST trace:\nCould not create a temporary file."));
    return;
  }

  try {
    const QString traceFileName = traceFile.fileName();
    traceFile.close();

    SILICON::waveform::fst::writeTrace(traceFileName.toStdString(), trace);
    QFile file(traceFileName);
    if (!file.open(QIODevice::ReadOnly)) {
      QMessageBox::warning(
          this, tr("Save Waveform"),
          tr("Cannot write FST trace:\nCould not read the generated trace."));
      return;
    }
    SILICON::ui::fileDialog::saveFileContent(
        this, tr("Save Waveform"), QStringLiteral("waveform.fst"),
        tr("FST Trace (*.fst);;All Files (*)"), file.readAll());
  } catch (const std::exception& e) {
    QMessageBox::warning(this, tr("Save Waveform"),
                         tr("Cannot write FST trace:\n%1").arg(e.what()));
  }
#else
  const QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save Waveform"), QString(), tr("FST Trace (*.fst);;All Files (*)"));
  if (fileName.isEmpty())
    return;

  try {
    SILICON::waveform::fst::writeTrace(fileName.toStdString(), trace);
  } catch (const std::exception& e) {
    QMessageBox::warning(this, tr("Save Waveform"),
                         tr("Cannot write FST trace:\n%1").arg(e.what()));
  }
#endif
}

}  // namespace SILICON::ui::waveform
