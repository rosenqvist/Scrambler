#include "ui/MainWindow.h"

#include "core/EffectConfig.h"
#include "core/FlowTracker.h"
#include "core/PacketInterceptor.h"
#include "core/StartupError.h"
#include "platform/ProcessEnumerator.h"
#include "ui/DiagnosticsTab.h"
#include "ui/HotkeyEdit.h"
#include "ui/HotkeyManager.h"
#include "ui/SoundPlayer.h"

#include <QAbstractItemView>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QMetaObject>
#include <QObject>
#include <QOverload>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <Qt>
#include <QTabWidget>
#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/qobjectdefs.h>
#include <QtGlobal>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace scrambler::ui
{

namespace
{
constexpr int kProcessPidRole = Qt::UserRole;
constexpr int kProcessCreationTimeRole = Qt::UserRole + 1;
constexpr int kProcessExePathRole = Qt::UserRole + 2;
constexpr int kTargetMonitorIntervalMs = 500;

// Recursively filters the tree. A node is visible if it matches the text itself.
bool FilterTreeItem(QTreeWidgetItem* item, const QString& text)
{
    const bool self_matches = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive)
                              || item->text(1).contains(text, Qt::CaseInsensitive);

    bool any_descendant_matches = false;
    for (int i = 0; i < item->childCount(); ++i)
    {
        if (FilterTreeItem(item->child(i), text))
        {
            any_descendant_matches = true;
        }
    }

    item->setHidden(!self_matches && !any_descendant_matches);

    // Expand ancestors of a match so the user can see it.
    if (!text.isEmpty() && any_descendant_matches)
    {
        item->setExpanded(true);
    }

    return self_matches || any_descendant_matches;
}

platform::ProcessIdentity IdentityFromItem(const QTreeWidgetItem* item)
{
    if (item == nullptr)
    {
        return {};
    }

    return {
        .pid = item->data(0, kProcessPidRole).toUInt(),
        .creation_time = item->data(0, kProcessCreationTimeRole).toULongLong(),
        .exe_path = item->data(0, kProcessExePathRole).toString().toStdWString(),
    };
}
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      refresh_timer_(new QTimer(this)),
      target_monitor_timer_(new QTimer(this)),
      process_watcher_(new QFutureWatcher<std::vector<platform::ProcessInfo>>(this)),
      hotkey_manager_(new HotkeyManager(this)),
      sound_player_(new SoundPlayer(this))
{
    SetupUi();

    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::RefreshProcessList);
    refresh_timer_->start(5000);

    target_monitor_timer_->setInterval(kTargetMonitorIntervalMs);
    connect(target_monitor_timer_, &QTimer::timeout, this, &MainWindow::CheckSelectedProcessIdentity);

    connect(process_watcher_,
            &QFutureWatcher<std::vector<platform::ProcessInfo>>::finished,
            this,
            &MainWindow::OnProcessListReady);

    RefreshProcessList();
    UpdatePipelineStatus("Stopped", false);
}

MainWindow::~MainWindow()
{
    StopPipeline();
}

