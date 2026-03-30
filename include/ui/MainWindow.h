#pragma once

#include "core/EffectConfig.h"
#include "core/FlowTracker.h"
#include "core/PacketInterceptor.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>

#include <memory>

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

private:  // NOLINT(readability-redundant-access-specifiers)
    void SetupUi();
    void StartPipeline();
    void StopPipeline();
    void UpdateDriverStatus(const QString& message, bool is_error);

    // UI widgets
    QPushButton* start_stop_button_ = nullptr;
    QTreeWidget* process_tree_ = nullptr;
    QLineEdit* process_filter_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTimer* refresh_timer_ = nullptr;

    // Effect controls
    QSlider* delay_slider_ = nullptr;
    QSpinBox* delay_spinbox_ = nullptr;
    QSlider* drop_slider_ = nullptr;
    QLabel* drop_label_ = nullptr;
    QComboBox* direction_combo_ = nullptr;

    // Core pipeline
    core::TargetSet targets_;
    core::EffectConfig effects_;
    std::unique_ptr<core::FlowTracker> flow_tracker_;
    std::unique_ptr<core::PacketInterceptor> interceptor_;

    bool running_ = false;
};

}  // namespace scrambler::ui
