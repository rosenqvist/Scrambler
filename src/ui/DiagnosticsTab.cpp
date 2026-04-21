#include "ui/DiagnosticsTab.h"

#include "core/Diagnostics.h"

#include <QClipboard>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include <chrono>
#include <ctime>
#include <format>

namespace scrambler::ui
{

namespace
{

constexpr int kRefreshIntervalMs = 500;
constexpr int kLogViewMaxBlocks = 2048;
constexpr size_t kLogPollBatch = 512;

// Red and bold. Applied to any counter where a non-zero value means a real problem.
constexpr const char* kAlertStyle = "color: #c84a3a; font-weight: bold;";
constexpr const char* kNormalStyle = "";

const char* LevelName(core::LogLevel level) noexcept
{
    switch (level)
    {
        case core::LogLevel::kInfo:
            return "INFO ";
        case core::LogLevel::kWarn:
            return "WARN ";
        case core::LogLevel::kError:
            return "ERROR";
        case core::LogLevel::kFatal:
            return "FATAL";
    }
    return "?    ";
}

QString FormatEntry(const core::LogEntry& e)
{
    const auto t = std::chrono::system_clock::to_time_t(e.when);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(e.when.time_since_epoch()).count() % 1000;

    std::tm tm{};
    ::localtime_s(&tm, &t);

    return QString::fromStdString(std::format(
        "[{:02}:{:02}:{:02}.{:03}] [{}] {}", tm.tm_hour, tm.tm_min, tm.tm_sec, ms, LevelName(e.level), e.message));
}

}  // namespace

DiagnosticsTab::DiagnosticsTab(QWidget* parent) : QWidget(parent), refresh_timer_(new QTimer(this))
{
    SetupUi();

    // The timer is started in showEvent and stopped in hideEvent so we don't
    // snapshot the log ring twice a second while the tab isn't visible.
    connect(refresh_timer_, &QTimer::timeout, this, &DiagnosticsTab::Refresh);
}

void DiagnosticsTab::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh_timer_->start(kRefreshIntervalMs);
    // Paint the latest state immediately instead of waiting up to 500ms.
    Refresh();
}

void DiagnosticsTab::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    refresh_timer_->stop();
}

void DiagnosticsTab::SetupUi()
{
    auto* root = new QVBoxLayout(this);

    // --- Counters ---
    auto* counters_group = new QGroupBox("Counters");
    auto* counters_grid = new QGridLayout(counters_group);

    auto add_row = [&](int row, const QString& name, QLabel*& value_label)
    {
        auto* name_label = new QLabel(name + ":");
        value_label = new QLabel("0");
        value_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        // Wide enough to hold the "N  (out X / in Y)" directional format
        // without reflowing the whole column as numbers grow.
        value_label->setMinimumWidth(200);
        counters_grid->addWidget(name_label, row, 0);
        counters_grid->addWidget(value_label, row, 1);
    };

    int row = 0;
    add_row(row++, "Packets captured", packets_captured_value_);
    add_row(row++, "Packets reinjected", packets_reinjected_value_);
    add_row(row++, "Packets dropped", packets_dropped_value_);
    add_row(row++, "Packets delayed", packets_delayed_value_);
    add_row(row++, "Packets duplicated", packets_duplicated_value_);
    add_row(row++, "Pool exhausted", pool_exhausted_value_);
    add_row(row++, "Packets oversized", packets_oversized_value_);
    add_row(row++, "Reinject failures", reinject_failures_value_);
    add_row(row++, "Parse failures", parse_failures_value_);
    add_row(row++, "Flow cache hits", flow_cache_hits_value_);
    add_row(row++, "Flow kernel scans", flow_kernel_scans_value_);
    add_row(row++, "Driver errors", driver_errors_value_);
    counters_grid->setColumnStretch(2, 1);

    root->addWidget(counters_group);

    // --- Log viewer ---
    auto* log_group = new QGroupBox("Log");
    auto* log_layout = new QVBoxLayout(log_group);

    log_view_ = new QPlainTextEdit();
    log_view_->setReadOnly(true);
    log_view_->setMaximumBlockCount(kLogViewMaxBlocks);
    log_view_->setFont(QFont("Consolas", 9));
    log_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    log_layout->addWidget(log_view_);

    // --- Actions ---
    auto* actions_layout = new QHBoxLayout();
    auto* copy_button = new QPushButton("Copy log");
    auto* clear_button = new QPushButton("Clear log");
    auto* reset_button = new QPushButton("Reset counters");
    actions_layout->addWidget(copy_button);
    actions_layout->addWidget(clear_button);
    actions_layout->addWidget(reset_button);
    actions_layout->addStretch();
    log_layout->addLayout(actions_layout);

    root->addWidget(log_group, 1);  // log group takes remaining vertical space

    connect(copy_button, &QPushButton::clicked, this, &DiagnosticsTab::CopyLog);
    connect(clear_button, &QPushButton::clicked, this, &DiagnosticsTab::ClearLog);
    connect(reset_button, &QPushButton::clicked, this, &DiagnosticsTab::ResetCounters);
}