void MainWindow::SetupUi()
{
    setWindowTitle("Scrambler 1.4");
    setMinimumSize(520, 600);

    auto* central = new QWidget();
    auto* main_layout = new QVBoxLayout(central);

    tab_widget_ = new QTabWidget();

    // Tab 1: Network
    auto* network_tab = new QWidget();
    auto* network_layout = new QVBoxLayout(network_tab);

    // Process list:
    auto* process_group = new QGroupBox("Processes");
    auto* process_layout = new QVBoxLayout(process_group);

    process_filter_ = new QLineEdit();
    process_filter_->setPlaceholderText("Filter by name or PID...");
    process_layout->addWidget(process_filter_);

    process_tree_ = new QTreeWidget();
    process_tree_->setHeaderLabels({"PID", "Name"});
    process_tree_->setRootIsDecorated(true);
    process_tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    process_tree_->setSortingEnabled(true);
    process_tree_->sortByColumn(1, Qt::AscendingOrder);
    process_tree_->header()->setStretchLastSection(true);
    process_tree_->setColumnWidth(0, 80);

    // Placeholder shown until the first enumeration completes. OnProcessListReady
    // clears the tree before populating it so this vanishes on the first tick.
    auto* loading_item = new QTreeWidgetItem();
    loading_item->setText(1, "Loading processes...");
    loading_item->setDisabled(true);
    process_tree_->addTopLevelItem(loading_item);

    process_layout->addWidget(process_tree_);

    network_layout->addWidget(process_group);

    // Start/Stop:
    start_stop_button_ = new QPushButton("Start");
    start_stop_button_->setFixedHeight(36);
    network_layout->addWidget(start_stop_button_);

    // Network conditions:
    auto* effects_group = new QGroupBox("Network Conditions");
    auto* effects_layout = new QVBoxLayout(effects_group);

    auto make_effect_controls =
        [](int max_value, const QString& suffix, QSlider*& slider, QSpinBox*& spinbox) -> QHBoxLayout*
    {
        auto* layout = new QHBoxLayout();
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, max_value);
        slider->setValue(0);
        spinbox = new QSpinBox();
        spinbox->setRange(0, max_value);
        spinbox->setSuffix(suffix);
        spinbox->setFixedWidth(72);
        layout->addWidget(slider);
        layout->addWidget(spinbox);
        return layout;
    };

    auto make_primary_effect_row = [](const QString& label_text,
                                      int max_value,
                                      const QString& suffix,
                                      QSlider*& slider,
                                      QSpinBox*& spinbox,
                                      QCheckBox*& checkbox) -> QHBoxLayout*
    {
        auto* layout = new QHBoxLayout();
        auto* label = new QLabel(label_text);
        label->setFixedWidth(104);
        checkbox = new QCheckBox("Asymmetric");
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, max_value);
        slider->setValue(0);
        spinbox = new QSpinBox();
        spinbox->setRange(0, max_value);
        spinbox->setSuffix(suffix);
        spinbox->setFixedWidth(72);
        layout->addWidget(label);
        layout->addWidget(slider);
        layout->addWidget(spinbox);
        layout->addWidget(checkbox);
        return layout;
    };

    auto make_labeled_effect_row = [&](const QString& label_text,
                                       int max_value,
                                       const QString& suffix,
                                       QSlider*& slider,
                                       QSpinBox*& spinbox) -> QHBoxLayout*
    {
        auto* layout = new QHBoxLayout();
        auto* label = new QLabel(label_text);
        label->setFixedWidth(104);
        auto* controls = make_effect_controls(max_value, suffix, slider, spinbox);
        layout->addWidget(label);
        layout->addLayout(controls);
        return layout;
    };

    auto make_directional_controls = [&](QWidget*& container,
                                         int max_value,
                                         const QString& suffix,
                                         QSlider*& outbound_slider,
                                         QSpinBox*& outbound_spinbox,
                                         QSlider*& inbound_slider,
                                         QSpinBox*& inbound_spinbox)
    {
        container = new QWidget();
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(24, 0, 0, 0);

        auto* outbound_row = new QHBoxLayout();
        auto* outbound_label = new QLabel("Out:");
        outbound_label->setFixedWidth(48);
        outbound_row->addWidget(outbound_label);
        auto* outbound_controls = make_effect_controls(max_value, suffix, outbound_slider, outbound_spinbox);
        outbound_row->addLayout(outbound_controls);

        auto* inbound_row = new QHBoxLayout();
        auto* inbound_label = new QLabel("In:");
        inbound_label->setFixedWidth(48);
        inbound_row->addWidget(inbound_label);
        auto* inbound_controls = make_effect_controls(max_value, suffix, inbound_slider, inbound_spinbox);
        inbound_row->addLayout(inbound_controls);

        layout->addLayout(outbound_row);
        layout->addLayout(inbound_row);
        container->setVisible(false);
    };

    effects_layout->addLayout(
        make_primary_effect_row("Delay:", 1000, " ms", delay_slider_, delay_spinbox_, delay_asymmetric_checkbox_));
    make_directional_controls(delay_asymmetric_controls_,
                              1000,
                              " ms",
                              outbound_delay_slider_,
                              outbound_delay_spinbox_,
                              inbound_delay_slider_,
                              inbound_delay_spinbox_);
    effects_layout->addWidget(delay_asymmetric_controls_);

    effects_layout->addLayout(make_primary_effect_row(
        "Jitter:", core::kMaxDelayJitterMs, " ms", jitter_slider_, jitter_spinbox_, jitter_asymmetric_checkbox_));
    make_directional_controls(jitter_asymmetric_controls_,
                              core::kMaxDelayJitterMs,
                              " ms",
                              outbound_jitter_slider_,
                              outbound_jitter_spinbox_,
                              inbound_jitter_slider_,
                              inbound_jitter_spinbox_);
    effects_layout->addWidget(jitter_asymmetric_controls_);

    effects_layout->addLayout(make_primary_effect_row("Throttle:",
                                                      core::kMaxThrottleKBytesPerSec,
                                                      " KB/s",
                                                      throttle_slider_,
                                                      throttle_spinbox_,
                                                      throttle_asymmetric_checkbox_));
    make_directional_controls(throttle_asymmetric_controls_,
                              core::kMaxThrottleKBytesPerSec,
                              " KB/s",
                              outbound_throttle_slider_,
                              outbound_throttle_spinbox_,
                              inbound_throttle_slider_,
                              inbound_throttle_spinbox_);
    effects_layout->addWidget(throttle_asymmetric_controls_);

    auto* drop_row =
        make_primary_effect_row("Drop:", 100, " %", drop_slider_, drop_spinbox_, drop_asymmetric_checkbox_);
    burst_drop_checkbox_ = new QCheckBox("Burst loss");
    drop_row->insertWidget(drop_row->count() - 1, burst_drop_checkbox_);
    effects_layout->addLayout(drop_row);
    make_directional_controls(drop_asymmetric_controls_,
                              100,
                              " %",
                              outbound_drop_slider_,
                              outbound_drop_spinbox_,
                              inbound_drop_slider_,
                              inbound_drop_spinbox_);
    effects_layout->addWidget(drop_asymmetric_controls_);

    burst_drop_controls_ = new QWidget();
    auto* burst_drop_layout = new QVBoxLayout(burst_drop_controls_);
    burst_drop_layout->setContentsMargins(24, 0, 0, 0);
    burst_drop_layout->addLayout(make_primary_effect_row("Burst chance:",
                                                         100,
                                                         " %",
                                                         burst_drop_chance_slider_,
                                                         burst_drop_chance_spinbox_,
                                                         burst_drop_asymmetric_checkbox_));
    make_directional_controls(burst_drop_chance_asymmetric_controls_,
                              100,
                              " %",
                              outbound_burst_drop_chance_slider_,
                              outbound_burst_drop_chance_spinbox_,
                              inbound_burst_drop_chance_slider_,
                              inbound_burst_drop_chance_spinbox_);
    burst_drop_layout->addWidget(burst_drop_chance_asymmetric_controls_);
    burst_drop_layout->addLayout(make_labeled_effect_row(
        "Burst length:", core::kMaxBurstDropLength, "", burst_drop_length_slider_, burst_drop_length_spinbox_));
    make_directional_controls(burst_drop_length_asymmetric_controls_,
                              core::kMaxBurstDropLength,
                              "",
                              outbound_burst_drop_length_slider_,
                              outbound_burst_drop_length_spinbox_,
                              inbound_burst_drop_length_slider_,
                              inbound_burst_drop_length_spinbox_);
    burst_drop_layout->addWidget(burst_drop_length_asymmetric_controls_);
    burst_drop_controls_->setVisible(false);
    effects_layout->addWidget(burst_drop_controls_);

    auto* duplicate_row = new QHBoxLayout();
    auto* duplicate_label = new QLabel("Duplicate:");
    duplicate_label->setFixedWidth(104);
    duplicate_custom_count_checkbox_ = new QCheckBox("Custom copies");
    duplicate_asymmetric_checkbox_ = new QCheckBox("Asymmetric");
    duplicate_slider_ = new QSlider(Qt::Horizontal);
    duplicate_slider_->setRange(0, 100);
    duplicate_slider_->setValue(0);
    duplicate_spinbox_ = new QSpinBox();
    duplicate_spinbox_->setRange(0, 100);
    duplicate_spinbox_->setSuffix(" %");
    duplicate_spinbox_->setFixedWidth(72);
    duplicate_row->addWidget(duplicate_label);
    duplicate_row->addWidget(duplicate_slider_);
    duplicate_row->addWidget(duplicate_spinbox_);
    duplicate_row->addWidget(duplicate_custom_count_checkbox_);
    duplicate_row->addWidget(duplicate_asymmetric_checkbox_);
    effects_layout->addLayout(duplicate_row);

    duplicate_count_controls_ = new QWidget();
    auto* duplicate_count_layout = new QVBoxLayout(duplicate_count_controls_);
    duplicate_count_layout->setContentsMargins(24, 0, 0, 0);
    duplicate_count_layout->addLayout(make_labeled_effect_row(
        "Extra copies:", core::kMaxDuplicateCopies, "", duplicate_count_slider_, duplicate_count_spinbox_));
    duplicate_count_controls_->setVisible(false);
    effects_layout->addWidget(duplicate_count_controls_);

    duplicate_asymmetric_controls_ = new QWidget();
    auto* duplicate_layout = new QVBoxLayout(duplicate_asymmetric_controls_);
    duplicate_layout->setContentsMargins(24, 0, 0, 0);

    auto make_duplicate_direction_row =
        [&](const QString& label_text, int max_value, const QString& suffix, QSlider*& slider, QSpinBox*& spinbox)
    {
        auto* row = new QHBoxLayout();
        auto* label = new QLabel(label_text);
        label->setFixedWidth(88);
        row->addWidget(label);
        row->addLayout(make_effect_controls(max_value, suffix, slider, spinbox));
        duplicate_layout->addLayout(row);
    };

    make_duplicate_direction_row("Out chance:", 100, " %", outbound_duplicate_slider_, outbound_duplicate_spinbox_);
    make_duplicate_direction_row("In chance:", 100, " %", inbound_duplicate_slider_, inbound_duplicate_spinbox_);

    duplicate_count_asymmetric_controls_ = new QWidget();
    auto* duplicate_count_asymmetric_layout = new QVBoxLayout(duplicate_count_asymmetric_controls_);
    duplicate_count_asymmetric_layout->setContentsMargins(0, 0, 0, 0);

    auto make_duplicate_count_direction_row = [&](const QString& label_text, QSlider*& slider, QSpinBox*& spinbox)
    {
        auto* row = new QHBoxLayout();
        auto* label = new QLabel(label_text);
        label->setFixedWidth(88);
        row->addWidget(label);
        row->addLayout(make_effect_controls(core::kMaxDuplicateCopies, "", slider, spinbox));
        duplicate_count_asymmetric_layout->addLayout(row);
    };

    make_duplicate_count_direction_row("Out copies:",
                                       outbound_duplicate_count_slider_,
                                       outbound_duplicate_count_spinbox_);
    make_duplicate_count_direction_row("In copies:", inbound_duplicate_count_slider_, inbound_duplicate_count_spinbox_);
    duplicate_count_asymmetric_controls_->setVisible(false);
    duplicate_layout->addWidget(duplicate_count_asymmetric_controls_);
    duplicate_asymmetric_controls_->setVisible(false);
    effects_layout->addWidget(duplicate_asymmetric_controls_);

    auto initialize_duplicate_count_control = [](QSlider* slider, QSpinBox* spinbox)
    {
        slider->setRange(core::kDefaultDuplicateCopies, core::kMaxDuplicateCopies);
        slider->setValue(core::kDefaultDuplicateCopies);
        spinbox->setRange(core::kDefaultDuplicateCopies, core::kMaxDuplicateCopies);
        spinbox->setValue(core::kDefaultDuplicateCopies);
    };

    initialize_duplicate_count_control(duplicate_count_slider_, duplicate_count_spinbox_);
    initialize_duplicate_count_control(outbound_duplicate_count_slider_, outbound_duplicate_count_spinbox_);
    initialize_duplicate_count_control(inbound_duplicate_count_slider_, inbound_duplicate_count_spinbox_);

    auto initialize_burst_length_control = [](QSlider* slider, QSpinBox* spinbox)
    {
        slider->setRange(1, core::kMaxBurstDropLength);
        slider->setValue(core::kDefaultBurstDropLength);
        spinbox->setRange(1, core::kMaxBurstDropLength);
        spinbox->setValue(core::kDefaultBurstDropLength);
    };

    initialize_burst_length_control(burst_drop_length_slider_, burst_drop_length_spinbox_);
    initialize_burst_length_control(outbound_burst_drop_length_slider_, outbound_burst_drop_length_spinbox_);
    initialize_burst_length_control(inbound_burst_drop_length_slider_, inbound_burst_drop_length_spinbox_);

    network_layout->addWidget(effects_group);

    tab_widget_->addTab(network_tab, "Network");

    // Tab 2: Settings
    auto* settings_tab = new QWidget();
    auto* settings_layout = new QVBoxLayout(settings_tab);

    // Hotkeys:
    auto* hotkey_group = new QGroupBox("Hotkeys");
    auto* hotkey_layout = new QFormLayout(hotkey_group);

    auto make_hotkey_row = [](HotkeyEdit*& edit) -> QHBoxLayout*
    {
        auto* layout = new QHBoxLayout();
        edit = new HotkeyEdit();
        auto* clear_button = new QPushButton("X");
        clear_button->setFixedWidth(24);
        clear_button->setToolTip("Clear hotkey");
        layout->addWidget(edit);
        layout->addWidget(clear_button);
        QObject::connect(clear_button,
                         &QPushButton::clicked,
                         edit,
                         [edit]
        {
            edit->SetBinding({});
            edit->BindingChanged({});
        });
        return layout;
    };

    hotkey_layout->addRow("Toggle Start/Stop:", make_hotkey_row(toggle_hotkey_edit_));
    hotkey_layout->addRow("Increase Delay:", make_hotkey_row(inc_delay_hotkey_edit_));
    hotkey_layout->addRow("Decrease Delay:", make_hotkey_row(dec_delay_hotkey_edit_));

    delay_step_spinbox_ = new QSpinBox();
    delay_step_spinbox_->setRange(1, 500);
    delay_step_spinbox_->setSuffix(" ms");
    hotkey_layout->addRow("Delay Step:", delay_step_spinbox_);

    hotkey_layout->addRow("Increase Drop:", make_hotkey_row(inc_drop_hotkey_edit_));
    hotkey_layout->addRow("Decrease Drop:", make_hotkey_row(dec_drop_hotkey_edit_));

    drop_step_spinbox_ = new QSpinBox();
    drop_step_spinbox_->setRange(1, 50);
    drop_step_spinbox_->setSuffix(" %");
    hotkey_layout->addRow("Drop Step:", drop_step_spinbox_);

    auto* clear_all_button = new QPushButton("Clear All Hotkeys");
    hotkey_layout->addRow(clear_all_button);

    settings_layout->addWidget(hotkey_group);

    // Sound:
    auto* sound_group = new QGroupBox("Sound");
    auto* sound_layout = new QFormLayout(sound_group);

    sound_checkbox_ = new QCheckBox("Play sound on toggle");
    sound_layout->addRow(sound_checkbox_);

    auto* volume_layout = new QHBoxLayout();
    volume_slider_ = new QSlider(Qt::Horizontal);
    volume_slider_->setRange(0, 100);
    volume_label_ = new QLabel();
    volume_label_->setFixedWidth(40);
    volume_layout->addWidget(volume_slider_);
    volume_layout->addWidget(volume_label_);
    sound_layout->addRow("Volume:", volume_layout);

    settings_layout->addWidget(sound_group);
    settings_layout->addStretch();

    tab_widget_->addTab(settings_tab, "Settings");

    // Tab 3: Diagnostics
    diagnostics_tab_ = new DiagnosticsTab();
    tab_widget_->addTab(diagnostics_tab_, "Diagnostics");

    main_layout->addWidget(tab_widget_);

    // Status bar:
    status_label_ = new QLabel();
    statusBar()->addPermanentWidget(status_label_);

    setCentralWidget(central);

    // Load saved state:
    QSettings settings;
    settings.beginGroup("UI");
    delay_step_spinbox_->setValue(settings.value("DelayStep", 10).toInt());
    drop_step_spinbox_->setValue(settings.value("DropStep", 1).toInt());
    sound_checkbox_->setChecked(settings.value("SoundEnabled", false).toBool());
    const int saved_volume = settings.value("Volume", 25).toInt();
    settings.endGroup();

    volume_slider_->setValue(saved_volume);
    volume_label_->setText(QString::number(saved_volume) + "%");

    auto load_hotkey_edit = [this](HotkeyEdit* edit, HotkeyAction action)
    {
        edit->SetBinding(hotkey_manager_->GetBinding(action));
    };

    load_hotkey_edit(toggle_hotkey_edit_, HotkeyAction::kToggleEffects);
    load_hotkey_edit(inc_delay_hotkey_edit_, HotkeyAction::kIncrementDelay);
    load_hotkey_edit(dec_delay_hotkey_edit_, HotkeyAction::kDecrementDelay);
    load_hotkey_edit(inc_drop_hotkey_edit_, HotkeyAction::kIncrementDropRate);
    load_hotkey_edit(dec_drop_hotkey_edit_, HotkeyAction::kDecrementDropRate);

    // Wire up the signals:
    connect(process_filter_, &QLineEdit::textChanged, this, &MainWindow::OnProcessFilterChanged);
    connect(process_tree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::OnProcessSelectionChanged);
    connect(start_stop_button_, &QPushButton::clicked, this, &MainWindow::OnStartStopClicked);

    auto connect_slider_pair = [](QSlider* slider, QSpinBox* spinbox)
    {
        QObject::connect(slider, &QSlider::valueChanged, spinbox, &QSpinBox::setValue);
        QObject::connect(spinbox, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    };

    connect_slider_pair(delay_slider_, delay_spinbox_);
    connect_slider_pair(jitter_slider_, jitter_spinbox_);
    connect_slider_pair(drop_slider_, drop_spinbox_);
    connect_slider_pair(outbound_delay_slider_, outbound_delay_spinbox_);
    connect_slider_pair(inbound_delay_slider_, inbound_delay_spinbox_);
    connect_slider_pair(outbound_jitter_slider_, outbound_jitter_spinbox_);
    connect_slider_pair(inbound_jitter_slider_, inbound_jitter_spinbox_);
    connect_slider_pair(throttle_slider_, throttle_spinbox_);
    connect_slider_pair(outbound_throttle_slider_, outbound_throttle_spinbox_);
    connect_slider_pair(inbound_throttle_slider_, inbound_throttle_spinbox_);
    connect_slider_pair(outbound_drop_slider_, outbound_drop_spinbox_);
    connect_slider_pair(inbound_drop_slider_, inbound_drop_spinbox_);
    connect_slider_pair(burst_drop_chance_slider_, burst_drop_chance_spinbox_);
    connect_slider_pair(outbound_burst_drop_chance_slider_, outbound_burst_drop_chance_spinbox_);
    connect_slider_pair(inbound_burst_drop_chance_slider_, inbound_burst_drop_chance_spinbox_);
    connect_slider_pair(burst_drop_length_slider_, burst_drop_length_spinbox_);
    connect_slider_pair(outbound_burst_drop_length_slider_, outbound_burst_drop_length_spinbox_);
    connect_slider_pair(inbound_burst_drop_length_slider_, inbound_burst_drop_length_spinbox_);
    connect_slider_pair(duplicate_slider_, duplicate_spinbox_);
    connect_slider_pair(outbound_duplicate_slider_, outbound_duplicate_spinbox_);
    connect_slider_pair(inbound_duplicate_slider_, inbound_duplicate_spinbox_);
    connect_slider_pair(duplicate_count_slider_, duplicate_count_spinbox_);
    connect_slider_pair(outbound_duplicate_count_slider_, outbound_duplicate_count_spinbox_);
    connect_slider_pair(inbound_duplicate_count_slider_, inbound_duplicate_count_spinbox_);

    auto sync_slider_value = [](QSlider* slider, int value)
    {
        if (slider->value() != value)
        {
            slider->setValue(value);
        }
    };

    auto update_duplicate_copy_controls_visibility = [this]()
    {
        const bool custom_copies = duplicate_custom_count_checkbox_->isChecked();
        const bool asymmetric = duplicate_asymmetric_checkbox_->isChecked();

        duplicate_count_controls_->setVisible(custom_copies && !asymmetric);
        duplicate_count_asymmetric_controls_->setVisible(custom_copies && asymmetric);
        duplicate_count_slider_->setEnabled(custom_copies && !asymmetric);
        duplicate_count_spinbox_->setEnabled(custom_copies && !asymmetric);
    };

    auto update_burst_drop_controls_visibility = [this]()
    {
        const bool burst_mode = burst_drop_checkbox_->isChecked();
        const bool asymmetric = burst_drop_asymmetric_checkbox_->isChecked();

        burst_drop_controls_->setVisible(burst_mode);
        drop_asymmetric_checkbox_->setEnabled(!burst_mode);
        drop_slider_->setEnabled(!burst_mode && !drop_asymmetric_checkbox_->isChecked());
        drop_spinbox_->setEnabled(!burst_mode && !drop_asymmetric_checkbox_->isChecked());
        drop_asymmetric_controls_->setVisible(!burst_mode && drop_asymmetric_checkbox_->isChecked());

        burst_drop_chance_slider_->setEnabled(burst_mode && !asymmetric);
        burst_drop_chance_spinbox_->setEnabled(burst_mode && !asymmetric);
        burst_drop_length_slider_->setEnabled(burst_mode && !asymmetric);
        burst_drop_length_spinbox_->setEnabled(burst_mode && !asymmetric);
        burst_drop_chance_asymmetric_controls_->setVisible(burst_mode && asymmetric);
        burst_drop_length_asymmetric_controls_->setVisible(burst_mode && asymmetric);
    };

    connect(delay_slider_,
            &QSlider::valueChanged,
            this,
            [this, sync_slider_value](int value)
    {
        effects_.SetDelayMs(true, value);
        effects_.SetDelayMs(false, value);
        sync_slider_value(outbound_delay_slider_, value);
        sync_slider_value(inbound_delay_slider_, value);
    });

    connect(drop_slider_,
            &QSlider::valueChanged,
            this,
            [this, sync_slider_value](int value)
    {
        const float rate = static_cast<float>(value) / 100.0F;
        effects_.SetDropRate(true, rate);
        effects_.SetDropRate(false, rate);
        sync_slider_value(outbound_drop_slider_, value);
        sync_slider_value(inbound_drop_slider_, value);
    });

    connect(jitter_slider_,
            &QSlider::valueChanged,
            this,
            [this, sync_slider_value](int value)
    {
        effects_.SetDelayJitterMs(true, value);
        effects_.SetDelayJitterMs(false, value);
        sync_slider_value(outbound_jitter_slider_, value);
        sync_slider_value(inbound_jitter_slider_, value);
    });

    connect(throttle_slider_,
            &QSlider::valueChanged,
            this,
            [this, sync_slider_value](int value)
    {
        effects_.SetThrottleKBytesPerSec(true, value);
        effects_.SetThrottleKBytesPerSec(false, value);
        sync_slider_value(outbound_throttle_slider_, value);
        sync_slider_value(inbound_throttle_slider_, value);
    });

    connect(outbound_delay_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetDelayMs(true, value);
    });

    connect(inbound_delay_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetDelayMs(false, value);
    });

    connect(outbound_jitter_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetDelayJitterMs(true, value);
    });

    connect(inbound_jitter_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetDelayJitterMs(false, value);
    });

    connect(outbound_throttle_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetThrottleKBytesPerSec(true, value);
    });

    connect(inbound_throttle_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetThrottleKBytesPerSec(false, value);
    });

    connect(outbound_drop_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetDropRate(true, static_cast<float>(value) / 100.0F);
    });

    connect(inbound_drop_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetDropRate(false, static_cast<float>(value) / 100.0F);
    });

    connect(burst_drop_chance_slider_,
            &QSlider::valueChanged,
            this,
            [this, sync_slider_value](int value)
    {
        const float rate = static_cast<float>(value) / 100.0F;
        effects_.SetBurstDropRate(true, rate);
        effects_.SetBurstDropRate(false, rate);
        sync_slider_value(outbound_burst_drop_chance_slider_, value);
        sync_slider_value(inbound_burst_drop_chance_slider_, value);
    });

    connect(outbound_burst_drop_chance_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetBurstDropRate(true, static_cast<float>(value) / 100.0F);
    });

    connect(inbound_burst_drop_chance_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetBurstDropRate(false, static_cast<float>(value) / 100.0F);
    });

    connect(burst_drop_length_slider_,
            &QSlider::valueChanged,
            this,
            [this, sync_slider_value](int value)
    {
        effects_.SetBurstDropLength(true, value);
        effects_.SetBurstDropLength(false, value);
        sync_slider_value(outbound_burst_drop_length_slider_, value);
        sync_slider_value(inbound_burst_drop_length_slider_, value);
    });

    connect(outbound_burst_drop_length_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetBurstDropLength(true, value);
    });

    connect(inbound_burst_drop_length_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetBurstDropLength(false, value);
    });

    connect(duplicate_slider_,
            &QSlider::valueChanged,
            this,
            [this, sync_slider_value](int value)
    {
        const float rate = static_cast<float>(value) / 100.0F;
        effects_.SetDuplicateRate(true, rate);
        effects_.SetDuplicateRate(false, rate);
        sync_slider_value(outbound_duplicate_slider_, value);
        sync_slider_value(inbound_duplicate_slider_, value);
    });

    connect(outbound_duplicate_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetDuplicateRate(true, static_cast<float>(value) / 100.0F);
    });

    connect(inbound_duplicate_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.SetDuplicateRate(false, static_cast<float>(value) / 100.0F);
    });

    connect(duplicate_count_slider_,
            &QSlider::valueChanged,
            this,
            [this, sync_slider_value](int value)
    {
        if (!duplicate_custom_count_checkbox_->isChecked())
        {
            return;
        }

        effects_.SetDuplicateCount(true, value);
        effects_.SetDuplicateCount(false, value);
        sync_slider_value(outbound_duplicate_count_slider_, value);
        sync_slider_value(inbound_duplicate_count_slider_, value);
    });

    connect(outbound_duplicate_count_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        if (!duplicate_custom_count_checkbox_->isChecked())
        {
            return;
        }

        effects_.SetDuplicateCount(true, value);
    });

    connect(inbound_duplicate_count_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        if (!duplicate_custom_count_checkbox_->isChecked())
        {
            return;
        }

        effects_.SetDuplicateCount(false, value);
    });

    connect(duplicate_custom_count_checkbox_,
            &QCheckBox::toggled,
            this,
            [this, update_duplicate_copy_controls_visibility](bool checked)
    {
        if (checked)
        {
            if (duplicate_asymmetric_checkbox_->isChecked())
            {
                effects_.SetDuplicateCount(true, outbound_duplicate_count_slider_->value());
                effects_.SetDuplicateCount(false, inbound_duplicate_count_slider_->value());
            }
            else
            {
                const int shared_count = duplicate_count_slider_->value();
                effects_.SetDuplicateCount(true, shared_count);
                effects_.SetDuplicateCount(false, shared_count);
            }
        }
        else
        {
            effects_.SetDuplicateCount(true, core::kDefaultDuplicateCopies);
            effects_.SetDuplicateCount(false, core::kDefaultDuplicateCopies);
        }

        update_duplicate_copy_controls_visibility();
    });

    connect(delay_asymmetric_checkbox_,
            &QCheckBox::toggled,
            this,
            [this](bool checked)
    {
        if (checked)
        {
            const int shared_value = delay_slider_->value();
            outbound_delay_slider_->setValue(shared_value);
            inbound_delay_slider_->setValue(shared_value);
        }
        else
        {
            delay_slider_->setValue(outbound_delay_slider_->value());
        }

        delay_slider_->setEnabled(!checked);
        delay_spinbox_->setEnabled(!checked);
        delay_asymmetric_controls_->setVisible(checked);
    });

    connect(drop_asymmetric_checkbox_,
            &QCheckBox::toggled,
            this,
            [this](bool checked)
    {
        if (checked)
        {
            const int shared_value = drop_slider_->value();
            outbound_drop_slider_->setValue(shared_value);
            inbound_drop_slider_->setValue(shared_value);
        }
        else
        {
            drop_slider_->setValue(outbound_drop_slider_->value());
        }

        drop_slider_->setEnabled(!checked);
        drop_spinbox_->setEnabled(!checked);
        drop_asymmetric_controls_->setVisible(checked);
    });

    connect(burst_drop_checkbox_,
            &QCheckBox::toggled,
            this,
            [this, update_burst_drop_controls_visibility](bool checked)
    {
        effects_.SetBurstDropEnabled(true, checked);
        effects_.SetBurstDropEnabled(false, checked);
        update_burst_drop_controls_visibility();
    });

    connect(burst_drop_asymmetric_checkbox_,
            &QCheckBox::toggled,
            this,
            [this, update_burst_drop_controls_visibility](bool checked)
    {
        if (checked)
        {
            const int shared_chance = burst_drop_chance_slider_->value();
            const int shared_length = burst_drop_length_slider_->value();
            outbound_burst_drop_chance_slider_->setValue(shared_chance);
            inbound_burst_drop_chance_slider_->setValue(shared_chance);
            outbound_burst_drop_length_slider_->setValue(shared_length);
            inbound_burst_drop_length_slider_->setValue(shared_length);
        }
        else
        {
            burst_drop_chance_slider_->setValue(outbound_burst_drop_chance_slider_->value());
            burst_drop_length_slider_->setValue(outbound_burst_drop_length_slider_->value());
        }

        update_burst_drop_controls_visibility();
    });

    connect(jitter_asymmetric_checkbox_,
            &QCheckBox::toggled,
            this,
            [this](bool checked)
    {
        if (checked)
        {
            const int shared_value = jitter_slider_->value();
            outbound_jitter_slider_->setValue(shared_value);
            inbound_jitter_slider_->setValue(shared_value);
        }
        else
        {
            jitter_slider_->setValue(outbound_jitter_slider_->value());
        }

        jitter_slider_->setEnabled(!checked);
        jitter_spinbox_->setEnabled(!checked);
        jitter_asymmetric_controls_->setVisible(checked);
    });

    connect(throttle_asymmetric_checkbox_,
            &QCheckBox::toggled,
            this,
            [this](bool checked)
    {
        if (checked)
        {
            const int shared_value = throttle_slider_->value();
            outbound_throttle_slider_->setValue(shared_value);
            inbound_throttle_slider_->setValue(shared_value);
        }
        else
        {
            throttle_slider_->setValue(outbound_throttle_slider_->value());
        }

        throttle_slider_->setEnabled(!checked);
        throttle_spinbox_->setEnabled(!checked);
        throttle_asymmetric_controls_->setVisible(checked);
    });

    connect(duplicate_asymmetric_checkbox_,
            &QCheckBox::toggled,
            this,
            [this, update_duplicate_copy_controls_visibility](bool checked)
    {
        if (checked)
        {
            const int shared_value = duplicate_slider_->value();
            outbound_duplicate_slider_->setValue(shared_value);
            inbound_duplicate_slider_->setValue(shared_value);
            if (duplicate_custom_count_checkbox_->isChecked())
            {
                const int shared_count = duplicate_count_slider_->value();
                outbound_duplicate_count_slider_->setValue(shared_count);
                inbound_duplicate_count_slider_->setValue(shared_count);
            }
        }
        else
        {
            duplicate_slider_->setValue(outbound_duplicate_slider_->value());
            if (duplicate_custom_count_checkbox_->isChecked())
            {
                duplicate_count_slider_->setValue(outbound_duplicate_count_slider_->value());
            }
        }

        duplicate_slider_->setEnabled(!checked);
        duplicate_spinbox_->setEnabled(!checked);
        duplicate_asymmetric_controls_->setVisible(checked);
        update_duplicate_copy_controls_visibility();
    });

    update_burst_drop_controls_visibility();
    update_duplicate_copy_controls_visibility();

    auto connect_hotkey_edit = [this](HotkeyEdit* edit, HotkeyAction action)
    {
        connect(edit,
                &HotkeyEdit::BindingChanged,
                this,
                [this, action](const HotkeyBinding& binding)
        {
            hotkey_manager_->SetBinding(action, binding);
        });
    };

    connect_hotkey_edit(toggle_hotkey_edit_, HotkeyAction::kToggleEffects);
    connect_hotkey_edit(inc_delay_hotkey_edit_, HotkeyAction::kIncrementDelay);
    connect_hotkey_edit(dec_delay_hotkey_edit_, HotkeyAction::kDecrementDelay);
    connect_hotkey_edit(inc_drop_hotkey_edit_, HotkeyAction::kIncrementDropRate);
    connect_hotkey_edit(dec_drop_hotkey_edit_, HotkeyAction::kDecrementDropRate);

    connect(hotkey_manager_, &HotkeyManager::HotkeyTriggered, this, &MainWindow::OnHotkeyTriggered);

    connect(clear_all_button,
            &QPushButton::clicked,
            this,
            [this]
    {
        for (auto* edit : {toggle_hotkey_edit_,
                           inc_delay_hotkey_edit_,
                           dec_delay_hotkey_edit_,
                           inc_drop_hotkey_edit_,
                           dec_drop_hotkey_edit_})
        {
            edit->SetBinding({});
            edit->BindingChanged({});
        }
    });

    connect(delay_step_spinbox_,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            [](int value)
    {
        QSettings s;
        s.setValue("UI/DelayStep", value);
    });

    connect(drop_step_spinbox_,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            [](int value)
    {
        QSettings s;
        s.setValue("UI/DropStep", value);
    });

    connect(sound_checkbox_,
            &QCheckBox::toggled,
            this,
            [](bool checked)
    {
        QSettings s;
        s.setValue("UI/SoundEnabled", checked);
    });

    connect(volume_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        volume_label_->setText(QString::number(value) + "%");
        QSettings s;
        s.setValue("UI/Volume", value);
    });
}

