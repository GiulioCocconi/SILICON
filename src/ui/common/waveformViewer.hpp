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
#include <core/siliconWaveform.hpp>
#include <utils/num_formatting.hpp>
#include <vector>

class QAction;
class QLineEdit;
class QMouseEvent;


namespace SILICON::ui::waveform {
using namespace SILICON::waveform;

class SignalListWidget : public QWidget {
  Q_OBJECT
public:
  explicit SignalListWidget(QWidget* parent = nullptr);

  void setTrace(const QStringList& signalNames, int inputCount);
  void setValues(const QStringList& signalValues);
  void setSelectedSignalIndex(int signalIndex);

signals:
  void signalSelected(int signalIndex);
  void signalContextMenuRequested(int signalIndex, QPoint globalPosition);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

private:
  QStringList names;
  QStringList values;
  int         inputSignalCount    = 0;
  int         selectedSignalIndex = -1;

  [[nodiscard]] int signalAreaHeight() const;
  [[nodiscard]] int valueColumnX() const;
  [[nodiscard]] int yForSignalRow(int row) const;
  [[nodiscard]] int signalRowAt(QPoint position) const;
};

class Canvas : public QWidget {
  Q_OBJECT
public:
  /** @brief One complete set of signal values at a simulation timestamp. */
  using Sample = SILICON::waveform::Sample;

  explicit Canvas(QWidget* parent = nullptr);

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

  /** @brief Sets per-signal display formats used for bus labels. */
  void setSignalFormats(const std::vector<SILICON::core::BusValueFormat>& formats);

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

  /**
   * @brief Highlights one signal row, or clears selection for an invalid index.
   * @param signalIndex Signal row to highlight
   */
  void setSelectedSignalIndex(int signalIndex);

  /**
   * @brief Enables or disables waveform interval editing gestures.
   * @param enabled True to let left-button drags choose an input interval
   */
  void setEditMode(bool enabled);

  /**
   * @brief Sets the editable waveform time span.
   * @param duration Inclusive upper bound for edit gestures and rendering
   */
  void setEditDuration(quint64 duration);

  /**
   * @brief Sets the edit interval highlighted on the canvas.
   * @param signalIndex Signal row whose interval is selected
   * @param startTime Inclusive interval start
   * @param endTime Exclusive interval end
   */
  void setEditSelection(int signalIndex, quint64 startTime, quint64 endTime);

  /**
   * @brief Controls whether the precise edit interval highlight is visible.
   * @param visible True while precise interval fields are being edited
   */
  void setEditSelectionVisible(bool visible);

signals:
  /**
   * @brief Emitted when the pointer is nearest to a trace timestamp.
   * @param sampleIndex Index of the nearest sample
   */
  void timestampHovered(int sampleIndex);

  /**
   * @brief Emitted when a signal row is selected.
   * @param signalIndex Selected signal row
   */
  void signalSelected(int signalIndex);

  /**
   * @brief Requests the display-format context menu for a signal row.
   * @param signalIndex Signal row under the pointer
   * @param globalPosition Screen position where the menu should open
   */
  void signalContextMenuRequested(int signalIndex, QPoint globalPosition);

  /**
   * @brief Emitted while an edit-mode interval is being adjusted.
   * @param signalIndex Selected signal row
   * @param startTime Inclusive interval start
   * @param endTime Exclusive interval end
   */
  void editIntervalChanged(int signalIndex, quint64 startTime, quint64 endTime);

  /**
   * @brief Emitted after an edit-mode drag chooses a signal/time interval.
   * @param signalIndex Selected signal row
   * @param startTime Inclusive interval start
   * @param endTime Exclusive interval end
   */
  void editIntervalSelected(int signalIndex, quint64 startTime, quint64 endTime);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

private:
  /** @brief Signal labels rendered by the canvas. */
  QStringList signalNames;

