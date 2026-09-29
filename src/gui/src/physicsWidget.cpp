// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "physicsWidget.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "utl/Logger.h"
#include "web/core.h"

namespace gui {

namespace {

QString number(double value, int precision = 3)
{
  return QString::number(value, 'g', precision);
}

QString fixed(double value, int decimals = 2)
{
  return QString::number(value, 'f', decimals);
}

QString nanoseconds(double seconds)
{
  return fixed(seconds * 1e9, 3);
}

QTableWidgetItem* readOnlyItem(const QString& text)
{
  auto* item = new QTableWidgetItem(text);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  return item;
}

QTableWidgetItem* numericItem(double value, const QString& text)
{
  QTableWidgetItem* item = readOnlyItem(text);
  item->setData(Qt::UserRole, value);
  item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  return item;
}

}  // namespace

PhysicsWidget::PhysicsWidget(QWidget* parent)
    : QDockWidget("Physics", parent),
      status_(new QLabel(this)),
      metrics_table_(new QTableWidget(0, 2, this)),
      history_table_(new QTableWidget(0, 9, this)),
      live_button_(new QPushButton("Live map", this)),
      refresh_button_(new QPushButton("Refresh", this)),
      show_map_(new QCheckBox("Show temperature heat map", this)),
      chart_(new QChart),
      chart_view_(new QChartView(chart_, this)),
      peak_series_(new QLineSeries(chart_)),
      wns_series_(new QLineSeries(chart_)),
      reference_peak_series_(new QLineSeries(chart_)),
      x_axis_(new QValueAxis(chart_)),
      peak_axis_(new QValueAxis(chart_)),
      wns_axis_(new QValueAxis(chart_))
{
  setObjectName("physics_widget");  // for settings

  status_->setText("No physics analysis has been run (analyze_thermal).");
  status_->setWordWrap(true);

  metrics_table_->setHorizontalHeaderLabels({"Metric", "Value"});
  metrics_table_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  metrics_table_->horizontalHeader()->setStretchLastSection(true);
  metrics_table_->verticalHeader()->hide();
  metrics_table_->setSelectionMode(QAbstractItemView::NoSelection);
  metrics_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

  history_table_->setHorizontalHeaderLabels({"#",
                                             "Label",
                                             "Time (s)",
                                             "Peak T (C)",
                                             "Avg T (C)",
                                             "WNS nom (ns)",
                                             "WNS derated (ns)",
                                             "Power (W)",
                                             "HPWL (um)"});
  history_table_->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  history_table_->horizontalHeader()->setStretchLastSection(true);
  history_table_->verticalHeader()->hide();
  history_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  history_table_->setSelectionMode(QAbstractItemView::SingleSelection);
  history_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  history_table_->setToolTip(
      "Select a checkpoint to display its temperature map in the "
      "Temperature heat map.");

  show_map_->setChecked(true);
  live_button_->setToolTip(
      "Display the live temperature grid instead of a history checkpoint.");
  refresh_button_->setToolTip("Reload the metrics and history from thm.");

  peak_series_->setName("Peak T (C)");
  wns_series_->setName("Derated WNS (ns)");
  reference_peak_series_->setName("Reference peak T (C)");
  QPen reference_pen(peak_series_->pen());
  reference_pen.setStyle(Qt::DashLine);
  reference_peak_series_->setPen(reference_pen);
  reference_peak_series_->setVisible(false);

  chart_->addSeries(peak_series_);
  chart_->addSeries(wns_series_);
  chart_->addSeries(reference_peak_series_);
  x_axis_->setTitleText("Checkpoint");
  x_axis_->setLabelFormat("%d");
  x_axis_->setTickType(QValueAxis::TicksDynamic);
  x_axis_->setTickInterval(1);
  peak_axis_->setTitleText("Peak T (C)");
  wns_axis_->setTitleText("Derated WNS (ns)");
  chart_->addAxis(x_axis_, Qt::AlignBottom);
  chart_->addAxis(peak_axis_, Qt::AlignLeft);
  chart_->addAxis(wns_axis_, Qt::AlignRight);
  peak_series_->attachAxis(x_axis_);
  peak_series_->attachAxis(peak_axis_);
  reference_peak_series_->attachAxis(x_axis_);
  reference_peak_series_->attachAxis(peak_axis_);
  wns_series_->attachAxis(x_axis_);
  wns_series_->attachAxis(wns_axis_);
  chart_->legend()->setAlignment(Qt::AlignBottom);
  chart_->setMargins(QMargins(4, 4, 4, 4));
  chart_view_->setRenderHint(QPainter::Antialiasing);
  chart_view_->setMinimumHeight(120);

  auto* buttons = new QHBoxLayout;
  buttons->addWidget(show_map_);
  buttons->addStretch();
  buttons->addWidget(live_button_);
  buttons->addWidget(refresh_button_);

  auto* history_box = new QWidget(this);
  auto* history_layout = new QVBoxLayout(history_box);
  history_layout->setContentsMargins(0, 0, 0, 0);
  history_layout->addWidget(new QLabel("Checkpoint history", history_box));
  history_layout->addWidget(history_table_);
  history_layout->addLayout(buttons);

  auto* splitter = new QSplitter(Qt::Vertical, this);
  splitter->addWidget(metrics_table_);
  splitter->addWidget(history_box);
  splitter->addWidget(chart_view_);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 3);
  splitter->setStretchFactor(2, 2);

