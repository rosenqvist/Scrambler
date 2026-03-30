#include "ui/MainWindow.h"

#include "platform/ProcessEnumerator.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include <unordered_map>

namespace scrambler::ui
{

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), refresh_timer_(new QTimer(this))
{
    SetupUi();

    // Refresh process list every 3 seconds
    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::RefreshProcessList);
    refresh_timer_->start(3000);

    // Initial population
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
    setMinimumSize(500, 400);

    auto* central = new QWidget();
    auto* main_layout = new QVBoxLayout(central);

    // -Start/Stop button
    start_stop_button_ = new QPushButton("Start");
    start_stop_button_->setFixedHeight(36);
    connect(start_stop_button_, &QPushButton::clicked, this, &MainWindow::OnStartStopClicked);
    main_layout->addWidget(start_stop_button_);

    // Process list group
    auto* process_group = new QGroupBox("Processes");
    auto* process_layout = new QVBoxLayout(process_group);

    process_filter_ = new QLineEdit();
    process_filter_->setPlaceholderText("Filter by name or PID...");
    connect(process_filter_, &QLineEdit::textChanged, this, &MainWindow::OnProcessFilterChanged);
    process_layout->addWidget(process_filter_);

    process_tree_ = new QTreeWidget();
    process_tree_->setHeaderLabels({"PID", "Name"});
    process_tree_->setRootIsDecorated(true);
    process_tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    process_tree_->setSortingEnabled(true);
    process_tree_->sortByColumn(1, Qt::AscendingOrder);
    process_tree_->header()->setStretchLastSection(true);
    process_tree_->setColumnWidth(0, 80);
    connect(process_tree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::OnProcessSelectionChanged);
    process_layout->addWidget(process_tree_);

    main_layout->addWidget(process_group);

    // the status bar:
    status_label_ = new QLabel();
    statusBar()->addPermanentWidget(status_label_);

    setCentralWidget(central);
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
    // Check that a process is selected
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
}

void MainWindow::StopPipeline()
{
    if (!running_)
    {
        return;
    }

    // Order matters here so stop interceptor first so no new packets
    // get routed to the delay queue while it's shutting down
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
}

void MainWindow::OnProcessSelectionChanged()
{
    auto selected = process_tree_->selectedItems();
    if (selected.isEmpty())
    {
        return;
    }

    auto pid = selected.first()->text(0).toUInt();

    // Update the target set with the newly selected process
    targets_.Clear();
    targets_.Add(pid);
}

void MainWindow::OnProcessFilterChanged(const QString& text)
{
    // Walk the tree and show/hide based on filter text.
    // A parent should stay visible if any of its children match.
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
    // Remember which PID was selected
    uint32_t selected_pid = 0;
    auto selected = process_tree_->selectedItems();
    if (!selected.isEmpty())
    {
        selected_pid = selected.first()->text(0).toUInt();
    }

    process_tree_->clear();

    auto processes = platform::EnumerateProcesses();

    // Build a lookup table so we can find which PIDs exist in this snapshot
    std::unordered_map<uint32_t, int> pid_to_index;
    for (int i = 0; i < static_cast<int>(processes.size()); ++i)
    {
        pid_to_index[processes[static_cast<std::size_t>(i)].pid] = i;
    }

    // First pass: create a tree item for every process
    std::unordered_map<uint32_t, QTreeWidgetItem*> pid_to_item;
    for (const auto& proc : processes)
    {
        auto* item = new QTreeWidgetItem();
        item->setText(0, QString::number(proc.pid));
        item->setText(1, QString::fromStdString(proc.name));
        item->setData(0, Qt::UserRole, proc.pid);
        pid_to_item[proc.pid] = item;
    }

    // Second pass that attach children to their parents.
    // If a process's parent isn't in the snapshot (dead or filtered out)
    // it becomes a top-level item.
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

    // Restore selection and make sure the item is visible
    if (selected_item != nullptr)
    {
        selected_item->setSelected(true);
        process_tree_->scrollToItem(selected_item);
    }

    // Reapply the filter after repopulating the process table
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