  /**
   * @brief Non-owning view of Viewer's sample storage.
   *
   * The viewer owns the canvas and its sample vector, so this remains valid for the
   * canvas lifetime and avoids copying the complete trace on every refresh.
   */
  const std::vector<Sample>*         traceSamples             = nullptr;
  double                             pixelsPerTick            = 12.0;
  int                                inputSignalCount         = 0;
  int                                selectedSampleIndex      = -1;
  int                                selectedSignalIndex      = -1;
  bool                               editMode                 = false;
  quint64                            editDuration             = 20;
  int                                editSelectionSignalIndex = -1;
  quint64                            editSelectionStartTime   = 0;
  quint64                            editSelectionEndTime     = 1;
  bool                               editSelectionVisible     = false;
  int                                editDragSignalIndex      = -1;
  quint64                            editDragStartTime        = 0;
  quint64                            editDragEndTime          = 0;
  std::vector<SILICON::core::BusValueFormat> signalFormats;

  [[nodiscard]] int     rowHeight() const { return 28; }
  [[nodiscard]] int     rulerHeight() const { return 24; }
  [[nodiscard]] int     groupHeaderHeight() const { return 22; }
  [[nodiscard]] quint64 endTime() const;
  [[nodiscard]] int     xForTime(quint64 time) const;
  /** @brief Converts an x-coordinate into a clamped simulation timestamp. */
  [[nodiscard]] quint64 timeForX(int x) const;
  [[nodiscard]] int     groupHeaderCount() const;
  [[nodiscard]] int     groupHeaderCountBeforeSignal(int row) const;
  [[nodiscard]] int     yForSignalRow(int row) const;
  [[nodiscard]] int     signalRowAt(QPoint position) const;
  [[nodiscard]] int     yForScalarValue(int row, const core::BusValue& value) const;
  [[nodiscard]] SILICON::core::BusValueFormat valueFormatForSignal(int row) const;
  /** @brief Recomputes the scrollable canvas dimensions from trace extent and zoom. */
  void updateCanvasSize();

  void drawScalar(QPainter& painter, int row, int x0, int x1,
                  const core::BusValue& value) const;
  void drawScalarTransition(QPainter& painter, int row, int x,
                            const core::BusValue& previousValue,
                            const core::BusValue& nextValue) const;
  void drawBus(QPainter& painter, int row, int x0, int x1,
               const core::BusValue& value) const;
};

class Viewer : public QWidget {
  Q_OBJECT
public:
  explicit Viewer(QWidget* parent = nullptr);

protected:
  /** @brief Tracks focus changes for precise interval edit fields. */
  bool eventFilter(QObject* watched, QEvent* event) override;

public slots:
  /**
   * @brief Replaces signal metadata and clears the current trace.
   * @param signalNames Ordered signal labels
   * @param inputCount Number of leading signals belonging to the input group
   * @param signalWidths Width of each signal in bits
   */
  void resetTrace(const QStringList& signalNames, int inputCount,
                  const QList<int>& signalWidths);

  /**
   * @brief Appends or replaces one timestamped snapshot.
   * @param time Simulation timestamp
   * @param values Signal values ordered like the configured names
   */
  void appendSnapshot(quint64 time, const std::vector<core::BusValue>& values);

  /**
   * @brief Appends a batch of snapshots with a single deferred UI refresh.
   * @param snapshots Ordered timestamp and signal-value snapshots
   */
  void appendSnapshots(const QList<QPair<qulonglong, std::vector<core::BusValue>>>& snapshots);

  /** @brief Clears all recorded waveform samples. */
  void clearTrace();

  /**
   * @brief Enables input-waveform editing mode.
   *
   * Edit mode drops output signals from the visible trace, clears transition history,
   * and keeps one editable input waveform spanning the configured duration.
   */
  void setEditMode(bool enabled);

signals:
  /**
   * @brief Emitted when waveform edit mode changes.
   * @param enabled True while input waveform editing is active
   */
  void editModeChanged(bool enabled);