void MainWindow::OnStartStopClicked()
{
    if (running_)
    {
        StopPipeline();
    }
    else
    {
        StartPipeline();
    }
}

void MainWindow::StartPipeline()
{
    auto selected = process_tree_->selectedItems();
    if (selected.isEmpty())
    {
        QMessageBox::warning(this, "Scrambler", "Select a process first.");
        return;
    }

    auto identity = IdentityFromItem(selected.first());
    if (!platform::IsProcessIdentityCurrent(identity))
    {
        target_pids_.Clear();
        selected_process_identity_ = {};
        QMessageBox::warning(this, "Scrambler", "The selected process is no longer running. Select it again.");
        RefreshProcessList();
        return;
    }

    selected_process_identity_ = std::move(identity);
    target_pids_.SetSelectedPid(selected_process_identity_.pid);

    // Hop from the capture thread to the UI thread. Using `this` as the
    // receiver for invokeMethod means the event gets dropped if MainWindow
    // dies first.
    auto fatal_handler = [this](uint32_t gle)
    {
        QMetaObject::invokeMethod(this, &MainWindow::OnPipelineFatal, Qt::QueuedConnection, gle);
    };

    flow_tracker_ = std::make_unique<core::FlowTracker>();
    flow_tracker_->SetFatalCallback(fatal_handler);

    if (auto result = flow_tracker_->Start(); !result)
    {
        UpdatePipelineStatus(QString::fromUtf8(core::ToUserMessage(result.error())), true);
        flow_tracker_.reset();
        return;
    }

    interceptor_ = std::make_unique<core::PacketInterceptor>(*flow_tracker_, target_pids_, effects_);
    interceptor_->SetFatalCallback(std::move(fatal_handler));

    if (auto result = interceptor_->Start(); !result)
    {
        UpdatePipelineStatus(QString::fromUtf8(core::ToUserMessage(result.error())), true);
        // Reset interceptor first since it holds a reference to *flow_tracker_.
        interceptor_.reset();
        flow_tracker_.reset();
        return;
    }

    running_ = true;
    refresh_timer_->stop();
    target_monitor_timer_->start();
    start_stop_button_->setText("Stop");
    UpdatePipelineStatus("Running", false);
    PlayToggleSound(true);
}

