#include "ui/MainWindow.h"

#include "platform/ProcessEnumerator.h"

#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QVBoxLayout>

#include <windows.h>

#include <cmath>
#include <numbers>
#include <unordered_map>

namespace scrambler::ui
{

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), refresh_timer_(new QTimer(this)), hotkey_manager_(new HotkeyManager(this))
{
    SetupUi();

    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::RefreshProcessList);
    refresh_timer_->start(3000);

    RefreshProcessList();
    UpdateDriverStatus("Stopped", false);
}

MainWindow::~MainWindow()
{
    StopPipeline();
}

void MainWindow::SetupUi()
{
    setWindowTitle("Scrambler");
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
    process_layout->addWidget(process_tree_);

    network_layout->addWidget(process_group);

    // Start/Stop:
    start_stop_button_ = new QPushButton("Start");
    start_stop_button_->setFixedHeight(36);
    network_layout->addWidget(start_stop_button_);

    // Network conditions:
    auto* effects_group = new QGroupBox("Network Conditions");
    auto* effects_layout = new QVBoxLayout(effects_group);

    auto make_direction_combo = []() -> QComboBox*
    {
        auto* combo = new QComboBox();
        combo->addItem("Out", static_cast<int>(core::Direction::kOutbound));
        combo->addItem("In", static_cast<int>(core::Direction::kInbound));
        combo->addItem("Both", static_cast<int>(core::Direction::kBoth));
        combo->setCurrentIndex(2);
        combo->setFixedWidth(60);
        return combo;
    };

    // Delay row:
    auto* delay_layout = new QHBoxLayout();
    delay_layout->addWidget(new QLabel("Delay (ms):"));
    delay_slider_ = new QSlider(Qt::Horizontal);
    delay_slider_->setRange(0, 1000);
    delay_slider_->setValue(0);
    delay_spinbox_ = new QSpinBox();
    delay_spinbox_->setRange(0, 1000);
    delay_spinbox_->setSuffix(" ms");
    delay_direction_combo_ = make_direction_combo();
    delay_layout->addWidget(delay_slider_);
    delay_layout->addWidget(delay_spinbox_);
    delay_layout->addWidget(delay_direction_combo_);
    effects_layout->addLayout(delay_layout);

    // Drop rate row:
    auto* drop_layout = new QHBoxLayout();
    drop_layout->addWidget(new QLabel("Drop rate:"));
    drop_slider_ = new QSlider(Qt::Horizontal);
    drop_slider_->setRange(0, 100);
    drop_slider_->setValue(0);
    drop_label_ = new QLabel("0%");
    drop_label_->setFixedWidth(40);
    drop_direction_combo_ = make_direction_combo();
    drop_layout->addWidget(drop_slider_);
    drop_layout->addWidget(drop_label_);
    drop_layout->addWidget(drop_direction_combo_);
    effects_layout->addLayout(drop_layout);

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
            emit edit->BindingChanged({});
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
    int saved_volume = settings.value("Volume", 25).toInt();
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

    connect(delay_slider_, &QSlider::valueChanged, delay_spinbox_, &QSpinBox::setValue);
    connect(delay_spinbox_, qOverload<int>(&QSpinBox::valueChanged), delay_slider_, &QSlider::setValue);
    connect(delay_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.delay_ms.store(value);
    });

    connect(drop_slider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
    {
        effects_.drop_rate.store(static_cast<float>(value) / 100.0F);
        drop_label_->setText(QString::number(value) + "%");
    });

    connect(delay_direction_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int index)
    {
        auto dir = static_cast<core::Direction>(delay_direction_combo_->itemData(index).toInt());
        effects_.delay_direction.store(dir);
    });

    connect(drop_direction_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int index)
    {
        auto dir = static_cast<core::Direction>(drop_direction_combo_->itemData(index).toInt());
        effects_.drop_direction.store(dir);
    });

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
            emit edit->BindingChanged({});
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

    flow_tracker_ = std::make_unique<core::FlowTracker>();
    if (!flow_tracker_->Start())
    {
        UpdateDriverStatus("FlowTracker failed to start (run as admin?)", true);
        flow_tracker_.reset();
        return;
    }

    interceptor_ = std::make_unique<core::PacketInterceptor>(*flow_tracker_, targets_, effects_);
    if (!interceptor_->Start())
    {
        UpdateDriverStatus("PacketInterceptor failed to start", true);
        flow_tracker_->Stop();
        flow_tracker_.reset();
        return;
    }

    running_ = true;
    start_stop_button_->setText("Stop");
    UpdateDriverStatus("Running", false);
    PlayToggleSound(true);
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

    targets_.Clear();

    running_ = false;
    start_stop_button_->setText("Start");
    UpdateDriverStatus("Stopped", false);
    PlayToggleSound(false);
}

