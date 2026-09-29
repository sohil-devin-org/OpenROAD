// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <QCheckBox>
#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>
#include <QtCharts>
#include <string>

#include "gui/gui.h"
#include "thm/PhysicsState.h"
#include "thm/Thermal.h"

namespace utl {
class Logger;
}

namespace gui {

// Dock widget showing the metrics of the last physics (thermal) analysis,
// the recorded checkpoint history as a table and a peak-T / derated-WNS plot.
// Selecting a history row displays that checkpoint's temperature map in the
// Temperature heat map instead of the live grid.
class PhysicsWidget : public QDockWidget, public thm::PhysicsObserver
{
  Q_OBJECT

 public:
  PhysicsWidget(QWidget* parent = nullptr);
  ~PhysicsWidget() override;

  void setThermal(thm::Thermal* thermal);
  void setLogger(utl::Logger* logger);

  // thm::PhysicsObserver
  void onAnalysisBegin(const std::string& label) override;
  void onLoopIteration(const thm::PhysicsMetrics& metrics) override;
  void onSnapshot(const thm::PhysicsSnapshot& snapshot,
                  const thm::PhysicsHistory& history) override;
  void onHistoryCleared() override;
  void onReferenceLoaded(const thm::PhysicsHistory& reference) override;

 private slots:
  void historySelectionChanged();
  void showLiveMap();
  void refresh();

 private:
  static constexpr const char* kTemperatureControl = "Heat Maps/Temperature";

  void updateMetrics(const thm::PhysicsMetrics& metrics, const QString& state);
  void rebuildHistory(const thm::PhysicsHistory& history);
  void appendHistoryRow(int index, const thm::PhysicsSnapshot& snapshot);
  void rebuildChart(const thm::PhysicsHistory& history);
  void setReference(const thm::PhysicsHistory* reference);
  void showTemperatureMap();

  thm::Thermal* thermal_ = nullptr;
  utl::Logger* logger_ = nullptr;
  bool updating_ = false;

  QLabel* status_;
  QTableWidget* metrics_table_;
  QTableWidget* history_table_;
  QPushButton* live_button_;
  QPushButton* refresh_button_;
  QCheckBox* show_map_;

  QChart* chart_;
  QChartView* chart_view_;
  QLineSeries* peak_series_;
  QLineSeries* wns_series_;
  QLineSeries* reference_peak_series_;
  QValueAxis* x_axis_;
  QValueAxis* peak_axis_;
  QValueAxis* wns_axis_;
};

}  // namespace gui