void MainWindow::OnPipelineFatal(uint32_t gle)
{
    // The user may have clicked Stop before this queued event was delivered.
    if (!running_)
    {
        return;
    }

    StopPipeline();
    UpdatePipelineStatus(QString("Pipeline stopped unexpectedly (GLE=%1)").arg(gle), true);
}

void MainWindow::StopPipeline()
{
    if (!running_)
    {
        return;
    }

    if (interceptor_)
    {
        interceptor_->Stop();
        interceptor_.reset();
    }

    if (flow_tracker_)
    {
        flow_tracker_->Stop();
        flow_tracker_.reset();
    }

    running_ = false;
    target_monitor_timer_->stop();
    refresh_timer_->start(5000);
    RefreshProcessList();
    start_stop_button_->setText("Start");
    UpdatePipelineStatus("Stopped", false);
    PlayToggleSound(false);
}

void MainWindow::CheckSelectedProcessIdentity()
{
    if (!running_)
    {
        return;
    }

    if (platform::IsProcessIdentityCurrent(selected_process_identity_))
    {
        return;
    }

    target_pids_.Clear();
    selected_process_identity_ = {};
    StopPipeline();
    UpdatePipelineStatus("Selected process exited or was replaced; stopped.", true);
}

void MainWindow::OnHotkeyTriggered(HotkeyAction action)
{
    auto adjust_pair = [](QSlider* first, QSlider* second, int delta)
    {
        first->setValue(first->value() + delta);
        second->setValue(second->value() + delta);
    };

    switch (action)
    {
        case HotkeyAction::kToggleEffects:
            OnStartStopClicked();
            break;
        case HotkeyAction::kIncrementDelay:
            if (delay_asymmetric_checkbox_->isChecked())
            {
                adjust_pair(outbound_delay_slider_, inbound_delay_slider_, delay_step_spinbox_->value());
            }
            else
            {
                delay_slider_->setValue(delay_slider_->value() + delay_step_spinbox_->value());
            }
            break;
        case HotkeyAction::kDecrementDelay:
            if (delay_asymmetric_checkbox_->isChecked())
            {
                adjust_pair(outbound_delay_slider_, inbound_delay_slider_, -delay_step_spinbox_->value());
            }
            else
            {
                delay_slider_->setValue(delay_slider_->value() - delay_step_spinbox_->value());
            }
            break;
        case HotkeyAction::kIncrementDropRate:
            if (drop_asymmetric_checkbox_->isChecked())
            {
                adjust_pair(outbound_drop_slider_, inbound_drop_slider_, drop_step_spinbox_->value());
            }
            else
            {
                drop_slider_->setValue(drop_slider_->value() + drop_step_spinbox_->value());
            }
            break;
        case HotkeyAction::kDecrementDropRate:
            if (drop_asymmetric_checkbox_->isChecked())
            {
                adjust_pair(outbound_drop_slider_, inbound_drop_slider_, -drop_step_spinbox_->value());
            }
            else
            {
                drop_slider_->setValue(drop_slider_->value() - drop_step_spinbox_->value());
            }
            break;
        default:
            break;
    }
}