  auto* container = new QWidget(this);
  auto* layout = new QVBoxLayout(container);
  layout->addWidget(status_);
  layout->addWidget(splitter);
  setWidget(container);

  connect(history_table_->selectionModel(),
          &QItemSelectionModel::selectionChanged,
          this,
          &PhysicsWidget::historySelectionChanged);
  connect(
      live_button_, &QPushButton::clicked, this, &PhysicsWidget::showLiveMap);
  connect(
      refresh_button_, &QPushButton::clicked, this, &PhysicsWidget::refresh);
}

PhysicsWidget::~PhysicsWidget()
{
  if (thermal_ != nullptr) {
    thermal_->removeObserver(this);
  }
}

void PhysicsWidget::setThermal(thm::Thermal* thermal)
{
  if (thermal_ == thermal) {
    return;
  }
  if (thermal_ != nullptr) {
    thermal_->removeObserver(this);
  }
  thermal_ = thermal;
  if (thermal_ != nullptr) {
    thermal_->addObserver(this);
  }
  refresh();
}

void PhysicsWidget::setLogger(utl::Logger* logger)
{
  logger_ = logger;
}

// ---------------------------------------------------------------------------
// observer callbacks

void PhysicsWidget::onAnalysisBegin(const std::string& label)
{
  status_->setText(QString("Running physics analysis '%1'...")
                       .arg(QString::fromStdString(label)));
  // The history grows; return to the live grid so the new result is shown.
  updating_ = true;
  history_table_->clearSelection();
  updating_ = false;
  if (thermal_ != nullptr && thermal_->displayedSnapshot() >= 0) {
    thermal_->setDisplayedSnapshot(-1);
  }
}

void PhysicsWidget::onLoopIteration(const thm::PhysicsMetrics& metrics)
{
  updateMetrics(metrics,
                QString("electrothermal loop iteration %1")
                    .arg(metrics.electrothermal_iterations));
}

void PhysicsWidget::onSnapshot(const thm::PhysicsSnapshot& snapshot,
                               const thm::PhysicsHistory& history)
{
  if (history_table_->rowCount() == history.size() - 1) {
    appendHistoryRow(history.size() - 1, snapshot);
  } else {
    rebuildHistory(history);
  }
  rebuildChart(history);
  updateMetrics(
      snapshot.metrics,
      QString("checkpoint %1 of %2").arg(history.size()).arg(history.size()));
}

void PhysicsWidget::onHistoryCleared()
{
  updating_ = true;
  history_table_->setRowCount(0);
  updating_ = false;
  peak_series_->clear();
  wns_series_->clear();
  status_->setText("Physics history cleared.");
}

void PhysicsWidget::onReferenceLoaded(const thm::PhysicsHistory& reference)
{
  setReference(&reference);
}

// ---------------------------------------------------------------------------
// slots

void PhysicsWidget::historySelectionChanged()
{
  if (updating_ || thermal_ == nullptr) {
    return;
  }
  const QModelIndexList rows = history_table_->selectionModel()->selectedRows();
  if (rows.empty()) {
    thermal_->setDisplayedSnapshot(-1);
    return;
  }
  const int index = rows.front().row();
  thermal_->setDisplayedSnapshot(index);
  if (index < thermal_->history().size()) {
    const thm::PhysicsSnapshot& snapshot = thermal_->history().at(index);
    updateMetrics(snapshot.metrics,
                  QString("history checkpoint %1 (heat map shows this "
                          "snapshot)")
                      .arg(index));
  }
  showTemperatureMap();
}

