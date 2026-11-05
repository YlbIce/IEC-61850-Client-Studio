#include "main_window.h"
#include "connection_dialog.h"
#include "control_dialog.h"
#include "history_panel.h"
#include "monitor_panel.h"
#include "report_dialog.h"
#include "studio_style.h"
#include "test_result_dialog.h"
#include <QtWidgets>

namespace {
QWidget* section(const QString& title, QWidget* content, QWidget* accessory = nullptr) {
    auto panel = new QWidget;
    panel->setObjectName("sectionPanel");
    auto layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto heading = new QLabel(title);
    heading->setObjectName("sectionTitle");
    if (accessory) {
        auto bar = new QHBoxLayout;
        bar->setContentsMargins(0, 0, 8, 0);
        bar->addWidget(heading, 1);
        bar->addWidget(accessory);
        layout->addLayout(bar);
    } else
        layout->addWidget(heading);
    layout->addWidget(content);
    return panel;
}
QTableView* pointTable(PointModel* model) {
    auto table = new QTableView;
    table->setModel(model);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setWordWrap(false);
    table->verticalHeader()->setDefaultSectionSize(32);
    table->horizontalHeader()->setStretchLastSection(false);
    for (int i = 0; i < 7; ++i)
        table->setColumnWidth(i, QList<int>{250, 140, 140, 145, 185, 110, 230}[i]);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    return table;
}
} // namespace