void MainWindow::OnHotkeyTriggered(HotkeyAction action)
{
    switch (action)
    {
        case HotkeyAction::kToggleEffects:
            OnStartStopClicked();
            break;
        case HotkeyAction::kIncrementDelay:
            delay_slider_->setValue(delay_slider_->value() + delay_step_spinbox_->value());
            break;
        case HotkeyAction::kDecrementDelay:
            delay_slider_->setValue(delay_slider_->value() - delay_step_spinbox_->value());
            break;
        case HotkeyAction::kIncrementDropRate:
            drop_slider_->setValue(drop_slider_->value() + drop_step_spinbox_->value());
            break;
        case HotkeyAction::kDecrementDropRate:
            drop_slider_->setValue(drop_slider_->value() - drop_step_spinbox_->value());
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

    int volume = volume_slider_->value();
    sound_future_ = std::async(std::launch::async,
                               [started, volume]
    {
        constexpr DWORD kSampleRate = 44100;
        constexpr DWORD kDurationMs = 150;
        constexpr DWORD kNumSamples = kSampleRate * kDurationMs / 1000;
        constexpr double kStartToneHz = 800.0;
        constexpr double kStopToneHz = 400.0;

        double frequency = started ? kStartToneHz : kStopToneHz;
        double amplitude = static_cast<double>(volume) / 100.0;

        std::array<int16_t, kNumSamples> samples{};
        for (DWORD i = 0; i < kNumSamples; ++i)
        {
            double t = static_cast<double>(i) / static_cast<double>(kSampleRate);
            double sample = std::sin(2.0 * std::numbers::pi * frequency * t) * amplitude;
            samples.at(i) = static_cast<int16_t>(sample * 32767.0);
        }

        WAVEFORMATEX wfx{};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 1;
        wfx.nSamplesPerSec = kSampleRate;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

        HWAVEOUT hwo = nullptr;
        if (waveOutOpen(&hwo, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return;
        }

        WAVEHDR hdr{};
        hdr.lpData = reinterpret_cast<LPSTR>(samples.data());
        hdr.dwBufferLength = static_cast<DWORD>(samples.size() * sizeof(int16_t));

        waveOutPrepareHeader(hwo, &hdr, sizeof(hdr));
        waveOutWrite(hwo, &hdr, sizeof(hdr));

        // Wait for playback to finish
        while ((hdr.dwFlags & WHDR_DONE) == 0)
        {
            Sleep(1);
        }

        waveOutUnprepareHeader(hwo, &hdr, sizeof(hdr));
        waveOutClose(hwo);
    });
}

void MainWindow::OnProcessSelectionChanged()
{
    auto selected = process_tree_->selectedItems();
    if (selected.isEmpty())
    {
        return;
    }

    auto pid = selected.first()->text(0).toUInt();
    targets_.SetSingle(pid);
}

void MainWindow::OnProcessFilterChanged(const QString& text)
{
    for (int i = 0; i < process_tree_->topLevelItemCount(); ++i)
    {
        auto* parent = process_tree_->topLevelItem(i);
        bool any_child_matches = false;

        for (int j = 0; j < parent->childCount(); ++j)
        {
            auto* child = parent->child(j);
            bool child_matches = text.isEmpty() || child->text(0).contains(text, Qt::CaseInsensitive)
                                 || child->text(1).contains(text, Qt::CaseInsensitive);
            child->setHidden(!child_matches);
            if (child_matches)
            {
                any_child_matches = true;
            }
        }

        bool parent_matches = text.isEmpty() || parent->text(0).contains(text, Qt::CaseInsensitive)
                              || parent->text(1).contains(text, Qt::CaseInsensitive);
        parent->setHidden(!parent_matches && !any_child_matches);
    }
}

void MainWindow::RefreshProcessList()
{
    // Save selected PID
    uint32_t selected_pid = 0;
    auto selected = process_tree_->selectedItems();
    if (!selected.isEmpty())
    {
        selected_pid = selected.first()->text(0).toUInt();
    }

    // Save expanded PIDs
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

    auto processes = platform::EnumerateProcesses();

    std::unordered_map<uint32_t, int> pid_to_index;
    for (int i = 0; i < static_cast<int>(processes.size()); ++i)
    {
        pid_to_index[processes[static_cast<std::size_t>(i)].pid] = i;
    }

    QFileIconProvider icon_provider;

    std::unordered_map<uint32_t, QTreeWidgetItem*> pid_to_item;
    for (const auto& proc : processes)
    {
        auto* item = new QTreeWidgetItem();
        item->setText(0, QString::number(proc.pid));
        item->setText(1, QString::fromStdString(proc.name));
        item->setData(0, Qt::UserRole, proc.pid);

        if (!proc.exe_path.empty())
        {
            auto info = QFileInfo(QString::fromStdWString(proc.exe_path));
            item->setIcon(1, icon_provider.icon(info));
        }

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

    // Restore expanded state
    for (int i = 0; i < process_tree_->topLevelItemCount(); ++i)
    {
        auto* item = process_tree_->topLevelItem(i);
        if (expanded_pids.contains(item->text(0).toUInt()))
        {
            item->setExpanded(true);
        }
    }

    // Restore selection
    if (selected_item != nullptr)
    {
        selected_item->setSelected(true);
        process_tree_->scrollToItem(selected_item);
    }

    OnProcessFilterChanged(process_filter_->text());
}

void MainWindow::UpdateDriverStatus(const QString& message, bool is_error)
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

}  // namespace scrambler::ui