void PhysicsWidget::showLiveMap()
{
  updating_ = true;
  history_table_->clearSelection();
  updating_ = false;
  if (thermal_ == nullptr) {
    return;
  }
  thermal_->setDisplayedSnapshot(-1);
  if (thermal_->hasResults()) {
    updateMetrics(thermal_->lastMetrics(), "last analysis (live map)");
  }
  showTemperatureMap();
}

void PhysicsWidget::refresh()
{
  if (thermal_ == nullptr) {
    return;
  }
  rebuildHistory(thermal_->history());
  rebuildChart(thermal_->history());
  setReference(thermal_->reference());
  if (thermal_->hasResults()) {
    updateMetrics(thermal_->lastMetrics(), "last analysis");
  } else {
    metrics_table_->setRowCount(0);
    status_->setText("No physics analysis has been run (analyze_thermal).");
  }
  const int displayed = thermal_->displayedSnapshot();
  if (displayed >= 0 && displayed < history_table_->rowCount()) {
    updating_ = true;
    history_table_->selectRow(displayed);
    updating_ = false;
  }
}

// ---------------------------------------------------------------------------
// helpers

void PhysicsWidget::updateMetrics(const thm::PhysicsMetrics& metrics,
                                  const QString& state)
{
  std::vector<std::pair<QString, QString>> rows;
  const int dies
      = std::max(metrics.peak_temp_c.size(), metrics.avg_temp_c.size());
  for (int die = 0; die < dies; ++die) {
    const QString suffix = dies > 1 ? QString(" die %1").arg(die) : QString();
    if (die < static_cast<int>(metrics.peak_temp_c.size())) {
      rows.emplace_back("Peak temperature (C)" + suffix,
                        fixed(metrics.peak_temp_c[die]));
    }
    if (die < static_cast<int>(metrics.avg_temp_c.size())) {
      rows.emplace_back("Average temperature (C)" + suffix,
                        fixed(metrics.avg_temp_c[die]));
    }
  }
  rows.emplace_back("Max gradient (C/mm)",
                    fixed(metrics.max_gradient_c_per_mm));
  rows.emplace_back("Total power (W)", number(metrics.total_power_w, 4));
  rows.emplace_back("Leakage power (W)", number(metrics.leakage_power_w, 4));
  rows.emplace_back("Worst IR drop (V)", number(metrics.worst_ir_drop_v, 4));
  rows.emplace_back("WNS nominal / derated (ns)",
                    nanoseconds(metrics.wns_nominal_s) + " / "
                        + nanoseconds(metrics.wns_derated_s));
  rows.emplace_back("TNS nominal / derated (ns)",
                    nanoseconds(metrics.tns_nominal_s) + " / "
                        + nanoseconds(metrics.tns_derated_s));
  rows.emplace_back("Electrothermal iterations",
                    QString::number(metrics.electrothermal_iterations));
  QString convergence = metrics.converged ? "converged" : "not converged";
  if (metrics.runaway) {
    convergence = "thermal runaway";
  }
  rows.emplace_back("Loop status", convergence);
  rows.emplace_back("HPWL (um)", fixed(metrics.hpwl_um, 1));
  rows.emplace_back("Density overflow", number(metrics.overflow));
  rows.emplace_back("Physics weight", number(metrics.physics_weight));
  rows.emplace_back("EM lifetime factor", number(metrics.em_lifetime_factor));
  rows.emplace_back("Clock skew delta (ps)",
                    fixed(metrics.clock_skew_delta_s * 1e12, 2));

  metrics_table_->setRowCount(static_cast<int>(rows.size()));
  for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
    metrics_table_->setItem(row, 0, readOnlyItem(rows[row].first));
    metrics_table_->setItem(row, 1, readOnlyItem(rows[row].second));
  }

  QString label = QString::fromStdString(metrics.label);
  if (label.isEmpty()) {
    label = "analysis";
  }
  status_->setText(QString("%1 (iteration %2): %3")
                       .arg(label)
                       .arg(metrics.iteration)
                       .arg(state));
}

