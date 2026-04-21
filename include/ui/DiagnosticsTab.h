#pragma once

#include <QLabel>
#include <QPlainTextEdit>
#include <QTimer>
#include <QWidget>

#include <cstdint>

namespace scrambler::ui
{

// Diagnostics tab: live counters and log viewer.
//
// Performance: this widget never touches any packet capture hot paths. It
// only reads atomic counters and snapshots the log ring on a 500 ms QTimer tick.
// The core pipeline has no idea whether this tab exists or is open.
class DiagnosticsTab : public QWidget
{
    Q_OBJECT

public:
    explicit DiagnosticsTab(QWidget* parent = nullptr);
    ~DiagnosticsTab() override = default;

    DiagnosticsTab(const DiagnosticsTab&) = delete;
    DiagnosticsTab& operator=(const DiagnosticsTab&) = delete;
    DiagnosticsTab(DiagnosticsTab&&) = delete;
    DiagnosticsTab& operator=(DiagnosticsTab&&) = delete;

protected:
    // Pause the refresh timer when the tab isn't visible
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void Refresh();
    void CopyLog();
    void ClearLog();
    void ResetCounters();

private:  // NOLINT(readability-redundant-access-specifiers)
    void SetupUi();
    void RefreshCounters();
    void RefreshLog();

    QTimer* refresh_timer_ = nullptr;

    // Counter value labels (one per Counter we surface)
    QLabel* packets_captured_value_ = nullptr;
    QLabel* packets_reinjected_value_ = nullptr;
    QLabel* packets_dropped_value_ = nullptr;
    QLabel* packets_delayed_value_ = nullptr;
    QLabel* packets_duplicated_value_ = nullptr;
    QLabel* pool_exhausted_value_ = nullptr;
    QLabel* packets_oversized_value_ = nullptr;
    QLabel* reinject_failures_value_ = nullptr;
    QLabel* parse_failures_value_ = nullptr;
    QLabel* flow_cache_hits_value_ = nullptr;
    QLabel* flow_kernel_scans_value_ = nullptr;
    QLabel* driver_errors_value_ = nullptr;

    // Log viewer
    QPlainTextEdit* log_view_ = nullptr;

    // Seq of the most recent log entry we have already appended to log_view_.
    // Polling and filtering here avoids any cross-thread sink call into Qt widgets.
    uint64_t last_seen_seq_ = 0;
};

}  // namespace scrambler::ui