void MainWindow::PlayToggleSound(bool started)
{
    if (!sound_checkbox_->isChecked())
    {
        return;
    }

    constexpr double kStartToneHz = 800.0;
    constexpr double kStopToneHz = 400.0;
    constexpr int kDurationMs = 150;

    const double frequency = started ? kStartToneHz : kStopToneHz;
    sound_player_->PlayTone(frequency, kDurationMs, volume_slider_->value());
}

void MainWindow::OnProcessSelectionChanged()
{
    auto selected = process_tree_->selectedItems();
    if (selected.isEmpty())
    {
        selected_process_identity_ = {};
        target_pids_.Clear();
        return;
    }

    selected_process_identity_ = IdentityFromItem(selected.first());
    if (selected_process_identity_.IsValid())
    {
        target_pids_.SetSelectedPid(selected_process_identity_.pid);
    }
    else
    {
        target_pids_.Clear();
    }
}

void MainWindow::OnProcessFilterChanged(const QString& text)
{
    for (int i = 0; i < process_tree_->topLevelItemCount(); ++i)
    {
        FilterTreeItem(process_tree_->topLevelItem(i), text);
    }
}

void MainWindow::RefreshProcessList()
{
    if (refresh_in_flight_)
    {
        return;
    }

    refresh_in_flight_ = true;
    auto future = QtConcurrent::run(&platform::EnumerateProcesses);
    process_watcher_->setFuture(future);
}

