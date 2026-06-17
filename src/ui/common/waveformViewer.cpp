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

#include "utils/ranges_wrapper.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#include <QAbstractItemView>
#include <QAction>
#include <QFileDialog>
#include <QFrame>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QScrollBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <core/fstTraceWriter.hpp>
#include <ui/common/icons.hpp>
#include <ui/common/theme.hpp>

namespace {

constexpr int rulerHeightPx       = 24;
constexpr int traceRowHeightPx    = 28;
constexpr int groupHeaderHeightPx = 22;
constexpr int signalListWidthPx   = 220;
constexpr int waveformLeftInset   = 16;

QColor colorForTraceValue(const QString& value)
{
  if (value.contains('z'))
    return Qt::red;
  if (value.contains('x'))
    return ThemeEngine::getColor("SILICON_VIOLET");
  if (std::ranges::all_of(value, [](QChar ch) { return ch == '0'; }))
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

int groupHeaderCount(int signalCount, int inputCount)
{
  return (hasInputGroup(inputCount) ? 1 : 0)
         + (hasOutputGroup(signalCount, inputCount) ? 1 : 0);
}

int groupHeaderCountBeforeSignal(int row, int signalCount, int inputCount)
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

int SignalListWidget::signalAreaHeight() const
{
  return groupHeaderCount(names.size(), inputSignalCount) * groupHeaderHeightPx
         + names.size() * traceRowHeightPx;
}

int SignalListWidget::valueColumnX() const
{
  return std::max(96, signalListWidthPx * 33 / 50);
}

int SignalListWidget::yForSignalRow(int row) const
{
  return groupHeaderCountBeforeSignal(row, names.size(), inputSignalCount)
             * groupHeaderHeightPx
         + row * traceRowHeightPx;
}

void SignalListWidget::paintEvent(QPaintEvent* event)
{
  QWidget::paintEvent(event);

  QPainter painter(this);
  painter.fillRect(rect(), palette().base());

  const QColor gridColor(220, 220, 220);
  const QColor groupColor = palette().alternateBase().color();
  const QColor textColor  = palette().text().color();
  const int    valueX     = valueColumnX();

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

  for (const auto& [row, name] : names | silicon::views::enumerate) {
    const int     y     = yForSignalRow(static_cast<int>(row));
    const QString value = row < values.size() ? values[row] : QString("x");

    painter.setPen(textColor);
    painter.drawText(QRect(6, y, valueX - 18, traceRowHeightPx),
                     Qt::AlignVCenter | Qt::AlignLeft, name);
    painter.drawText(QRect(valueX, y, width() - valueX - 6, traceRowHeightPx),
                     Qt::AlignVCenter | Qt::AlignLeft, value);

    painter.setPen(gridColor);
    painter.drawLine(0, y + traceRowHeightPx - 1, width(), y + traceRowHeightPx - 1);
  }
}

WaveformCanvas::WaveformCanvas(QWidget* parent) : QWidget(parent)
{
  setAutoFillBackground(true);
  setMouseTracking(true);
  setMinimumSize(480, rulerHeight());
}

void WaveformCanvas::setTrace(const QStringList&         names,
                              const std::vector<Sample>& samples)
{
  setTrace(names, samples, inputSignalCount);
}

void WaveformCanvas::setTrace(const QStringList&         names,
                              const std::vector<Sample>& samples, const int inputCount)
{
  if (signalNames != names)
    signalNames = names;

  // The owning WaveformViewer outlives the canvas and keeps the vector stable between
  // GUI-thread refreshes, avoiding an O(history) copy for every new snapshot.
  traceSamples     = &samples;
  inputSignalCount = std::clamp<qsizetype>(inputCount, 0, signalNames.size());
  if (selectedSampleIndex >= static_cast<int>(traceSamples->size()))
    selectedSampleIndex = -1;

  updateCanvasSize();
  update();
}

void WaveformCanvas::updateCanvasSize()
{
  const int       width         = std::max(480, xForTime(endTime()) + 160);
  const qsizetype minimumHeight = rulerHeight() + rowHeight();
  const qsizetype contentHeight = rulerHeight() + groupHeaderCount() * groupHeaderHeight()
                                  + signalNames.size() * rowHeight();
  const qsizetype height = std::max(minimumHeight, contentHeight);
  setMinimumSize(width, height);
  resize(width, height);
}

void WaveformCanvas::setPixelsPerTick(const double value)
{
  const double nextPixelsPerTick = std::clamp(value, 2.0, 80.0);
  if (pixelsPerTick == nextPixelsPerTick)
    return;

  pixelsPerTick = nextPixelsPerTick;
  updateCanvasSize();
  update();
}

void WaveformCanvas::setSelectedSampleIndex(const int sampleIndex)
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

quint64 WaveformCanvas::endTime() const
{
  if (!traceSamples || traceSamples->empty())
    return 0;
  return traceSamples->back().time;
}

int WaveformCanvas::xForTime(const quint64 time) const
{
  return waveformLeftInset + std::lround(time * pixelsPerTick);
}

int WaveformCanvas::groupHeaderCount() const
{
  return ::groupHeaderCount(signalNames.size(), inputSignalCount);
}

int WaveformCanvas::groupHeaderCountBeforeSignal(const int row) const
{
  return ::groupHeaderCountBeforeSignal(row, signalNames.size(), inputSignalCount);
}

int WaveformCanvas::yForSignalRow(const int row) const
{
  return rulerHeight() + groupHeaderCountBeforeSignal(row) * groupHeaderHeight()
         + row * rowHeight();
}

void WaveformCanvas::paintEvent(QPaintEvent* event)
{
  QWidget::paintEvent(event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.fillRect(rect(), palette().base());

  const QColor gridColor(220, 220, 220);
  const QColor textColor   = palette().text().color();
  const QRect  visibleRect = event ? event->rect() : rect();

  painter.setPen(gridColor);
  painter.drawLine(0, rulerHeight() - 1, width(), rulerHeight() - 1);

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

  for (const Sample& sample : visibleSamples) {
    const int     x     = xForTime(sample.time);
    const QString label = QString::number(sample.time);
    const int     labelWidth =
        std::max(40, painter.fontMetrics().horizontalAdvance(label) + 8);
    const int labelX = std::max(0, x - labelWidth / 2);

    painter.setPen(gridColor);
    painter.drawLine(x, painter.fontMetrics().height() + 2, x, height());
    painter.setPen(textColor);
    painter.drawText(QRect(labelX, 1, labelWidth, rulerHeight() - 4),
                     Qt::AlignHCenter | Qt::AlignTop, label);
  }

  auto drawGroupSpacer = [&](int y) {
    painter.fillRect(QRect(0, y, width(), groupHeaderHeight()), palette().base());
    painter.setPen(gridColor);
    painter.drawLine(0, y + groupHeaderHeight() - 1, width(),
                     y + groupHeaderHeight() - 1);
  };

  if (hasInputGroup(inputSignalCount))
    drawGroupSpacer(rulerHeight());
  if (hasOutputGroup(signalNames.size(), inputSignalCount))
    drawGroupSpacer(yForSignalRow(inputSignalCount) - groupHeaderHeight());

  for (int row = 0; row < signalNames.size(); ++row) {
    const int y = yForSignalRow(row);
    painter.setPen(gridColor);
    painter.drawLine(0, y + rowHeight() - 1, width(), y + rowHeight() - 1);
  }

  if (samples.empty())
    return;

  // Iterate only the visible time slice instead of the full simulation history.
  const auto firstVisibleIndex = visibleSamples.begin() - samples.cbegin();
  const auto lastVisibleIndex  = visibleSamples.end() - samples.cbegin();
  const auto valueAt = [&samples](int sampleIndex, int row) {
    return row < samples[sampleIndex].values.size()
               ? samples[sampleIndex].values[row]
               : QString("x");
  };

  for (int row = 0; row < signalNames.size(); ++row) {
    for (int i = firstVisibleIndex; i + 1 < lastVisibleIndex;) {
      const QString value = valueAt(i, row);
      int           end   = i + 1;
      while (end < lastVisibleIndex - 1 && valueAt(end, row) == value)
        ++end;

      const int x0 = xForTime(samples[i].time);
      const int x1 = xForTime(samples[end].time);
      if (value.size() == 1) {
        drawScalar(painter, row, x0, x1, value);
        if (end < lastVisibleIndex - 1) {
          const QString nextValue = valueAt(end, row);
          if (nextValue.size() == 1)
            drawScalarTransition(painter, row, x1, value, nextValue);
        }
      } else {
        drawBus(painter, row, x0, x1, value);
      }

      i = end;
    }
  }

  if (selectedSampleIndex >= 0
      && selectedSampleIndex < static_cast<int>(samples.size())) {
    const int     x     = xForTime(samples[selectedSampleIndex].time);
    const QString label = QString::number(samples[selectedSampleIndex].time);
    const int     labelWidth =
        std::max(40, painter.fontMetrics().horizontalAdvance(label) + 8);
    const int labelX = std::max(0, x - labelWidth / 2);

    painter.setPen(QPen(ThemeEngine::getColor("SILICON_BLUE"), 2));
    painter.drawLine(x, painter.fontMetrics().height() + 2, x, height());
    painter.drawText(QRect(labelX, 1, labelWidth, rulerHeight() - 4),
                     Qt::AlignHCenter | Qt::AlignTop, label);
  }
}

void WaveformCanvas::mouseMoveEvent(QMouseEvent* event)
{
  QWidget::mouseMoveEvent(event);

  if (!traceSamples || traceSamples->empty())
    return;

  const auto& samples = *traceSamples;
  const int   mouseX  = event->position().toPoint().x();
  const auto  it      = std::lower_bound(
      samples.begin(), samples.end(), mouseX,
      [this](const Sample& sample, const int x) { return xForTime(sample.time) < x; });

  auto       closestIndex    = 0;
  int        closestDistance = std::abs(mouseX - xForTime(samples.front().time));
  const auto considerSample  = [this, mouseX, &samples, &closestIndex,
                               &closestDistance](auto sampleIt) {
    const int distance = std::abs(mouseX - xForTime(sampleIt->time));
    if (distance < closestDistance) {
      closestDistance = distance;
      closestIndex    = sampleIt - samples.begin();
    }
  };

  if (it != samples.begin())
    considerSample(it - 1);
  if (it != samples.end())
    considerSample(it);

  emit timestampHovered(closestIndex);
}

int WaveformCanvas::yForScalarValue(int row, const QString& value) const
{
  const int top    = yForSignalRow(row) + 5;
  const int bottom = yForSignalRow(row) + rowHeight() - 6;
  const int mid    = std::midpoint(top, bottom);

  if (value == "1")
    return top;
  if (value == "0")
    return bottom;
  return mid;
}

void WaveformCanvas::drawScalar(QPainter& painter, int row, int x0, int x1,
                                const QString& value) const
{
  painter.setPen(QPen(colorForTraceValue(value), 2));
  painter.drawLine(x0, yForScalarValue(row, value), x1, yForScalarValue(row, value));
}

void WaveformCanvas::drawScalarTransition(QPainter& painter, int row, int x,
                                          const QString& previousValue,
                                          const QString& nextValue) const
{
  if (previousValue == nextValue)
    return;

  painter.setPen(QPen(colorForTraceValue(previousValue), 2));
  painter.drawLine(x, yForScalarValue(row, previousValue), x,
                   yForScalarValue(row, nextValue));
}

void WaveformCanvas::drawBus(QPainter& painter, int row, int x0, int x1,
                             const QString& value) const
{
  const int top    = yForSignalRow(row) + 5;
  const int bottom = yForSignalRow(row) + rowHeight() - 6;
  const int mid    = std::midpoint(top, bottom);
  const int offset = std::min(5, (x1 - x0) / 2);

  painter.setPen(QPen(colorForTraceValue(value), 2));
  painter.drawLine(x0, mid, x0 + offset, top);
  painter.drawLine(x0, mid, x0 + offset, bottom);
  painter.drawLine(x0 + offset, top, x1 - offset, top);
  painter.drawLine(x0 + offset, bottom, x1 - offset, bottom);
  painter.drawLine(x1 - offset, top, x1, mid);
  painter.drawLine(x1 - offset, bottom, x1, mid);

  if (x1 - x0 > 28) {
    painter.setPen(palette().text().color());
    painter.drawText(QRect(x0 + 7, top, x1 - x0 - 14, bottom - top), Qt::AlignCenter,
                     value);
  }
}

WaveformViewer::WaveformViewer(QWidget* parent) : QWidget(parent)
{
  const auto root = new QVBoxLayout(this);
  root->setContentsMargins(4, 4, 4, 4);

  const auto fileToolBar = new QToolBar(this);
  fileToolBar->setFloatable(false);
  fileToolBar->setMovable(false);
  fileToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

  newAct  = new QAction(Icon("file"), tr("&New"), this);
  openAct = new QAction(Icon("open"), tr("&Open..."), this);
  saveAct = new QAction(Icon("save"), tr("&Save"), this);

  fileToolBar->addAction(newAct);
  fileToolBar->addAction(openAct);
  fileToolBar->addAction(saveAct);
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
  canvas          = new WaveformCanvas();
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

  const auto zoomToolBar = new QToolBar(this);
  zoomToolBar->setFloatable(false);
  zoomToolBar->setMovable(false);
  zoomToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

  zoomOutAct = new QAction(Icon("minus"), tr("Zoom Out"), this);
  zoomInAct  = new QAction(Icon("plus"), tr("Zoom In"), this);

  zoomToolBar->addAction(zoomOutAct);
  zoomToolBar->addAction(zoomInAct);
  root->addWidget(zoomToolBar);

  refreshTimer = new QTimer(this);
  refreshTimer->setSingleShot(true);
  // Limit relayout and repaint work to roughly one frame when snapshots arrive rapidly.
  refreshTimer->setInterval(16);
  connect(refreshTimer, &QTimer::timeout, this, [this]() {
    refreshSignalList();
    refreshCanvas();
    if (keepScrolledToEnd)
      scrollArea->horizontalScrollBar()->setValue(
          scrollArea->horizontalScrollBar()->maximum());
    keepScrolledToEnd = false;
  });

  connect(saveAct, &QAction::triggered, this, &WaveformViewer::saveTrace);
  connect(zoomInAct, &QAction::triggered, this, [this]() {
    pixelsPerTick *= 1.25;
    refreshCanvas();
  });
  connect(zoomOutAct, &QAction::triggered, this, [this]() {
    pixelsPerTick /= 1.25;
    refreshCanvas();
  });
  connect(scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this,
          [this](int value) {
            if (syncingScrollBars)
              return;

            syncingScrollBars = true;
            labelScrollArea->verticalScrollBar()->setValue(value);
            syncingScrollBars = false;
          });
  connect(labelScrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this,
          [this](int value) {
            if (syncingScrollBars)
              return;

            syncingScrollBars = true;
            scrollArea->verticalScrollBar()->setValue(value);
            syncingScrollBars = false;
          });
  connect(canvas, &WaveformCanvas::timestampHovered, this, [this](int sampleIndex) {
    selectedSampleIndex = sampleIndex;
    refreshSignalList();
    refreshCanvas();
  });
}

void WaveformViewer::resetTrace(const QStringList& signalNames, int inputCount)
{
  names               = signalNames;
  inputSignalCount    = std::clamp<qsizetype>(inputCount, 0, names.size());
  selectedSampleIndex = -1;
  samples.clear();
  signalList->setTrace(names, inputSignalCount);
  refreshSignalList();
  refreshCanvas();
}

void WaveformViewer::appendSnapshot(quint64 time, const QStringList& values)
{
  appendSnapshots({{time, values}});
}

void WaveformViewer::appendSnapshots(
    const QList<QPair<qulonglong, QStringList>>& snapshots)
{
  if (names.empty())
    return;

  keepScrolledToEnd = keepScrolledToEnd
                      || scrollArea->horizontalScrollBar()->value()
                             == scrollArea->horizontalScrollBar()->maximum();

  samples.reserve(samples.size() + static_cast<std::size_t>(snapshots.size()));
  for (const auto& [time, values] : snapshots) {
    // Trace setup and propagation may report the same timestamp more than once; the
    // latest complete value set is authoritative.
    if (!samples.empty() && samples.back().time == time) {
      samples.back().values = values;
    } else {
      samples.push_back({time, values});
    }
  }

  if (selectedSampleIndex >= static_cast<int>(samples.size()))
    selectedSampleIndex = -1;

  scheduleRefresh();
}

void WaveformViewer::clearTrace()
{
  samples.clear();
  selectedSampleIndex = -1;
  refreshSignalList();
  refreshCanvas();
}

void WaveformViewer::refreshSignalList()
{
  const int scrollValue = labelScrollArea->verticalScrollBar()->value();
  signalList->setValues(displayedValues());
  labelScrollArea->verticalScrollBar()->setValue(scrollValue);
}

void WaveformViewer::refreshCanvas()
{
  canvas->setPixelsPerTick(pixelsPerTick);
  canvas->setTrace(names, samples, inputSignalCount);
  canvas->setSelectedSampleIndex(selectedSampleIndex);
}

void WaveformViewer::scheduleRefresh()
{
  // A running single-shot timer represents an already-scheduled frame and absorbs any
  // additional snapshots received before it fires.
  if (!refreshTimer->isActive())
    refreshTimer->start();
}

QStringList WaveformViewer::displayedValues() const
{
  if (selectedSampleIndex >= 0 && selectedSampleIndex < static_cast<int>(samples.size()))
    return samples[selectedSampleIndex].values;
  if (!samples.empty())
    return samples.back().values;
  return {};
}

void WaveformViewer::saveTrace()
{
  const QString fileName = QFileDialog::getSaveFileName(
      this, tr("Save Waveform"), QString(), tr("FST Trace (*.fst);;All Files (*)"));
  if (fileName.isEmpty())
    return;

  try {
    writeFstTrace(fileName.toStdString());
  } catch (const std::exception& e) {
    QMessageBox::warning(this, tr("Save Waveform"),
                         tr("Cannot write FST trace:\n%1").arg(e.what()));
  }
}

void WaveformViewer::writeFstTrace(std::string_view fileName) const
{
  if (names.empty())
    throw std::runtime_error("no waveform signals are available");

  std::vector<std::size_t>                 widths;
  std::vector<FstTraceWriter::TraceSignal> traceSignals;
  widths.reserve(names.size());
  traceSignals.reserve(names.size());

  for (const auto& [i, name] : names | silicon::views::enumerate) {
    std::size_t width = 1;
    for (const auto& sample : samples) {
      if (i < sample.values.size())
        width = std::max(width, std::size_t(sample.values[i].size()));
    }
    widths.push_back(width);
    traceSignals.push_back({name.toStdString(), width});
  }

  FstTraceWriter writer(fileName, traceSignals, {.topScopeName = "Waveform"});

  for (const auto& sample : samples) {
    std::vector<std::string> values;
    values.reserve(sample.values.size());
    for (const auto& value : sample.values)
      values.push_back(value.toStdString());

    writer.emitSnapshot(sample.time, values);
  }

  writer.flush();
}