void DiagnosticsTab::Refresh()
{
    RefreshCounters();
    RefreshLog();
}

void DiagnosticsTab::RefreshCounters()
{
    const auto& d = core::Diagnostics::Instance();

    auto set = [](QLabel* label, uint64_t value, bool alert_when_nonzero = false)
    {
        label->setText(QString::number(value));
        label->setStyleSheet(alert_when_nonzero && value > 0 ? kAlertStyle : kNormalStyle);
    };

    auto set_directional = [](QLabel* label, uint64_t outbound, uint64_t inbound)
    {
        label->setText(QString("%1  (out %2 / in %3)").arg(outbound + inbound).arg(outbound).arg(inbound));
    };

    set_directional(packets_captured_value_,
                    d.Get(core::Counter::kPacketsCapturedOutbound),
                    d.Get(core::Counter::kPacketsCapturedInbound));
    set(packets_reinjected_value_, d.Get(core::Counter::kPacketsReinjected));
    set_directional(packets_dropped_value_,
                    d.Get(core::Counter::kPacketsDroppedOutbound),
                    d.Get(core::Counter::kPacketsDroppedInbound));
    set_directional(packets_delayed_value_,
                    d.Get(core::Counter::kPacketsDelayedOutbound),
                    d.Get(core::Counter::kPacketsDelayedInbound));
    set_directional(packets_duplicated_value_,
                    d.Get(core::Counter::kPacketsDuplicatedOutbound),
                    d.Get(core::Counter::kPacketsDuplicatedInbound));
    set(pool_exhausted_value_, d.Get(core::Counter::kPoolExhausted), true);
    set(packets_oversized_value_, d.Get(core::Counter::kPacketsOversized), true);
    set(reinject_failures_value_, d.Get(core::Counter::kReinjectFailures), true);
    set(parse_failures_value_, d.Get(core::Counter::kParseFailures), true);
    set(flow_cache_hits_value_, d.Get(core::Counter::kFlowCacheHits));
    set(flow_kernel_scans_value_, d.Get(core::Counter::kFlowCacheKernelScans));
    set(driver_errors_value_, d.Get(core::Counter::kDriverErrors), true);
}

void DiagnosticsTab::RefreshLog()
{
    const auto entries = core::Diagnostics::Instance().SnapshotSince(last_seen_seq_, kLogPollBatch);
    if (entries.empty())
    {
        return;
    }

    for (const auto& e : entries)
    {
        log_view_->appendPlainText(FormatEntry(e));
    }
    // SnapshotSince returns entries in chronological order so the last one has the highest seq.
    last_seen_seq_ = entries.back().seq;
}

void DiagnosticsTab::CopyLog()
{
    QGuiApplication::clipboard()->setText(log_view_->toPlainText());
}

void DiagnosticsTab::ClearLog()
{
    // Keep last_seen_seq_ as-is. The user wants to hide what they've seen,
    // not replay history, so future ticks only append genuinely new entries.
    log_view_->clear();
}

void DiagnosticsTab::ResetCounters()
{
    core::Diagnostics::Instance().ResetCounters();
    RefreshCounters();
}

}  // namespace scrambler::ui
