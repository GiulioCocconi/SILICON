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

#pragma once

#include <QScrollArea>
#include <QSplitter>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <string_view>
#include <vector>

class QAction;
class QMouseEvent;

class SignalListWidget : public QWidget {
  Q_OBJECT
public:
  explicit SignalListWidget(QWidget* parent = nullptr);

  void setTrace(const QStringList& signalNames, int inputCount);
  void setValues(const QStringList& signalValues);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QStringList names;
  QStringList values;
  int         inputSignalCount = 0;

  [[nodiscard]] int signalAreaHeight() const;
  [[nodiscard]] int valueColumnX() const;
  [[nodiscard]] int yForSignalRow(int row) const;
};

class WaveformCanvas : public QWidget {
  Q_OBJECT
public:
  /** @brief One complete set of signal values at a simulation timestamp. */
  struct Sample {
    quint64     time;   /**< Simulation timestamp */
    QStringList values; /**< Encoded values ordered like the signal-name list */
  };

  explicit WaveformCanvas(QWidget* parent = nullptr);

  /**
   * @brief Displays a trace using the existing input-group count.
   * @param names Ordered signal labels
   * @param samples Viewer-owned sample storage observed without copying
   */
  void setTrace(const QStringList& names, const std::vector<Sample>& samples);

  /**
   * @brief Displays a trace and updates signal grouping.
   * @param names Ordered signal labels
   * @param samples Viewer-owned sample storage observed without copying
   * @param inputCount Number of leading signals belonging to the input group
   */
  void setTrace(const QStringList& names, const std::vector<Sample>& samples,
                int inputCount);

  /**
   * @brief Changes horizontal waveform scale.
   * @param value Pixels rendered per simulation tick
   */
  void setPixelsPerTick(double value);

  /**
   * @brief Highlights one sample timestamp, or clears selection for an invalid index.
   * @param sampleIndex Sample index to highlight
   */
  void setSelectedSampleIndex(int sampleIndex);

signals:
  /**
   * @brief Emitted when the pointer is nearest to a trace timestamp.
   * @param sampleIndex Index of the nearest sample
   */
  void timestampHovered(int sampleIndex);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

private:
  /** @brief Signal labels rendered by the canvas. */
  QStringList signalNames;

  /**
   * @brief Non-owning view of WaveformViewer's sample storage.
   *
   * The viewer owns the canvas and its sample vector, so this remains valid for the
   * canvas lifetime and avoids copying the complete trace on every refresh.
   */
  const std::vector<Sample>* traceSamples        = nullptr;
  double                     pixelsPerTick       = 12.0;
  int                        inputSignalCount    = 0;
  int                        selectedSampleIndex = -1;

  [[nodiscard]] int     rowHeight() const { return 28; }
  [[nodiscard]] int     rulerHeight() const { return 24; }
  [[nodiscard]] int     groupHeaderHeight() const { return 22; }
  [[nodiscard]] quint64 endTime() const;
  [[nodiscard]] int     xForTime(quint64 time) const;
  [[nodiscard]] int     groupHeaderCount() const;
  [[nodiscard]] int     groupHeaderCountBeforeSignal(int row) const;
  [[nodiscard]] int     yForSignalRow(int row) const;
  [[nodiscard]] int     yForScalarValue(int row, const QString& value) const;
  /** @brief Recomputes the scrollable canvas dimensions from trace extent and zoom. */
  void updateCanvasSize();

  void drawScalar(QPainter& painter, int row, int x0, int x1, const QString& value) const;
  void drawScalarTransition(QPainter& painter, int row, int x,
                            const QString& previousValue, const QString& nextValue) const;
  void drawBus(QPainter& painter, int row, int x0, int x1, const QString& value) const;
};

class WaveformViewer : public QWidget {
  Q_OBJECT
public:
  explicit WaveformViewer(QWidget* parent = nullptr);

public slots:
  /**
   * @brief Replaces signal metadata and clears the current trace.
   * @param signalNames Ordered signal labels
   * @param inputCount Number of leading signals belonging to the input group
   */
  void resetTrace(const QStringList& signalNames, int inputCount);

  /**
   * @brief Appends or replaces one timestamped snapshot.
   * @param time Simulation timestamp
   * @param values Signal values ordered like the configured names
   */
  void appendSnapshot(quint64 time, const QStringList& values);

  /**
   * @brief Appends a batch of snapshots with a single deferred UI refresh.
   * @param snapshots Ordered timestamp and signal-value snapshots
   */
  void appendSnapshots(const QList<QPair<qulonglong, QStringList>>& snapshots);

  /** @brief Clears all recorded waveform samples. */
  void clearTrace();

private:
  SignalListWidget* signalList;
  WaveformCanvas*   canvas;
  QScrollArea*      labelScrollArea;
  QScrollArea*      scrollArea;
  QAction*          newAct;
  QAction*          openAct;
  QAction*          saveAct;
  QAction*          zoomInAct;
  QAction*          zoomOutAct;

  QStringList                         names;
  std::vector<WaveformCanvas::Sample> samples;
  int                                 inputSignalCount    = 0;
  int                                 selectedSampleIndex = -1;
  double                              pixelsPerTick       = 12.0;
  bool                                syncingScrollBars   = false;
  /** @brief Preserves tail-following behavior across a deferred refresh. */
  bool keepScrolledToEnd = false;

  /** @brief Coalesces rapid snapshot arrivals into at most one refresh per frame. */
  QTimer* refreshTimer = nullptr;

  void refreshSignalList();
  void refreshCanvas();
  /** @brief Schedules a coalesced waveform refresh if one is not already pending. */
  void                      scheduleRefresh();
  [[nodiscard]] QStringList displayedValues() const;
  void                      saveTrace();
  void                      writeFstTrace(std::string_view fileName) const;
};