void PhysicsWidget::rebuildHistory(const thm::PhysicsHistory& history)
{
  updating_ = true;
  history_table_->setRowCount(0);
  for (int i = 0; i < history.size(); ++i) {
    appendHistoryRow(i, history.at(i));
  }
  updating_ = false;
}

void PhysicsWidget::appendHistoryRow(int index,
                                     const thm::PhysicsSnapshot& snapshot)
{
  const thm::PhysicsMetrics& m = snapshot.metrics;
  const bool was_updating = updating_;
  updating_ = true;
  const int row = history_table_->rowCount();
  history_table_->insertRow(row);
  history_table_->setItem(row, 0, numericItem(index, QString::number(index)));
  QString label = QString::fromStdString(m.label);
  if (!snapshot.tag.empty()) {
    label += QString(" [%1]").arg(QString::fromStdString(snapshot.tag));
  }
  history_table_->setItem(row, 1, readOnlyItem(label));
  history_table_->setItem(
      row, 2, numericItem(snapshot.time_s, number(snapshot.time_s, 4)));
  history_table_->setItem(
      row, 3, numericItem(m.peakTemp(), fixed(m.peakTemp())));
  double avg = 0.0;
  if (!m.avg_temp_c.empty()) {
    avg = *std::max_element(m.avg_temp_c.begin(), m.avg_temp_c.end());
  }
  history_table_->setItem(row, 4, numericItem(avg, fixed(avg)));
  history_table_->setItem(
      row, 5, numericItem(m.wns_nominal_s, nanoseconds(m.wns_nominal_s)));
  history_table_->setItem(
      row, 6, numericItem(m.wns_derated_s, nanoseconds(m.wns_derated_s)));
  history_table_->setItem(
      row, 7, numericItem(m.total_power_w, number(m.total_power_w, 4)));
  history_table_->setItem(row, 8, numericItem(m.hpwl_um, fixed(m.hpwl_um, 1)));
  history_table_->scrollToBottom();
  updating_ = was_updating;
}

void PhysicsWidget::rebuildChart(const thm::PhysicsHistory& history)
{
  peak_series_->clear();
  wns_series_->clear();
  double peak_min = std::numeric_limits<double>::max();
  double peak_max = std::numeric_limits<double>::lowest();
  double wns_min = std::numeric_limits<double>::max();
  double wns_max = std::numeric_limits<double>::lowest();
  for (int i = 0; i < history.size(); ++i) {
    const thm::PhysicsMetrics& m = history.at(i).metrics;
    const double peak = m.peakTemp();
    const double wns_ns = m.wns_derated_s * 1e9;
    peak_series_->append(i, peak);
    wns_series_->append(i, wns_ns);
    peak_min = std::min(peak_min, peak);
    peak_max = std::max(peak_max, peak);
    wns_min = std::min(wns_min, wns_ns);
    wns_max = std::max(wns_max, wns_ns);
  }
  if (reference_peak_series_->isVisible()) {
    for (const QPointF& point : reference_peak_series_->points()) {
      peak_min = std::min(peak_min, point.y());
      peak_max = std::max(peak_max, point.y());
    }
  }
  if (history.empty()) {
    return;
  }
  x_axis_->setRange(0, std::max(1, history.size() - 1));
  const double peak_pad = std::max(0.5, (peak_max - peak_min) * 0.1);
  peak_axis_->setRange(peak_min - peak_pad, peak_max + peak_pad);
  const double wns_pad = std::max(0.01, (wns_max - wns_min) * 0.1);
  wns_axis_->setRange(wns_min - wns_pad, wns_max + wns_pad);
}

void PhysicsWidget::setReference(const thm::PhysicsHistory* reference)
{
  reference_peak_series_->clear();
  if (reference == nullptr || reference->empty()) {
    reference_peak_series_->setVisible(false);
    return;
  }
  for (int i = 0; i < reference->size(); ++i) {
    reference_peak_series_->append(i, reference->at(i).metrics.peakTemp());
  }
  reference_peak_series_->setVisible(true);
  if (thermal_ != nullptr) {
    rebuildChart(thermal_->history());
  }
}

void PhysicsWidget::showTemperatureMap()
{
  if (!show_map_->isChecked()) {
    return;
  }
  web::Gui::get()->setDisplayControlsVisible(kTemperatureControl, true);
}

}  // namespace gui