  /**
   * @brief Emitted when edit mode is left and the edited input waveform should run.
   * @param duration Total edited waveform duration
   * @param inputSnapshots Edited input-only snapshots
   */
  void editTraceCommitted(qulonglong                         duration,
                          std::vector<Sample> inputSnapshots);

private:
  SignalListWidget* signalList;
  Canvas*   canvas;
  QScrollArea*      labelScrollArea;
  QScrollArea*      scrollArea;
  QAction*          newAct;
  QAction*          openAct;
  QAction*          saveAct;
  QAction*          editAct;
  QAction*          zoomInAct;
  QAction*          zoomOutAct;
  QLineEdit*        durationEdit;
  QLineEdit*        startEdit;
  QLineEdit*        endEdit;

  Trace               trace;
  int                                selectedSampleIndex = -1;
  int                                selectedSignalIndex = -1;
  double                             pixelsPerTick       = 12.0;
  std::vector<SILICON::core::BusValueFormat> signalFormats;
  bool                               syncingScrollBars      = false;
  bool                               editMode               = false;
  quint64                            editDuration           = 20;
  quint64                            selectedEditStart      = 0;
  quint64                            selectedEditEnd        = 1;
  bool                               preciseIntervalEditing = false;
  /** @brief Preserves tail-following behavior across a deferred refresh. */
  bool keepScrolledToEnd = false;

  /** @brief Coalesces rapid snapshot arrivals into at most one refresh per frame. */
  QTimer* refreshTimer = nullptr;

  /** @brief Repaints the signal list while preserving scroll position. */
  void refreshSignalList();

  /** @brief Pushes current trace state into the waveform canvas. */
  void refreshCanvas();

  /** @brief Replaces trace samples with an input-only editable default waveform. */
  void rebuildEditTrace();

  /**
   * @brief Applies a raw bit value to one input signal over a time interval.
   * @param signalIndex Input signal index
   * @param startTime Inclusive interval start
   * @param endTime Exclusive interval end
   * @param rawValue Raw bit-string value to assign
   */
  void applyEditInterval(int signalIndex, quint64 startTime, quint64 endTime,
                         const QString& rawValue);

  /**
   * @brief Prompts for and validates a value for an edit-mode interval.
   * @param signalIndex Input signal index
   * @param startTime Inclusive interval start
   * @param endTime Exclusive interval end
   */
  void promptEditIntervalValue(int signalIndex, quint64 startTime, quint64 endTime);

  /** @brief Prompts for a value using the current selected signal and text interval. */
  void promptSelectedEditIntervalValue();

  /**
   * @brief Updates the precise interval fields from an edit gesture.
   * @param startTime Inclusive interval start
   * @param endTime Exclusive interval end
   */
  void setEditIntervalFields(quint64 startTime, quint64 endTime);

  /** @brief Parses and clamps the precise interval text fields. */
  void commitEditIntervalFields();

  /** @brief Synchronizes the duration field with edit mode or the latest trace time. */
  void updateDurationField();

  /** @brief Converts the current editable samples into signal payload form. */
  [[nodiscard]] std::vector<Sample> editedInputSnapshots() const;

  /** @brief Synchronizes edit controls with the current mode and duration. */
  void updateEditControls();
  /** @brief Schedules a coalesced waveform refresh if one is not already pending. */
  void scheduleRefresh();
  /** @brief Returns the signal names currently visible in the viewer. */
  [[nodiscard]] QStringList visibleNames() const;
  [[nodiscard]] QStringList displayedValues() const;
  [[nodiscard]] std::size_t signalWidth(int signalIndex) const;
  [[nodiscard]] QString     displayValue(int signalIndex, const core::BusValue& value) const;
  void                      setSelectedSignalIndex(int signalIndex);
  void                      showSignalFormatMenu(int signalIndex, QPoint globalPosition);
  void                      saveTrace();
};

}  // namespace SILICON::ui::waveform
