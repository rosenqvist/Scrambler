#pragma once

#include "core/EffectConfig.h"
#include "core/FlowTracker.h"
#include "core/PacketInterceptor.h"
#include "platform/ProcessEnumerator.h"
#include "ui/DiagnosticsTab.h"
#include "ui/HotkeyEdit.h"
#include "ui/HotkeyManager.h"
#include "ui/SoundPlayer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFutureWatcher>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>

#include <memory>
#include <string>
#include <unordered_map>

namespace scrambler::ui
{

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

private slots:
    void OnStartStopClicked();
    void OnProcessFilterChanged(const QString& text);
    void OnProcessSelectionChanged();
    void RefreshProcessList();
    void OnHotkeyTriggered(HotkeyAction action);
    // Invoked on the UI thread (via queued connection) when the core pipeline
    // exits due to a terminal driver error. Tears down the pipeline and
    // visually show the GLE in the status bar.
    void OnPipelineFatal(quint32 gle);

private:  // NOLINT(readability-redundant-access-specifiers)
    // --- Setup ---
    void SetupUi();
    void StartPipeline();
    void StopPipeline();
    void UpdateDriverStatus(const QString& message, bool is_error);
    void PlayToggleSound(bool started);
    void OnProcessListReady();
    QIcon IconToExePath(const std::wstring& exe_path);

    // --- Top-level layout ---
    QTabWidget* tab_widget_ = nullptr;
    QPushButton* start_stop_button_ = nullptr;
    QLabel* status_label_ = nullptr;

    // --- Process list ---
    QTreeWidget* process_tree_ = nullptr;
    QLineEdit* process_filter_ = nullptr;
    QTimer* refresh_timer_ = nullptr;
    QFutureWatcher<std::vector<platform::ProcessInfo>>* process_watcher_ = nullptr;
    bool refresh_in_flight_ = false;
    std::unordered_map<std::wstring, QIcon> icon_cache_;

    // --- Effect controls ---
    QSlider* delay_slider_ = nullptr;
    QSpinBox* delay_spinbox_ = nullptr;
    QComboBox* delay_direction_combo_ = nullptr;
    QSlider* drop_slider_ = nullptr;
    QSpinBox* drop_spinbox_ = nullptr;
    QComboBox* drop_direction_combo_ = nullptr;

    // --- Hotkey controls ---
    HotkeyEdit* toggle_hotkey_edit_ = nullptr;
    HotkeyEdit* inc_delay_hotkey_edit_ = nullptr;
    HotkeyEdit* dec_delay_hotkey_edit_ = nullptr;
    HotkeyEdit* inc_drop_hotkey_edit_ = nullptr;
    HotkeyEdit* dec_drop_hotkey_edit_ = nullptr;
    QSpinBox* delay_step_spinbox_ = nullptr;
    QSpinBox* drop_step_spinbox_ = nullptr;
    HotkeyManager* hotkey_manager_ = nullptr;

    // --- Sound ---
    QCheckBox* sound_checkbox_ = nullptr;
    QSlider* volume_slider_ = nullptr;
    QLabel* volume_label_ = nullptr;
    SoundPlayer* sound_player_ = nullptr;

    // --- Diagnostics ---
    DiagnosticsTab* diagnostics_tab_ = nullptr;

    // --- Core pipeline ---
    core::TargetSet targets_;
    core::EffectConfig effects_;
    std::unique_ptr<core::FlowTracker> flow_tracker_;
    std::unique_ptr<core::PacketInterceptor> interceptor_;
    bool running_ = false;
};

}  // namespace scrambler::ui