MainWindow::MainWindow() {
    StudioStyle::apply();
    setWindowIcon(QIcon(":/lite/app.png"));
    setWindowTitle("IEC 61850 Client Lite — Qt 轻量版");
    resize(1280, 800);
    setMinimumSize(820, 540);
    session_ = new Session;
    session_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, session_, &QObject::deleteLater);
    connect(this, &MainWindow::command, session_, &Session::execute);
    connect(session_, &Session::result, this, &MainWindow::receive);
    connect(session_, &Session::failed, this, [this](QString op, QString message) {
        busy_ = false;
        loadingModel_ = false;
        if (progress_) {
            progress_->close();
            progress_->deleteLater();
            progress_ = nullptr;
        }
        log_->append("错误", endpoint_ + " | " + op + "：" + message);
        statusBar()->showMessage(message);
        if (op == "connect" || message.contains("连接已断开")) {
            connected_ = false;
            timer_.stop();
            updateConnectionBadge(false);
        }
        emit error(op, message);
    });
    connect(session_, &Session::event, this, &MainWindow::addLog);
    connect(session_, &Session::connectionProgress, this, [this](int percent, QString phase) {
        if (progress_) {
            progress_->setLabelText(phase);
            if (percent >= 0)
                progress_->setValue(qMin(percent, 99));
        }
        statusBar()->showMessage(phase);
    });
    connect(session_, &Session::connectionLost, this, [this] {
        connected_ = false;
        timer_.stop();
        updateConnectionBadge(false);
        statusBar()->showMessage("MMS 连接已断开，监视已暂停");
    });
    thread_.start();

    auto connection = menuBar()->addMenu("连接(&C)");
    auto toolbar = addToolBar("主工具栏");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setIconSize(QSize(18, 18));
    auto brandIcon = new QLabel;
    brandIcon->setObjectName("brandIcon");
    brandIcon->setAlignment(Qt::AlignCenter);
    auto logo =
        QIcon(":/lite/app.png").pixmap(qRound(34 * devicePixelRatioF()), qRound(34 * devicePixelRatioF()));
    logo.setDevicePixelRatio(devicePixelRatioF());
    brandIcon->setPixmap(logo);
    brandIcon->setFixedSize(34, 34);
    toolbar->addWidget(brandIcon);
    auto brandName = new QLabel("CLIENT STUDIO  /  LITE");
    brandName->setObjectName("brandName");
    toolbar->addWidget(brandName);
    toolbar->addSeparator();
    auto connectAction = connection->addAction(StudioStyle::icon("connect"), "连接 IED…", this,
                                               &MainWindow::connectDialog, QKeySequence("Ctrl+O"));
    auto disconnect = connection->addAction(StudioStyle::icon("disconnect"), "断开", this,
                                            [this] { request("disconnect"); });
    connection->addSeparator();
    connection->addAction("退出", this, &QWidget::close, QKeySequence("Alt+F4"));
    toolbar->addAction(connectAction);
    toolbar->addAction(disconnect);
    toolbar->addSeparator();
    auto view = menuBar()->addMenu("视图(&V)");
    auto refresh = view->addAction(
        StudioStyle::icon("refresh"), "刷新在线模型", this, [this] { request("discover"); },
        QKeySequence("F5"));
    toolbar->addAction(refresh);
    auto spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    connectionBadge_ = new QLabel("●  未连接");
    connectionBadge_->setObjectName("connectionBadge");
    toolbar->addWidget(connectionBadge_);
    auto poll = view->addAction("自动刷新当前数据集（1 秒）");
    poll->setCheckable(true);
    poll->setChecked(true);
    connect(poll, &QAction::toggled, this, [this](bool enabled) {
        autoRefresh_ = enabled;
        if (enabled && connected_)
            timer_.start();
        else
            timer_.stop();
    });
    auto testMenu = menuBar()->addMenu("测试标记(&T)");
    testMenu->addAction("整个数据集标记成功（含筛选隐藏行）", this, [this] { points_->markAll(true); });
    testMenu->addAction("清除当前数据集已测标记", this, [this] {
        if (QMessageBox::question(this, "清除测试记录",
                                  "清除整个数据集的测试结果及失败原因（包含筛选隐藏行）？") ==
            QMessageBox::Yes)
            points_->markAll(false);
    });
    auto help = menuBar()->addMenu("帮助(&H)");
    help->addAction("关于", this, [this] {
        QMessageBox::about(this, "关于",
                           "IEC 61850 Client Lite 0.1.6\nQt 5.15 Widgets / "
                           "libiec61850\n在线模型、数据集、报告参数、遥控、已测标记\n\n已测为人工测试记录，与"
                           "质量 Test 位无关。\nGPL-3.0-or-later；Qt 按随包许可证分发。");
    });

    tree_ = new QTreeWidget;
    tree_->setHeaderLabel("在线模型 · 按需展开");
    tree_->setUniformRowHeights(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tabs_ = new QTabWidget;
    auto dsSplit = new QSplitter;
    datasets_ = new QListWidget;
    datasets_->setMinimumWidth(180);
    datasets_->setUniformItemSizes(true);
    datasets_->setToolTip("在线数据集列表，选中后自动读取数据对象");
    dsSplit->addWidget(section("数据集目录", datasets_));
    points_ = new PointModel(this);
    filteredPoints_ = new TestFilterModel(this);
    filteredPoints_->setSourceModel(points_);
    table_ = pointTable(points_);
    table_->setModel(filteredPoints_);
    table_->setObjectName("datasetTable");
    auto filterPanel = new QWidget;
    auto filterLayout = new QHBoxLayout(filterPanel);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    auto filter = new QComboBox;
    filter->setObjectName("testFilter");
    for (auto pair : QList<QPair<QString, QString>>{{"全部", "all"},
                                                    {"未测", "untested"},
                                                    {"已测（全部）", "done"},
                                                    {"已测成功", "success"},
                                                    {"已测失败", "failure"},
                                                    {"已测待确认", "tested"}})
        filter->addItem(pair.first, pair.second);
    filter->setToolTip("按测试结果筛选；右键或双击结果列可编辑失败原因");
    testCounts_ = new QLabel;
    filterLayout->addWidget(testCounts_);
    filterLayout->addWidget(new QLabel("筛选"));
    filterLayout->addWidget(filter);
    connect(filter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, filter] { filteredPoints_->setStatus(filter->currentData().toString()); });
    connect(points_, &PointModel::testResultsChanged, this, &MainWindow::updateTestCounts);
    dsSplit->addWidget(section("数据对象 · 测试记录", table_, filterPanel));
    dsSplit->setSizes({240, 1040});
    dsSplit->setContentsMargins(8, 8, 8, 8);
    dsSplit->setHandleWidth(8);
    dsSplit->setChildrenCollapsible(false);
    tabs_->addTab(dsSplit, "数据集");
    tabs_->addTab(tree_, "在线模型");
    auto reportSplit = new QSplitter;
    reports_ = new QListWidget;
    reportSplit->addWidget(section("报告控制块目录", reports_));
    reportSplit->setContentsMargins(8, 8, 8, 8);
    reportSplit->setHandleWidth(8);
    auto reportPanel = new QWidget;
    auto rLayout = new QVBoxLayout(reportPanel);
    reportInfo_ = new QLabel(
        "选中报告控制块，读取并设置报告参数。\n支持 BRCB / URCB、触发条件、可选字段、GI、使能与预留。");
    reportInfo_->setWordWrap(true);
    auto editReport = new QPushButton("读取 / 设置参数…");
    editReport->setProperty("primary", true);
    rLayout->setContentsMargins(22, 22, 22, 22);
    rLayout->setSpacing(20);
    rLayout->addWidget(reportInfo_);
    rLayout->addWidget(editReport, 0, Qt::AlignLeft);
    rLayout->addStretch();
    reportSplit->addWidget(section("报告参数", reportPanel));
    reportSplit->setSizes({340, 900});
    tabs_->addTab(reportSplit, "报告控制块");
    monitors_ = new MonitorPanel;
    tabs_->addTab(monitors_, "监视对象");
    connect(monitors_, &MonitorPanel::removeRequested, this,
            [this](QString ref) { request("watchRemove", {{"ref", ref}}); });
    connect(monitors_, &MonitorPanel::clearRequested, this, [this] { request("watchClear"); });
    connect(session_, &Session::monitorSnapshot, monitors_, &MonitorPanel::updateSnapshot);
    connect(session_, &Session::monitoringChanged, this, [this](QStringList refs) {
        watched_ = QSet<QString>(refs.begin(), refs.end());
        monitors_->setReferences(refs);
        tabs_->setTabText(3, QString("监视对象 (%1)").arg(refs.size()));
    });
    auto openReport = [this] {
        if (auto item = reports_->currentItem())
            request("report", {{"ref", item->text()}});
    };
    connect(editReport, &QPushButton::clicked, this, openReport);
    connect(reports_, &QListWidget::itemDoubleClicked, this,
            [openReport](QListWidgetItem*) { openReport(); });
    reports_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(reports_, &QListWidget::customContextMenuRequested, this, [this, openReport](QPoint p) {
        if (auto item = reports_->itemAt(p)) {
            reports_->setCurrentItem(item);
            QMenu m;
            m.addAction("读取 / 设置参数…", openReport);
            m.exec(reports_->viewport()->mapToGlobal(p));
        }
    });
    setCentralWidget(tabs_);
    auto dock = new QDockWidget("通信与监视日志", this);
    history_ = new HistoryPanel;
    log_ = history_->eventLog();
    dock->setWidget(history_);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    resizeDocks({dock}, {290}, Qt::Vertical);
    connect(session_, &Session::attributeChanged, log_, &EventLog::change);
    connect(session_, &Session::reportBatch, history_,
            [this](QString device, QVariantList changes, QVariantMap datasets) {
                history_->model()->samples(device, changes, "MMS 报告");
                if (device == endpoint_ && datasets.contains(selectedDataset_) && !loadingModel_)
                    points_->update(endpoint_, selectedDataset_, datasets[selectedDataset_].toList());
            });
    connect(session_, &Session::monitorSnapshot, history_,
            [this](QString, QVariantMap snapshot, QString error) {
                if (error.isEmpty() && snapshot.value("source") != "MMS 报告")
                    history_->model()->samples(endpoint_, snapshot["rows"].toList(), "监视采样");
            });
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, history_,
            [this](QModelIndex index) {
                history_->selectObject(
                    endpoint_, index.isValid() ? points_->reference(filteredPoints_->mapToSource(index).row())
                                               : QString());
            });
    connect(tree_, &QTreeWidget::currentItemChanged, history_, [this](QTreeWidgetItem* item) {
        if (item && item->data(0, Qt::UserRole + 1) == "DO")
            history_->selectObject(endpoint_, item->data(0, Qt::UserRole).toString());
    });
    connect(monitors_, &MonitorPanel::objectSelected, history_,
            [this](QString ref) { history_->selectObject(endpoint_, ref); });
    connect(session_, &Session::failed, history_, [this](QString op, QString error) {
        if (op == "connect" || error.contains("连接已断开"))
            history_->model()->link(endpoint_, "连接失败 / 断开", op + ": " + error);
    });
    connect(session_, &Session::connectionLost, history_, [this] {
        history_->model()->link(endpoint_, "意外断连", "MMS 关联关闭通知");
        history_->model()->resetBaseline();
    });

    connect(points_, &PointModel::resultRecorded, this, [this](QString ref, QString status, QString reason) {
        log_->append("测试记录",
                     endpoint_ + " | " + ref + " | " + status + (reason.isEmpty() ? "" : " | " + reason));
    });
    view->addAction(dock->toggleViewAction());
    connect(points_, &PointModel::storageError, this, [this](QString error) {
        addLog(error);
        statusBar()->showMessage(error);
    });

    connect(tree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole + 1) == "LN" && !item->data(0, Qt::UserRole + 2).toBool())
            request("objects", {{"ref", item->data(0, Qt::UserRole)}});
    });
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](QPoint p) {
        auto item = tree_->itemAt(p);
        if (!item)
            return;
        tree_->setCurrentItem(item);
        QString kind = item->data(0, Qt::UserRole + 1).toString();
        QString ref = item->data(0, Qt::UserRole).toString();
        QMenu menu;
        if (kind == "LN")
            menu.addAction("加载数据对象", [this, ref] { request("objects", {{"ref", ref}}); });
        if (kind == "DO") {
            menu.addAction("读取值 / 质量 / 时标 / dU", [this, ref] { request("object", {{"ref", ref}}); });
            menu.addAction(watched_.contains(ref) ? "移除监视" : "加入监视", [this, ref] {
                request(watched_.contains(ref) ? "watchRemove" : "watchAdd", {{"ref", ref}});
            });
            menu.addAction("遥控…",
                           [this, ref] { request("control", {{"ref", ref}, {"action", "inspect"}}); });
        }
        menu.exec(tree_->viewport()->mapToGlobal(p));
    });
    connect(table_, &QTableView::customContextMenuRequested, this, [this](QPoint p) {
        auto index = table_->indexAt(p);
        if (!index.isValid())
            return;
        table_->selectRow(index.row());
        QPersistentModelIndex source = filteredPoints_->mapToSource(index);
        QString ref = points_->reference(source.row());
        QMenu menu;
        menu.addAction("读取对象", [this, ref] { request("object", {{"ref", ref}}); });
        menu.addAction(watched_.contains(ref) ? "移除监视" : "加入监视", [this, ref] {
            request(watched_.contains(ref) ? "watchRemove" : "watchAdd", {{"ref", ref}});
        });
        menu.addAction("遥控…", [this, ref] { request("control", {{"ref", ref}, {"action", "inspect"}}); });
        menu.addSeparator();
        menu.addAction("标记已测成功", [this, source] {
            if (source.isValid())
                points_->setTestResult(source.row(), "success");
        });
        menu.addAction("标记已测失败…", [this, source] { editTestResult(source, "failure"); });
        menu.addAction("编辑测试结果 / 失败原因…", [this, source] { editTestResult(source); });
        menu.addAction("重置为未测", [this, source] {
            if (source.isValid())
                points_->setTestResult(source.row(), "untested");
        });
        menu.addAction("复制对象引用", [ref] { QApplication::clipboard()->setText(ref); });
        menu.exec(table_->viewport()->mapToGlobal(p));
    });
    connect(table_, &QTableView::doubleClicked, this, [this](QModelIndex index) {
        if (index.column() >= 5)
            editTestResult(filteredPoints_->mapToSource(index));
    });
    connect(datasets_, &QListWidget::currentTextChanged, this, [this](QString ref) {
        selectedDataset_ = ref;
        points_->update(endpoint_, ref, {});
        refreshValues();
    });
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, &MainWindow::refreshValues);
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { refreshValues(); });
    statusBar()->showMessage("未连接 · 请连接 IED 后选择数据集");
    auto storageHint = new QLabel("已测记录自动保存到本机");
    statusBar()->addPermanentWidget(storageHint);
    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    updateTestCounts();
}
void MainWindow::updateConnectionBadge(bool connected) {
    connectionBadge_->setText(connected ? "●  已连接" : "●  未连接");
    connectionBadge_->setToolTip(connected ? endpoint_ : "尚未建立 MMS 连接");
    connectionBadge_->setProperty("connected", connected);
    connectionBadge_->style()->unpolish(connectionBadge_);
    connectionBadge_->style()->polish(connectionBadge_);
}
MainWindow::~MainWindow() {
    QSettings().setValue("window/geometry", saveGeometry());
    timer_.stop();
    thread_.requestInterruption();
    QMetaObject::invokeMethod(session_, "shutdown", Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}
void MainWindow::addLog(const QString& message) {
    log_->append(message.contains("失败") || message.contains("错误") ? "错误" : "通信",
                 endpoint_.isEmpty() ? message : endpoint_ + " | " + message);
}
bool MainWindow::request(const QString& operation, QVariantMap args) {
    if (busy_) {
        if (operation != "dataset")
            statusBar()->showMessage("正在通信，请稍后重试");
        return false;
    }
    if (operation == "connect")
        endpoint_ = args["host"].toString().trimmed().toLower() + ':' + args["port"].toString();
    if (operation == "connect") {
        history_->model()->resetBaseline();
        history_->model()->link(endpoint_, "开始连接", "请求建立 MMS 关联");
    }
    if (operation == "connect" || operation == "discover") {
        loadingModel_ = true;
        timer_.stop();
        progress_ = new QProgressDialog("准备连接与模型读取", QString(), 0, 100, this);
        progress_->setObjectName("connectionProgress");
        progress_->setWindowTitle("连接与初始化进度");
        progress_->setCancelButton(nullptr);
        progress_->setAutoClose(false);
        progress_->setMinimumDuration(0);
        progress_->setWindowModality(Qt::WindowModal);
        progress_->setMinimumWidth(520);
        progress_->setValue(0);
        progress_->show();
    }
    busy_ = true;
    emit command(operation, args);
    return true;
}
void MainWindow::connectDialog() {
    ConnectionDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
        request("connect", dialog.arguments());
}
void MainWindow::refreshValues() {
    if (connected_ && !busy_ && !loadingModel_ && !progress_ && !selectedDataset_.isEmpty() &&
        tabs_->currentIndex() == 0 && !QApplication::activeModalWidget() &&
        !QApplication::activePopupWidget())
        request("dataset", {{"ref", selectedDataset_}});
}
void MainWindow::receive(QString operation, QVariantMap data) {
    busy_ = false;
    if (operation == "connect" || operation == "discover") {
        loadingModel_ = true;
        QSignalBlocker datasetSignals(datasets_);
        if (operation == "connect")
            history_->model()->link(endpoint_, "连接成功", "关联与在线模型读取完成");
        updateConnectionBadge(true);
        connected_ = true;
        tree_->clear();
        nodes_.clear();
        datasets_->clear();
        reports_->clear();
        for (auto value : data["nodes"].toList()) {
            auto node = value.toMap();
            QString ld = node["ld"].toString(), ref = node["ref"].toString();
            if (!nodes_.contains(ld)) {
                auto root = new QTreeWidgetItem(tree_, {ld});
                nodes_[ld] = root;
                root->setExpanded(true);
            }
            auto item = new QTreeWidgetItem(nodes_[ld], {node["name"].toString()});
            nodes_[ref] = item;
            item->setData(0, Qt::UserRole, ref);
            item->setData(0, Qt::UserRole + 1, "LN");
            item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        }
        datasets_->addItems(data["datasets"].toStringList());
        reports_->addItems(data["reports"].toStringList());
        // Selecting a dataset starts one read after this discovery response.
        if (autoRefresh_)
            timer_.start();
        statusBar()->showMessage(endpoint_ + " 已连接");
        addLog(data["partial"].toBool() ? "MMS 已连接，模型未完整读取；可刷新模型重试，无需重新连接"
                                        : "在线模型读取完成，数据属性按需读取");
        selectedDataset_ = datasets_->currentItem() ? datasets_->currentItem()->text() : QString();
        loadingModel_ = false;
        if (progress_) {
            progress_->setValue(100);
            progress_->close();
            progress_->deleteLater();
            progress_ = nullptr;
        }
        QTimer::singleShot(0, this, &MainWindow::refreshValues);
    } else if (operation == "disconnect") {
        history_->model()->link(endpoint_, "主动断开", "用户断开连接");
        history_->model()->resetBaseline();
        updateConnectionBadge(false);
        connected_ = false;
        timer_.stop();
        tree_->clear();
        nodes_.clear();
        datasets_->clear();
        reports_->clear();
        points_->update({}, {}, {});
        statusBar()->showMessage("已断开");
    } else if (operation == "objects") {
        if (auto parent = nodes_.value(data["ref"].toString(), nullptr)) {
            qDeleteAll(parent->takeChildren());
            parent->setData(0, Qt::UserRole + 2, true);
            for (auto name : data["names"].toStringList()) {
                auto item = new QTreeWidgetItem(parent, {name});
                item->setData(0, Qt::UserRole, data["ref"].toString() + '.' + name);
                item->setData(0, Qt::UserRole + 1, "DO");
            }
            parent->setExpanded(true);
        }
    } else if (operation == "dataset") {
        history_->model()->samples(endpoint_, data["rows"].toList(), "数据集读取");
        if (data["ref"].toString() == selectedDataset_) {
            points_->update(endpoint_, selectedDataset_, data["rows"].toList());
            statusBar()->showMessage(QString("%1 · %2 个数据对象 · 更新 %3")
                                         .arg(endpoint_)
                                         .arg(points_->rowCount())
                                         .arg(QTime::currentTime().toString("HH:mm:ss")));
        }
    } else if (operation == "object") {
        history_->model()->samples(endpoint_, data["rows"].toList(), "手动读取");
        auto dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(data["ref"].toString());
        dialog->resize(1100, 300);
        auto model = new PointModel(dialog);
        model->update(endpoint_, {}, data["rows"].toList());
        auto layout = new QVBoxLayout(dialog);
        layout->addWidget(pointTable(model));
        dialog->show();
    } else if (operation == "report")
        reportDialog(data);
    else if (operation == "saveReport") {
        statusBar()->showMessage(data["enabled"].toBool() ? "报告已启用，后台持续接收"
                                                          : "报告已停用，恢复按需读取");
        addLog("报告参数已回读：" + data["ref"].toString());
        reportInfo_->setText(
            QString("%1\nRptID: %2\n数据集: %3\n使能: %4\nBufTm: %5 ms / IntgPd: %6 ms\nConfRev: %7")
                .arg(data["ref"].toString(), data["rptId"].toString(), data["dataset"].toString(),
                     data["enabled"].toBool() ? "是" : "否")
                .arg(data["bufTm"].toUInt())
                .arg(data["intgPd"].toUInt())
                .arg(data["confRev"].toUInt()));
    } else if (operation == "control" && data["action"] == "inspect")
        controlDialog(data);
    emit completed(operation, data);
}

void MainWindow::reportDialog(QVariantMap data) {
    QStringList references;
    for (int i = 0; i < datasets_->count(); ++i)
        references << datasets_->item(i)->text();
    auto dialog = new ReportDialog(data, references, this);
    connect(dialog, &ReportDialog::applyRequested, this, [this, dialog](QVariantMap args) {
        if (request("saveReport", args))
            dialog->accept();
    });
    dialog->show();
}

void MainWindow::controlDialog(QVariantMap data) {
    auto dialog = new ControlDialog(data, this);
    connect(dialog, &ControlDialog::commandRequested, this, [this, dialog](QVariantMap args) {
        if (!request("control", args))
            dialog->handleError("control", "正在通信，请稍后重试");
    });
    connect(this, &MainWindow::completed, dialog, &ControlDialog::handleCompleted);
    connect(this, &MainWindow::error, dialog, &ControlDialog::handleError);
    dialog->show();
}

void MainWindow::updateTestCounts() {
    int success = 0, failure = 0, untested = 0, legacy = 0;
    for (int row = 0; row < points_->rowCount(); ++row) {
        auto status = points_->testRecord(row)["status"].toString();
        if (status == "success")
            ++success;
        else if (status == "failure")
            ++failure;
        else if (status == "tested")
            ++legacy;
        else
            ++untested;
    }
    testCounts_->setText(QString("成功 %1  失败 %2  未测 %3%4")
                             .arg(success)
                             .arg(failure)
                             .arg(untested)
                             .arg(legacy ? QString("  待确认 %1").arg(legacy) : ""));
}

void MainWindow::editTestResult(const QModelIndex& index, QString initialStatus) {
    if (!index.isValid())
        return;
    auto dialog = new TestResultDialog(points_, index, initialStatus, this);
    dialog->show();
}
