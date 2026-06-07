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
  struct Sample {
    quint64     time;
    QStringList values;
  };

  explicit WaveformCanvas(QWidget* parent = nullptr);

  void setTrace(const QStringList& names, const std::vector<Sample>& samples);
  void setTrace(const QStringList& names, const std::vector<Sample>& samples,
                int inputCount);
  void setPixelsPerTick(double value);
  void setSelectedSampleIndex(int sampleIndex);

signals:
  void timestampHovered(int sampleIndex);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

private:
  QStringList         signalNames;
  std::vector<Sample> traceSamples;
  double              pixelsPerTick       = 12.0;
  int                 inputSignalCount    = 0;
  int                 selectedSampleIndex = -1;

  [[nodiscard]] int     rowHeight() const { return 28; }
  [[nodiscard]] int     rulerHeight() const { return 24; }
  [[nodiscard]] int     groupHeaderHeight() const { return 22; }
  [[nodiscard]] quint64 endTime() const;
  [[nodiscard]] int     xForTime(quint64 time) const;
  [[nodiscard]] int     groupHeaderCount() const;
  [[nodiscard]] int     groupHeaderCountBeforeSignal(int row) const;
  [[nodiscard]] int     yForSignalRow(int row) const;
  [[nodiscard]] int     yForScalarValue(int row, const QString& value) const;

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
  void resetTrace(const QStringList& signalNames, int inputCount);
  void appendSnapshot(quint64 time, const QStringList& values);
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

  void                      refreshSignalList();
  void                      refreshCanvas();
  [[nodiscard]] QStringList displayedValues() const;
  void                      saveTrace();
  void                      writeFstTrace(std::string_view fileName) const;
};