void MainWindow::OnProcessListReady()
{
    refresh_in_flight_ = false;
    const auto processes = process_watcher_->result();

    uint32_t selected_pid = 0;
    auto selected = process_tree_->selectedItems();
    if (!selected.isEmpty())
    {
        selected_pid = selected.first()->text(0).toUInt();
    }

    std::unordered_set<uint32_t> expanded_pids;
    for (int i = 0; i < process_tree_->topLevelItemCount(); ++i)
    {
        auto* item = process_tree_->topLevelItem(i);
        if (item->isExpanded())
        {
            expanded_pids.insert(item->text(0).toUInt());
        }
    }

    process_tree_->clear();

    std::unordered_map<uint32_t, QTreeWidgetItem*> pid_to_item;
    for (const auto& proc : processes)
    {
        auto* item = new QTreeWidgetItem();
        item->setText(0, QString::number(proc.pid));
        item->setText(1, QString::fromStdString(proc.name));
        item->setData(0, kProcessPidRole, proc.pid);
        item->setData(0, kProcessCreationTimeRole, QString::number(proc.creation_time));
        item->setData(0, kProcessExePathRole, QString::fromStdWString(proc.exe_path));
        item->setIcon(1, IconForExePath(proc.exe_path));
        pid_to_item[proc.pid] = item;
    }

    QTreeWidgetItem* selected_item = nullptr;
    for (const auto& proc : processes)
    {
        auto* item = pid_to_item[proc.pid];
        auto parent_it = pid_to_item.find(proc.parent_pid);

        if (parent_it != pid_to_item.end() && proc.parent_pid != proc.pid)
        {
            parent_it->second->addChild(item);
        }
        else
        {
            process_tree_->addTopLevelItem(item);
        }

        if (proc.pid == selected_pid)
        {
            selected_item = item;
        }
    }

    for (int i = 0; i < process_tree_->topLevelItemCount(); ++i)
    {
        auto* item = process_tree_->topLevelItem(i);
        if (expanded_pids.contains(item->text(0).toUInt()))
        {
            item->setExpanded(true);
        }
    }

    if (selected_item != nullptr)
    {
        selected_item->setSelected(true);
        process_tree_->scrollToItem(selected_item);
    }

    OnProcessFilterChanged(process_filter_->text());
}

void MainWindow::UpdatePipelineStatus(const QString& message, bool is_error)
{
    status_label_->setText(message);
    if (is_error)
    {
        status_label_->setStyleSheet("color: red;");
    }
    else
    {
        status_label_->setStyleSheet("");
    }
}

QIcon MainWindow::IconForExePath(const std::wstring& exe_path)
{
    if (exe_path.empty())
    {
        return {};
    }

    auto it = icon_cache_.find(exe_path);
    if (it != icon_cache_.end())
    {
        return it->second;
    }

    const QFileIconProvider icon_provider;
    auto info = QFileInfo(QString::fromStdWString(exe_path));
    QIcon icon = icon_provider.icon(info);
    icon_cache_.emplace(exe_path, icon);
    return icon;
}

}  // namespace scrambler::ui
