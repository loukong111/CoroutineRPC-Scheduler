#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QTabWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QFrame>
#include <QMenuBar>
#include <QMessageBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QSplitter>
#include <QStringList>
#include <QVector>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>
#include "corpcron/rpc/protocol.hpp"
#include "rpc.pb.h"

static QByteArray encode(uint32_t serialId, const std::string& payload) {
    std::string frame;
    if (!corpcron::rpc::tryEncode(serialId, payload, frame)) {
        return {};
    }
    return QByteArray(frame.data(), static_cast<int>(frame.size()));
}

static void setButtonRole(QPushButton *button, const char *role) {
    button->setProperty("role", role);
}

static QFrame* createStatCard(const QString& title, QLabel*& valueLabel, const QString& hint = QString()) {
    auto *card = new QFrame;
    card->setProperty("class", "statCard");
    auto *layout = new QVBoxLayout(card);
    auto *titleLabel = new QLabel(title);
    titleLabel->setProperty("class", "statTitle");
    valueLabel = new QLabel("0");
    valueLabel->setProperty("class", "statValue");
    layout->addWidget(titleLabel);
    layout->addWidget(valueLabel);
    if (!hint.isEmpty()) {
        auto *hintLabel = new QLabel(hint);
        hintLabel->setProperty("class", "statHint");
        layout->addWidget(hintLabel);
    }
    layout->addStretch(1);
    return card;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("CorpCron 管理控制台");
    resize(1320, 820);
    auto *fileMenu = menuBar()->addMenu("文件");
    fileMenu->addAction("退出", this, &QWidget::close);
    auto *runMenu = menuBar()->addMenu("运行");
    runMenu->addAction("启动依赖", this, &MainWindow::onStartDependencies);
    runMenu->addAction("启动服务端", this, &MainWindow::onStartServer);
    runMenu->addAction("运行检查", this, &MainWindow::onRunCheck);
    auto *observabilityMenu = menuBar()->addMenu("可观测性");
    observabilityMenu->addAction("打开 Grafana", this, &MainWindow::onOpenGrafana);
    observabilityMenu->addAction("查看 Alerts", this, &MainWindow::onShowAlerts);
    auto *toolsMenu = menuBar()->addMenu("工具");
    toolsMenu->addAction("Redis 快照", this, &MainWindow::onShowRedisSnapshot);
    toolsMenu->addAction("MySQL 快照", this, &MainWindow::onShowMySQLSnapshot);
    setStyleSheet(
        "QMainWindow, QWidget { background: #1f2329; color: #d8dee9; font-size: 13px; }"
        "QFrame[class='topBar'] { background: #2b3037; border-bottom: 1px solid #171a1f; }"
        "QMenuBar { background: #2b3037; color: #d8dee9; border-bottom: 1px solid #171a1f; }"
        "QMenuBar::item { padding: 5px 10px; background: transparent; }"
        "QMenuBar::item:selected { background: #3b424d; }"
        "QMenu { background: #252a31; color: #d8dee9; border: 1px solid #3b424d; }"
        "QMenu::item { padding: 6px 24px; }"
        "QMenu::item:selected { background: #334155; }"
        "QLabel[class='productTitle'] { color: #f3f4f6; font-size: 15px; font-weight: 700; }"
        "QGroupBox { background: #252a31; border: 1px solid #3b424d; border-radius: 6px; margin-top: 12px; padding: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #aeb7c2; font-weight: 600; }"
        "QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QComboBox { background: #1c2026; color: #d8dee9; border: 1px solid #444c58; border-radius: 4px; padding: 6px; selection-background-color: #2f81f7; }"
        "QPushButton { background: #343b45; border: 1px solid #4b5563; border-radius: 4px; padding: 7px 11px; color: #e5e7eb; }"
        "QPushButton:hover { background: #3f4652; }"
        "QPushButton:disabled { color: #6b7280; background: #252a31; border-color: #343b45; }"
        "QPushButton[role='primary'] { background: #2563eb; color: white; border-color: #1d4ed8; }"
        "QPushButton[role='primary']:hover { background: #1d4ed8; }"
        "QPushButton[role='danger'] { background: #b91c1c; color: white; border-color: #991b1b; }"
        "QPushButton[role='danger']:hover { background: #991b1b; }"
        "QPushButton[role='quiet'] { background: #252a31; }"
        "QLabel[class='statusPill'] { background: #11324d; color: #7dd3fc; border-radius: 10px; padding: 4px 10px; font-weight: 600; }"
        "QFrame[class='statCard'] { background: #252a31; border: 1px solid #3b424d; border-radius: 6px; }"
        "QLabel[class='statTitle'] { color: #9ca3af; font-size: 12px; }"
        "QLabel[class='statValue'] { color: #f9fafb; font-size: 24px; font-weight: 700; }"
        "QLabel[class='statHint'] { color: #7b8492; font-size: 12px; }"
        "QTabWidget::pane { border: 1px solid #343b45; background: #1f2329; }"
        "QTabBar::tab { padding: 10px 16px; margin: 1px; color: #aeb7c2; background: #252a31; border-radius: 3px; }"
        "QTabBar::tab:selected { background: #334155; color: #ffffff; font-weight: 600; }"
        "QHeaderView::section { background: #2b3037; border: 0; border-bottom: 1px solid #3b424d; padding: 7px; color: #cbd5e1; font-weight: 600; }"
        "QTableWidget { background: #20242b; color: #d8dee9; alternate-background-color: #252a31; gridline-color: #343b45; border: 1px solid #343b45; border-radius: 4px; }"
        "QSplitter::handle { background: #343b45; }"
    );

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *connectionBox = new QFrame;
    connectionBox->setProperty("class", "topBar");
    auto *connectionLayout = new QHBoxLayout(connectionBox);
    connectionLayout->setContentsMargins(14, 8, 14, 8);
    auto *titleLabel = new QLabel("CorpCron 管理控制台");
    titleLabel->setProperty("class", "productTitle");
    hostEdit = new QLineEdit("127.0.0.1");
    hostEdit->setMaximumWidth(170);
    portSpin = new QSpinBox;
    portSpin->setRange(1, 65535);
    portSpin->setValue(8081);
    portSpin->setMaximumWidth(92);
    tokenEdit = new QLineEdit;
    tokenEdit->setPlaceholderText("Auth Token，可为空");
    tokenEdit->setEchoMode(QLineEdit::Password);
    tokenEdit->setMaximumWidth(220);
    tokenEdit->setText(qEnvironmentVariable("CORPCRON_RPC_AUTH_TOKEN"));
    connectBtn = new QPushButton("连接");
    disconnectBtn = new QPushButton("断开");
    statusLabel = new QLabel("未连接");
    statusLabel->setProperty("class", "statusPill");
    setButtonRole(connectBtn, "primary");
    setButtonRole(disconnectBtn, "quiet");
    connectionLayout->addWidget(titleLabel);
    connectionLayout->addSpacing(18);
    connectionLayout->addWidget(new QLabel("Host"));
    connectionLayout->addWidget(hostEdit);
    connectionLayout->addWidget(new QLabel("Port"));
    connectionLayout->addWidget(portSpin);
    connectionLayout->addWidget(new QLabel("Token"));
    connectionLayout->addWidget(tokenEdit);
    connectionLayout->addWidget(connectBtn);
    connectionLayout->addWidget(disconnectBtn);
    connectionLayout->addStretch(1);
    connectionLayout->addWidget(statusLabel);
    layout->addWidget(connectionBox);

    auto *mainSplitter = new QSplitter(Qt::Vertical);
    mainSplitter->setChildrenCollapsible(false);
    layout->addWidget(mainSplitter, 1);

    auto *tabs = new QTabWidget;
    tabs->setTabPosition(QTabWidget::West);
    tabs->setDocumentMode(true);
    mainSplitter->addWidget(tabs);

    auto *overviewTab = new QWidget;
    auto *overviewLayout = new QVBoxLayout(overviewTab);
    auto *overviewGrid = new QGridLayout;
    overviewGrid->setSpacing(12);
    overviewGrid->addWidget(createStatCard("连接状态", overviewConnectionValue, "RPC 通道"), 0, 0);
    overviewGrid->addWidget(createStatCard("任务数量", overviewTaskCountValue, "当前列表"), 0, 1);
    overviewGrid->addWidget(createStatCard("历史记录", overviewHistoryCountValue, "最近查询"), 0, 2);
    overviewGrid->addWidget(createStatCard("服务节点", overviewServiceCountValue, "Redis 服务发现"), 0, 3);
    overviewGrid->addWidget(createStatCard("RPC 请求", overviewRpcRequestsValue, "累计"), 1, 0);
    overviewGrid->addWidget(createStatCard("RPC 错误", overviewRpcErrorsValue, "累计"), 1, 1);
    overviewGrid->addWidget(createStatCard("活跃连接", overviewActiveConnectionsValue, "当前"), 1, 2);
    overviewGrid->addWidget(createStatCard("任务成功", overviewTaskSuccessValue, "累计"), 2, 0);
    overviewGrid->addWidget(createStatCard("任务失败", overviewTaskFailureValue, "累计"), 2, 1);
    overviewLayout->addLayout(overviewGrid);
    auto *overviewHint = new QLabel("连接服务后，概览会随任务列表、执行历史、服务发现和运行指标刷新。");
    overviewHint->setProperty("class", "statHint");
    overviewLayout->addWidget(overviewHint);
    overviewLayout->addStretch(1);
    tabs->addTab(overviewTab, "概览");

    auto *operationTab = new QWidget;
    auto *operationLayout = new QVBoxLayout(operationTab);

    auto *echoBox = new QGroupBox("Echo 测试");
    auto *echoLayout = new QHBoxLayout(echoBox);
    echoEdit = new QLineEdit("hello");
    echoBtn = new QPushButton("发送 Echo");
    setButtonRole(echoBtn, "primary");
    echoLayout->addWidget(echoEdit);
    echoLayout->addWidget(echoBtn);
    operationLayout->addWidget(echoBox);

    auto *rpcCapabilityBox = new QGroupBox("RPC 框架能力");
    auto *rpcCapabilityLayout = new QHBoxLayout(rpcCapabilityBox);
    healthCheckBtn = new QPushButton("HealthCheck");
    streamSamplesSpin = new QSpinBox;
    streamSamplesSpin->setRange(1, 10);
    streamSamplesSpin->setValue(3);
    streamMetricsBtn = new QPushButton("StreamMetrics");
    setButtonRole(healthCheckBtn, "quiet");
    setButtonRole(streamMetricsBtn, "primary");
    rpcCapabilityLayout->addWidget(healthCheckBtn);
    rpcCapabilityLayout->addSpacing(12);
    rpcCapabilityLayout->addWidget(new QLabel("Streaming 帧数"));
    rpcCapabilityLayout->addWidget(streamSamplesSpin);
    rpcCapabilityLayout->addWidget(streamMetricsBtn);
    rpcCapabilityLayout->addStretch(1);
    operationLayout->addWidget(rpcCapabilityBox);

    auto *submitBox = new QGroupBox("提交定时任务");
    auto *submitLayout = new QFormLayout(submitBox);
    cronEdit = new QLineEdit;
    cronEdit->setText("* * * * * ?");
    cronEdit->setPlaceholderText("Cron 表达式，例如: * * * * * ?");
    paramsEdit = new QTextEdit;
    paramsEdit->setFixedHeight(88);
    paramsEdit->setPlaceholderText("参数 (字符串)");
    handlerEdit = new QLineEdit;
    handlerEdit->setText("Echo");
    handlerEdit->setPlaceholderText("Handler 名称，例如: Echo");
    submitBtn = new QPushButton("提交任务");
    setButtonRole(submitBtn, "primary");
    submitLayout->addRow("Cron", cronEdit);
    submitLayout->addRow("Handler", handlerEdit);
    submitLayout->addRow("Params", paramsEdit);
    submitLayout->addRow(submitBtn);
    operationLayout->addWidget(submitBox);

    auto *cancelBox = new QGroupBox("取消任务");
    auto *cancelLayout = new QHBoxLayout(cancelBox);
    cancelTaskIdEdit = new QLineEdit;
    cancelTaskIdEdit->setPlaceholderText("task_id");
    cancelBtn = new QPushButton("取消任务");
    setButtonRole(cancelBtn, "danger");
    cancelLayout->addWidget(cancelTaskIdEdit);
    cancelLayout->addWidget(cancelBtn);
    operationLayout->addWidget(cancelBox);
    operationLayout->addStretch(1);
    tabs->addTab(operationTab, "RPC");

    auto *tasksTab = new QWidget;
    auto *tasksLayout = new QVBoxLayout(tasksTab);
    auto *tasksToolbar = new QHBoxLayout;
    enabledOnlyCheck = new QCheckBox("只看启用任务");
    taskStatusCombo = new QComboBox;
    taskStatusCombo->addItem("全部状态", -1);
    taskStatusCombo->addItem("停用", 0);
    taskStatusCombo->addItem("待调度", 1);
    taskStatusCombo->addItem("执行中", 2);
    taskKeywordEdit = new QLineEdit;
    taskKeywordEdit->setPlaceholderText("搜索 ID / Handler / Cron / Params");
    taskPageSizeSpin = new QSpinBox;
    taskPageSizeSpin->setRange(10, 500);
    taskPageSizeSpin->setSingleStep(10);
    taskPageSizeSpin->setValue(50);
    autoRefreshCheck = new QCheckBox("自动刷新");
    refreshTasksBtn = new QPushButton("刷新任务");
    setButtonRole(refreshTasksBtn, "quiet");
    tasksToolbar->addWidget(enabledOnlyCheck);
    tasksToolbar->addWidget(new QLabel("状态"));
    tasksToolbar->addWidget(taskStatusCombo);
    tasksToolbar->addWidget(new QLabel("关键字"));
    tasksToolbar->addWidget(taskKeywordEdit, 1);
    tasksToolbar->addWidget(new QLabel("每页"));
    tasksToolbar->addWidget(taskPageSizeSpin);
    tasksToolbar->addWidget(autoRefreshCheck);
    tasksToolbar->addWidget(refreshTasksBtn);
    tasksLayout->addLayout(tasksToolbar);
    tasksTable = new QTableWidget;
    tasksTable->setColumnCount(12);
    tasksTable->setHorizontalHeaderLabels({"ID", "Handler", "状态", "Cron", "下次执行", "上次执行",
                                           "重试", "最大重试", "Execution ID", "运行节点", "开始时间", "Params"});
    tasksTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tasksTable->setSelectionMode(QAbstractItemView::SingleSelection);
    tasksTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tasksTable->setAlternatingRowColors(true);
    tasksTable->horizontalHeader()->setStretchLastSection(true);
    tasksTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tasksLayout->addWidget(tasksTable);
    auto *tasksPageLayout = new QHBoxLayout;
    prevTasksPageBtn = new QPushButton("上一页");
    nextTasksPageBtn = new QPushButton("下一页");
    tasksPageLabel = new QLabel("第 0-0 条 / 共 0 条");
    setButtonRole(prevTasksPageBtn, "quiet");
    setButtonRole(nextTasksPageBtn, "quiet");
    tasksPageLayout->addStretch(1);
    tasksPageLayout->addWidget(prevTasksPageBtn);
    tasksPageLayout->addWidget(tasksPageLabel);
    tasksPageLayout->addWidget(nextTasksPageBtn);
    tasksLayout->addLayout(tasksPageLayout);

    auto *editBox = new QGroupBox("任务详情 / 操作");
    auto *editLayout = new QFormLayout(editBox);
    editTaskIdEdit = new QLineEdit;
    editTaskIdEdit->setReadOnly(true);
    editCronEdit = new QLineEdit;
    editHandlerEdit = new QLineEdit;
    editParamsEdit = new QTextEdit;
    editParamsEdit->setFixedHeight(70);
    editMaxRetriesSpin = new QSpinBox;
    editMaxRetriesSpin->setRange(1, 100);
    editMaxRetriesSpin->setValue(3);
    updateTaskBtn = new QPushButton("保存修改");
    enableTaskBtn = new QPushButton("启用");
    disableTaskBtn = new QPushButton("禁用");
    deleteTaskBtn = new QPushButton("删除");
    runNowBtn = new QPushButton("立即执行");
    setButtonRole(updateTaskBtn, "primary");
    setButtonRole(runNowBtn, "primary");
    setButtonRole(deleteTaskBtn, "danger");
    auto *taskButtons = new QHBoxLayout;
    taskButtons->addWidget(updateTaskBtn);
    taskButtons->addWidget(enableTaskBtn);
    taskButtons->addWidget(disableTaskBtn);
    taskButtons->addWidget(runNowBtn);
    taskButtons->addWidget(deleteTaskBtn);
    editLayout->addRow("ID", editTaskIdEdit);
    editLayout->addRow("Cron", editCronEdit);
    editLayout->addRow("Handler", editHandlerEdit);
    editLayout->addRow("Params", editParamsEdit);
    editLayout->addRow("最大重试", editMaxRetriesSpin);
    editLayout->addRow(taskButtons);
    tasksLayout->addWidget(editBox);
    tabs->addTab(tasksTab, "任务");

    auto *historyTab = new QWidget;
    auto *historyLayout = new QVBoxLayout(historyTab);
    auto *historyToolbar = new QHBoxLayout;
    historyTaskIdEdit = new QLineEdit;
    historyTaskIdEdit->setPlaceholderText("task_id 为空时查看最近历史");
    historyLimitSpin = new QSpinBox;
    historyLimitSpin->setRange(10, 500);
    historyLimitSpin->setSingleStep(10);
    historyLimitSpin->setValue(50);
    historySuccessCombo = new QComboBox;
    historySuccessCombo->addItem("全部结果", -1);
    historySuccessCombo->addItem("成功", 1);
    historySuccessCombo->addItem("失败", 0);
    historyKeywordEdit = new QLineEdit;
    historyKeywordEdit->setPlaceholderText("搜索 execution_id / 节点 / 结果 / 错误");
    refreshHistoryBtn = new QPushButton("刷新历史");
    setButtonRole(refreshHistoryBtn, "quiet");
    historyToolbar->addWidget(new QLabel("Task ID"));
    historyToolbar->addWidget(historyTaskIdEdit, 1);
    historyToolbar->addWidget(new QLabel("结果"));
    historyToolbar->addWidget(historySuccessCombo);
    historyToolbar->addWidget(new QLabel("关键字"));
    historyToolbar->addWidget(historyKeywordEdit, 1);
    historyToolbar->addWidget(new QLabel("每页"));
    historyToolbar->addWidget(historyLimitSpin);
    historyToolbar->addWidget(refreshHistoryBtn);
    historyLayout->addLayout(historyToolbar);
    historyTable = new QTableWidget;
    historyTable->setColumnCount(8);
    historyTable->setHorizontalHeaderLabels({"Execution ID", "Task ID", "节点", "结果", "返回结果", "错误", "开始时间", "结束时间"});
    historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyTable->setAlternatingRowColors(true);
    historyTable->horizontalHeader()->setStretchLastSection(true);
    historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    historyLayout->addWidget(historyTable);
    auto *historyPageLayout = new QHBoxLayout;
    prevHistoryPageBtn = new QPushButton("上一页");
    nextHistoryPageBtn = new QPushButton("下一页");
    historyPageLabel = new QLabel("第 0-0 条 / 共 0 条");
    setButtonRole(prevHistoryPageBtn, "quiet");
    setButtonRole(nextHistoryPageBtn, "quiet");
    historyPageLayout->addStretch(1);
    historyPageLayout->addWidget(prevHistoryPageBtn);
    historyPageLayout->addWidget(historyPageLabel);
    historyPageLayout->addWidget(nextHistoryPageBtn);
    historyLayout->addLayout(historyPageLayout);
    auto *historyDetailBox = new QGroupBox("执行详情");
    auto *historyDetailLayout = new QVBoxLayout(historyDetailBox);
    historyDetailView = new QPlainTextEdit;
    historyDetailView->setReadOnly(true);
    historyDetailView->setMinimumHeight(130);
    historyDetailLayout->addWidget(historyDetailView);
    historyLayout->addWidget(historyDetailBox);
    tabs->addTab(historyTab, "执行记录");

    auto *servicesTab = new QWidget;
    auto *servicesLayout = new QVBoxLayout(servicesTab);
    auto *servicesToolbar = new QHBoxLayout;
    serviceNameEdit = new QLineEdit("rpc");
    refreshServicesBtn = new QPushButton("刷新服务");
    setButtonRole(refreshServicesBtn, "quiet");
    servicesToolbar->addWidget(new QLabel("Service"));
    servicesToolbar->addWidget(serviceNameEdit);
    servicesToolbar->addStretch(1);
    servicesToolbar->addWidget(refreshServicesBtn);
    servicesLayout->addLayout(servicesToolbar);
    servicesTable = new QTableWidget;
    servicesTable->setColumnCount(1);
    servicesTable->setHorizontalHeaderLabels({"Endpoint"});
    servicesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    servicesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    servicesTable->setAlternatingRowColors(true);
    servicesTable->horizontalHeader()->setStretchLastSection(true);
    servicesLayout->addWidget(servicesTable);
    tabs->addTab(servicesTab, "服务发现");

    auto *metricsTab = new QWidget;
    auto *metricsLayout = new QVBoxLayout(metricsTab);
    auto *metricsToolbar = new QHBoxLayout;
    refreshMetricsBtn = new QPushButton("刷新指标");
    setButtonRole(refreshMetricsBtn, "quiet");
    metricsToolbar->addStretch(1);
    metricsToolbar->addWidget(refreshMetricsBtn);
    metricsLayout->addLayout(metricsToolbar);
    metricsTable = new QTableWidget;
    metricsTable->setColumnCount(2);
    metricsTable->setHorizontalHeaderLabels({"Metric", "值"});
    metricsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    metricsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    metricsTable->setAlternatingRowColors(true);
    metricsTable->horizontalHeader()->setStretchLastSection(true);
    metricsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    metricsLayout->addWidget(metricsTable);
    tabs->addTab(metricsTab, "Metrics");

    auto *demoTab = new QWidget;
    auto *demoLayout = new QVBoxLayout(demoTab);
    auto *runtimeBox = new QGroupBox("运行环境");
    auto *runtimeLayout = new QGridLayout(runtimeBox);
    startDepsBtn = new QPushButton("启动依赖");
    stopDepsBtn = new QPushButton("停止依赖");
    resetDepsBtn = new QPushButton("重置依赖");
    startServerBtn = new QPushButton("启动服务端");
    stopServerBtn = new QPushButton("停止服务端");
    startSecondServerBtn = new QPushButton("启动二节点");
    stopSecondServerBtn = new QPushButton("停止二节点");
    demoCheckBtn = new QPushButton("环境检查");
    cleanDataBtn = new QPushButton("清理数据");
    runCheckBtn = new QPushButton("构建测试");
    runIntegrationCheckBtn = new QPushButton("集成/E2E");
    dockerBuildBtn = new QPushButton("构建镜像");
    showBenchmarkResultBtn = new QPushButton("查看压测结果");
    showDeployDocBtn = new QPushButton("查看部署文档");
    showRedisSnapshotBtn = new QPushButton("Redis 快照");
    showMySQLSnapshotBtn = new QPushButton("MySQL 快照");
    setButtonRole(startDepsBtn, "primary");
    setButtonRole(startServerBtn, "primary");
    setButtonRole(startSecondServerBtn, "primary");
    setButtonRole(stopDepsBtn, "danger");
    setButtonRole(resetDepsBtn, "danger");
    setButtonRole(stopServerBtn, "danger");
    setButtonRole(stopSecondServerBtn, "danger");
    runtimeLayout->addWidget(startDepsBtn, 0, 0);
    runtimeLayout->addWidget(stopDepsBtn, 0, 1);
    runtimeLayout->addWidget(resetDepsBtn, 0, 2);
    runtimeLayout->addWidget(startServerBtn, 1, 0);
    runtimeLayout->addWidget(stopServerBtn, 1, 1);
    runtimeLayout->addWidget(startSecondServerBtn, 1, 2);
    runtimeLayout->addWidget(stopSecondServerBtn, 1, 3);
    runtimeLayout->addWidget(demoCheckBtn, 2, 0);
    runtimeLayout->addWidget(cleanDataBtn, 2, 1);
    runtimeLayout->addWidget(runCheckBtn, 2, 2);
    runtimeLayout->addWidget(runIntegrationCheckBtn, 2, 3);
    runtimeLayout->addWidget(dockerBuildBtn, 3, 0);
    runtimeLayout->addWidget(showBenchmarkResultBtn, 3, 1);
    runtimeLayout->addWidget(showDeployDocBtn, 3, 2);
    runtimeLayout->addWidget(showRedisSnapshotBtn, 4, 0);
    runtimeLayout->addWidget(showMySQLSnapshotBtn, 4, 1);
    demoLayout->addWidget(runtimeBox);

    auto *monitoringBox = new QGroupBox("可观测性");
    auto *monitoringLayout = new QGridLayout(monitoringBox);
    startMonitoringBtn = new QPushButton("启动监控栈");
    stopMonitoringBtn = new QPushButton("停止监控栈");
    openPrometheusBtn = new QPushButton("打开 Prometheus");
    openAlertmanagerBtn = new QPushButton("打开 Alertmanager");
    openGrafanaBtn = new QPushButton("打开 Grafana");
    showAlertsBtn = new QPushButton("查看 Alerts");
    showRawMetricsBtn = new QPushButton("查看原始 Metrics");
    setButtonRole(startMonitoringBtn, "primary");
    setButtonRole(stopMonitoringBtn, "danger");
    monitoringLayout->addWidget(startMonitoringBtn, 0, 0);
    monitoringLayout->addWidget(stopMonitoringBtn, 0, 1);
    monitoringLayout->addWidget(openPrometheusBtn, 1, 0);
    monitoringLayout->addWidget(openAlertmanagerBtn, 1, 1);
    monitoringLayout->addWidget(openGrafanaBtn, 1, 2);
    monitoringLayout->addWidget(showAlertsBtn, 2, 0);
    monitoringLayout->addWidget(showRawMetricsBtn, 2, 1);
    demoLayout->addWidget(monitoringBox);

    auto *benchmarkBox = new QGroupBox("压测");
    auto *benchmarkLayout = new QHBoxLayout(benchmarkBox);
    benchConcurrencySpin = new QSpinBox;
    benchConcurrencySpin->setRange(1, 512);
    benchConcurrencySpin->setValue(16);
    benchRequestsSpin = new QSpinBox;
    benchRequestsSpin->setRange(1, 1000000);
    benchRequestsSpin->setValue(1000);
    benchmarkShortBtn = new QPushButton("短连接压测");
    benchmarkReuseBtn = new QPushButton("长连接压测");
    setButtonRole(benchmarkShortBtn, "primary");
    setButtonRole(benchmarkReuseBtn, "primary");
    benchmarkLayout->addWidget(new QLabel("并发数"));
    benchmarkLayout->addWidget(benchConcurrencySpin);
    benchmarkLayout->addWidget(new QLabel("请求数"));
    benchmarkLayout->addWidget(benchRequestsSpin);
    benchmarkLayout->addWidget(benchmarkShortBtn);
    benchmarkLayout->addWidget(benchmarkReuseBtn);
    demoLayout->addWidget(benchmarkBox);

    auto *frameworkBox = new QGroupBox("RPC 框架验证");
    auto *frameworkLayout = new QHBoxLayout(frameworkBox);
    deadlineCancellationBtn = new QPushButton("Deadline / Cancellation");
    halfOpenProbeBtn = new QPushButton("HealthCheck + 半开探测");
    streamingStubBtn = new QPushButton("Streaming Stub");
    setButtonRole(deadlineCancellationBtn, "quiet");
    setButtonRole(halfOpenProbeBtn, "quiet");
    setButtonRole(streamingStubBtn, "quiet");
    frameworkLayout->addWidget(deadlineCancellationBtn);
    frameworkLayout->addWidget(halfOpenProbeBtn);
    frameworkLayout->addWidget(streamingStubBtn);
    frameworkLayout->addStretch(1);
    demoLayout->addWidget(frameworkBox);

    auto *diagnosticBox = new QGroupBox("协议异常");
    auto *diagnosticLayout = new QHBoxLayout(diagnosticBox);
    authFailureBtn = new QPushButton("鉴权失败");
    unknownRpcBtn = new QPushButton("未知方法");
    badFrameBtn = new QPushButton("坏包断连");
    setButtonRole(authFailureBtn, "quiet");
    setButtonRole(unknownRpcBtn, "quiet");
    setButtonRole(badFrameBtn, "danger");
    diagnosticLayout->addWidget(authFailureBtn);
    diagnosticLayout->addWidget(unknownRpcBtn);
    diagnosticLayout->addWidget(badFrameBtn);
    diagnosticLayout->addStretch(1);
    demoLayout->addWidget(diagnosticBox);

    auto *operationsHint = new QLabel("所有命令输出、服务端日志和压测结果都会进入底部控制台。");
    operationsHint->setProperty("class", "statHint");
    demoLayout->addWidget(operationsHint);
    demoLayout->addStretch(1);
    tabs->addTab(demoTab, "运维");

    auto *bottomConsole = new QTabWidget;
    bottomConsole->setDocumentMode(true);
    bottomConsole->setMinimumHeight(170);

    auto *toolOutputPage = new QWidget;
    auto *toolOutputLayout = new QVBoxLayout(toolOutputPage);
    toolOutputLayout->setContentsMargins(6, 6, 6, 6);
    toolOutput = new QPlainTextEdit;
    toolOutput->setReadOnly(true);
    toolOutputLayout->addWidget(toolOutput);
    bottomConsole->addTab(toolOutputPage, "控制台");

    auto *logBox = new QWidget;
    auto *logLayout = new QVBoxLayout(logBox);
    logLayout->setContentsMargins(6, 6, 6, 6);
    logView = new QPlainTextEdit;
    logView->setReadOnly(true);
    clearLogBtn = new QPushButton("清空日志");
    logLayout->addWidget(logView);
    logLayout->addWidget(clearLogBtn);
    bottomConsole->addTab(logBox, "事件");
    mainSplitter->addWidget(bottomConsole);
    mainSplitter->setStretchFactor(0, 4);
    mainSplitter->setStretchFactor(1, 1);

    connect(connectBtn, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    connect(echoBtn, &QPushButton::clicked, this, &MainWindow::onEcho);
    connect(healthCheckBtn, &QPushButton::clicked, this, &MainWindow::onHealthCheck);
    connect(streamMetricsBtn, &QPushButton::clicked, this, &MainWindow::onStreamMetrics);
    connect(submitBtn, &QPushButton::clicked, this, &MainWindow::onSubmit);
    connect(cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancelTask);
    connect(updateTaskBtn, &QPushButton::clicked, this, &MainWindow::onUpdateTask);
    connect(enableTaskBtn, &QPushButton::clicked, this, &MainWindow::onEnableSelectedTask);
    connect(disableTaskBtn, &QPushButton::clicked, this, &MainWindow::onDisableSelectedTask);
    connect(deleteTaskBtn, &QPushButton::clicked, this, &MainWindow::onDeleteSelectedTask);
    connect(runNowBtn, &QPushButton::clicked, this, &MainWindow::onRunSelectedTaskNow);
    connect(refreshTasksBtn, &QPushButton::clicked, this, &MainWindow::onRefreshTasks);
    connect(enabledOnlyCheck, &QCheckBox::toggled, this, &MainWindow::onTaskFilterChanged);
    connect(taskStatusCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onTaskFilterChanged);
    connect(taskKeywordEdit, &QLineEdit::returnPressed, this, &MainWindow::onTaskFilterChanged);
    connect(taskPageSizeSpin, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::onTaskFilterChanged);
    connect(prevTasksPageBtn, &QPushButton::clicked, this, &MainWindow::onPrevTasksPage);
    connect(nextTasksPageBtn, &QPushButton::clicked, this, &MainWindow::onNextTasksPage);
    connect(refreshHistoryBtn, &QPushButton::clicked, this, &MainWindow::onRefreshHistory);
    connect(historyTaskIdEdit, &QLineEdit::returnPressed, this, &MainWindow::onHistoryFilterChanged);
    connect(historySuccessCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onHistoryFilterChanged);
    connect(historyKeywordEdit, &QLineEdit::returnPressed, this, &MainWindow::onHistoryFilterChanged);
    connect(historyLimitSpin, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::onHistoryFilterChanged);
    connect(prevHistoryPageBtn, &QPushButton::clicked, this, &MainWindow::onPrevHistoryPage);
    connect(nextHistoryPageBtn, &QPushButton::clicked, this, &MainWindow::onNextHistoryPage);
    connect(refreshServicesBtn, &QPushButton::clicked, this, &MainWindow::onRefreshServices);
    connect(refreshMetricsBtn, &QPushButton::clicked, this, &MainWindow::onRefreshMetrics);
    connect(startDepsBtn, &QPushButton::clicked, this, &MainWindow::onStartDependencies);
    connect(stopDepsBtn, &QPushButton::clicked, this, &MainWindow::onStopDependencies);
    connect(resetDepsBtn, &QPushButton::clicked, this, &MainWindow::onResetDependencies);
    connect(startServerBtn, &QPushButton::clicked, this, &MainWindow::onStartServer);
    connect(stopServerBtn, &QPushButton::clicked, this, &MainWindow::onStopServer);
    connect(startSecondServerBtn, &QPushButton::clicked, this, &MainWindow::onStartSecondServer);
    connect(stopSecondServerBtn, &QPushButton::clicked, this, &MainWindow::onStopSecondServer);
    connect(demoCheckBtn, &QPushButton::clicked, this, &MainWindow::onDemoCheck);
    connect(cleanDataBtn, &QPushButton::clicked, this, &MainWindow::onCleanDemoData);
    connect(runCheckBtn, &QPushButton::clicked, this, &MainWindow::onRunCheck);
    connect(runIntegrationCheckBtn, &QPushButton::clicked, this, &MainWindow::onRunIntegrationCheck);
    connect(dockerBuildBtn, &QPushButton::clicked, this, &MainWindow::onDockerBuild);
    connect(showBenchmarkResultBtn, &QPushButton::clicked, this, &MainWindow::onShowBenchmarkResult);
    connect(showDeployDocBtn, &QPushButton::clicked, this, &MainWindow::onShowDeployDoc);
    connect(startMonitoringBtn, &QPushButton::clicked, this, &MainWindow::onStartMonitoringStack);
    connect(stopMonitoringBtn, &QPushButton::clicked, this, &MainWindow::onStopMonitoringStack);
    connect(openPrometheusBtn, &QPushButton::clicked, this, &MainWindow::onOpenPrometheus);
    connect(openAlertmanagerBtn, &QPushButton::clicked, this, &MainWindow::onOpenAlertmanager);
    connect(openGrafanaBtn, &QPushButton::clicked, this, &MainWindow::onOpenGrafana);
    connect(showAlertsBtn, &QPushButton::clicked, this, &MainWindow::onShowAlerts);
    connect(showRawMetricsBtn, &QPushButton::clicked, this, &MainWindow::onShowRawMetrics);
    connect(showRedisSnapshotBtn, &QPushButton::clicked, this, &MainWindow::onShowRedisSnapshot);
    connect(showMySQLSnapshotBtn, &QPushButton::clicked, this, &MainWindow::onShowMySQLSnapshot);
    connect(benchmarkShortBtn, &QPushButton::clicked, this, &MainWindow::onBenchmarkShort);
    connect(benchmarkReuseBtn, &QPushButton::clicked, this, &MainWindow::onBenchmarkReuse);
    connect(deadlineCancellationBtn, &QPushButton::clicked, this, &MainWindow::onDeadlineCancellationDemo);
    connect(halfOpenProbeBtn, &QPushButton::clicked, this, &MainWindow::onHalfOpenProbeDemo);
    connect(streamingStubBtn, &QPushButton::clicked, this, &MainWindow::onStreamingStubDemo);
    connect(authFailureBtn, &QPushButton::clicked, this, &MainWindow::onAuthFailureTest);
    connect(unknownRpcBtn, &QPushButton::clicked, this, &MainWindow::onUnknownRpcTest);
    connect(badFrameBtn, &QPushButton::clicked, this, &MainWindow::onBadFrameTest);
    connect(tasksTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onTaskSelectionChanged);
    connect(historyTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onHistorySelectionChanged);
    connect(autoRefreshCheck, &QCheckBox::toggled, this, &MainWindow::onAutoRefreshChanged);
    connect(clearLogBtn, &QPushButton::clicked, logView, &QPlainTextEdit::clear);

    autoRefreshTimer = new QTimer(this);
    autoRefreshTimer->setInterval(3000);
    connect(autoRefreshTimer, &QTimer::timeout, this, &MainWindow::onAutoRefreshTick);

    socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::readyRead, this, &MainWindow::onReadyRead);
    connect(socket, &QTcpSocket::connected, this, &MainWindow::onSocketConnected);
    connect(socket, &QTcpSocket::disconnected, this, &MainWindow::onSocketDisconnected);
    connect(socket, &QTcpSocket::errorOccurred, this, &MainWindow::onSocketError);

    toolProcess = new QProcess(this);
    toolProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(toolProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        appendToolOutput(QString::fromLocal8Bit(toolProcess->readAllStandardOutput()));
    });
    connect(toolProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        appendToolOutput(QString("\n[工具结束] exit=%1 status=%2\n")
                             .arg(exitCode)
                             .arg(status == QProcess::NormalExit ? "正常" : "异常"));
        updateUiState();
    });

    serverProcess = new QProcess(this);
    serverProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(serverProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        appendToolOutput(QString::fromLocal8Bit(serverProcess->readAllStandardOutput()));
    });
    connect(serverProcess, &QProcess::started, this, [this]() {
        appendToolOutput("[服务端已启动]\n");
        updateUiState();
    });
    connect(serverProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        appendToolOutput(QString("\n[服务端已退出] exit=%1 status=%2\n")
                             .arg(exitCode)
                             .arg(status == QProcess::NormalExit ? "正常" : "异常"));
        updateUiState();
    });

    server2Process = new QProcess(this);
    server2Process->setProcessChannelMode(QProcess::MergedChannels);
    connect(server2Process, &QProcess::readyReadStandardOutput, this, [this]() {
        appendToolOutput(QString::fromLocal8Bit(server2Process->readAllStandardOutput()));
    });
    connect(server2Process, &QProcess::started, this, [this]() {
        appendToolOutput("[二节点已启动]\n");
        updateUiState();
    });
    connect(server2Process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        appendToolOutput(QString("\n[二节点已退出] exit=%1 status=%2\n")
                             .arg(exitCode)
                             .arg(status == QProcess::NormalExit ? "正常" : "异常"));
        updateUiState();
    });
    updateUiState();
}

MainWindow::~MainWindow() {
    if (socket->state() == QAbstractSocket::ConnectedState)
        socket->disconnectFromHost();
    if (serverProcess && serverProcess->state() != QProcess::NotRunning) {
        serverProcess->terminate();
        if (!serverProcess->waitForFinished(3000)) {
            serverProcess->kill();
            serverProcess->waitForFinished(3000);
        }
    }
    if (server2Process && server2Process->state() != QProcess::NotRunning) {
        server2Process->terminate();
        if (!server2Process->waitForFinished(3000)) {
            server2Process->kill();
            server2Process->waitForFinished(3000);
        }
    }
    if (toolProcess && toolProcess->state() != QProcess::NotRunning) {
        toolProcess->kill();
        toolProcess->waitForFinished(3000);
    }
}

void MainWindow::onConnect() {
    recvBuffer.clear();
    socket->abort();
    appendLog(QString("连接 %1:%2").arg(hostEdit->text()).arg(portSpin->value()));
    socket->connectToHost(hostEdit->text(), static_cast<quint16>(portSpin->value()));
    updateUiState();
}

void MainWindow::onDisconnect() {
    socket->disconnectFromHost();
    updateUiState();
}

void MainWindow::onEcho() {
    if (echoEdit->text().isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写 Echo 消息");
        return;
    }
    corpcron::rpc::EchoRequest req;
    req.set_message(echoEdit->text().toStdString());
    req.set_auth_token(authToken());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kEchoRequestSerialId, payload, "Echo");
}

void MainWindow::onHealthCheck() {
    corpcron::rpc::HealthCheckRequest req;
    req.set_service_name("rpc");
    req.set_auth_token(authToken());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kHealthCheckRequestSerialId, payload, "HealthCheck");
}

void MainWindow::onStreamMetrics() {
    corpcron::rpc::StreamMetricsRequest req;
    req.set_auth_token(authToken());
    req.set_samples(streamSamplesSpin->value());

    std::string payload;
    req.SerializeToString(&payload);
    appendLog(QString("StreamMetrics 将接收 %1 帧响应").arg(streamSamplesSpin->value()));
    sendFrame(corpcron::rpc::kStreamMetricsRequestSerialId, payload, "StreamMetrics");
}

void MainWindow::onSubmit() {
    if (cronEdit->text().isEmpty() || handlerEdit->text().isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写 Cron 表达式和处理器名称");
        return;
    }
    corpcron::rpc::SubmitTaskRequest req;
    req.set_cron_expr(cronEdit->text().toStdString());
    req.set_params(paramsEdit->toPlainText().toStdString());
    req.set_handler(handlerEdit->text().toStdString());
    req.set_auth_token(authToken());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kSubmitTaskRequestSerialId, payload, "SubmitTask");
}

void MainWindow::onCancelTask() {
    QString taskId = cancelTaskIdEdit->text().trimmed();
    if (taskId.isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写 task_id");
        return;
    }
    corpcron::rpc::CancelTaskRequest req;
    req.set_task_id(taskId.toStdString());
    req.set_auth_token(authToken());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kCancelTaskRequestSerialId, payload, "CancelTask");
}

void MainWindow::onUpdateTask() {
    QString taskId = editTaskIdEdit->text().trimmed();
    if (taskId.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先在任务列表中选择任务");
        return;
    }
    if (editCronEdit->text().trimmed().isEmpty() || editHandlerEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "提示", "Cron 和 Handler 不能为空");
        return;
    }

    corpcron::rpc::UpdateTaskRequest req;
    req.set_auth_token(authToken());
    auto* task = req.mutable_task();
    task->set_id(taskId.toStdString());
    task->set_cron_expr(editCronEdit->text().trimmed().toStdString());
    task->set_handler(editHandlerEdit->text().trimmed().toStdString());
    task->set_params(editParamsEdit->toPlainText().toStdString());
    int status = 1;
    if (!tasksTable->selectedItems().isEmpty()) {
        int row = tasksTable->selectedItems().first()->row();
        if (auto* status_item = tasksTable->item(row, 2)) {
            status = status_item->text() == "停用" ? 0 : 1;
        }
    }
    task->set_status(status);
    task->set_retry_count(0);
    task->set_max_retries(editMaxRetriesSpin->value());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kUpdateTaskRequestSerialId, payload, "UpdateTask");
}

void MainWindow::onEnableSelectedTask() {
    QString taskId = editTaskIdEdit->text().trimmed();
    if (taskId.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先在任务列表中选择任务");
        return;
    }
    corpcron::rpc::EnableTaskRequest req;
    req.set_auth_token(authToken());
    req.set_task_id(taskId.toStdString());
    req.set_enabled(true);

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kEnableTaskRequestSerialId, payload, "EnableTask");
}

void MainWindow::onDisableSelectedTask() {
    QString taskId = editTaskIdEdit->text().trimmed();
    if (taskId.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先在任务列表中选择任务");
        return;
    }
    corpcron::rpc::EnableTaskRequest req;
    req.set_auth_token(authToken());
    req.set_task_id(taskId.toStdString());
    req.set_enabled(false);

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kEnableTaskRequestSerialId, payload, "DisableTask");
}

void MainWindow::onDeleteSelectedTask() {
    QString taskId = editTaskIdEdit->text().trimmed();
    if (taskId.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先在任务列表中选择任务");
        return;
    }
    if (QMessageBox::question(this, "确认删除", "确定要删除任务 " + taskId + " 吗？") != QMessageBox::Yes) {
        return;
    }
    corpcron::rpc::DeleteTaskRequest req;
    req.set_auth_token(authToken());
    req.set_task_id(taskId.toStdString());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kDeleteTaskRequestSerialId, payload, "DeleteTask");
}

void MainWindow::onRunSelectedTaskNow() {
    QString taskId = editTaskIdEdit->text().trimmed();
    if (taskId.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先在任务列表中选择任务");
        return;
    }
    corpcron::rpc::RunTaskNowRequest req;
    req.set_auth_token(authToken());
    req.set_task_id(taskId.toStdString());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kRunTaskNowRequestSerialId, payload, "RunTaskNow");
}

void MainWindow::onRefreshTasks() {
    corpcron::rpc::ListTasksRequest req;
    req.set_auth_token(authToken());
    tasksLimit_ = taskPageSizeSpin->value();
    req.set_limit(tasksLimit_);
    req.set_offset(tasksPageOffset_);
    req.set_enabled_only(enabledOnlyCheck->isChecked());
    const int status_filter = taskStatusCombo->currentData().toInt();
    if (status_filter >= 0) {
        req.set_has_status_filter(true);
        req.set_status_filter(status_filter);
    }
    req.set_keyword(taskKeywordEdit->text().trimmed().toStdString());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kListTasksRequestSerialId, payload, "ListTasks");
}

void MainWindow::onTaskFilterChanged() {
    tasksPageOffset_ = 0;
    if (socket->state() == QAbstractSocket::ConnectedState) onRefreshTasks();
    updateTaskPaginationUi();
}

void MainWindow::onPrevTasksPage() {
    tasksLimit_ = taskPageSizeSpin->value();
    tasksPageOffset_ = std::max(0, tasksPageOffset_ - tasksLimit_);
    onRefreshTasks();
}

void MainWindow::onNextTasksPage() {
    tasksLimit_ = taskPageSizeSpin->value();
    if (tasksPageOffset_ + tasksLimit_ < tasksTotal_) {
        tasksPageOffset_ += tasksLimit_;
        onRefreshTasks();
    }
}

void MainWindow::onRefreshHistory() {
    corpcron::rpc::ListHistoryRequest req;
    req.set_auth_token(authToken());
    req.set_task_id(historyTaskIdEdit->text().trimmed().toStdString());
    historyLimit_ = historyLimitSpin->value();
    req.set_limit(historyLimit_);
    req.set_offset(historyPageOffset_);
    const int success_filter = historySuccessCombo->currentData().toInt();
    if (success_filter >= 0) {
        req.set_has_success_filter(true);
        req.set_success_filter(success_filter);
    }
    req.set_keyword(historyKeywordEdit->text().trimmed().toStdString());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kListHistoryRequestSerialId, payload, "ListHistory");
}

void MainWindow::onHistoryFilterChanged() {
    historyPageOffset_ = 0;
    if (socket->state() == QAbstractSocket::ConnectedState) onRefreshHistory();
    updateHistoryPaginationUi();
}

void MainWindow::onPrevHistoryPage() {
    historyLimit_ = historyLimitSpin->value();
    historyPageOffset_ = std::max(0, historyPageOffset_ - historyLimit_);
    onRefreshHistory();
}

void MainWindow::onNextHistoryPage() {
    historyLimit_ = historyLimitSpin->value();
    if (historyPageOffset_ + historyLimit_ < historyTotal_) {
        historyPageOffset_ += historyLimit_;
        onRefreshHistory();
    }
}

void MainWindow::onRefreshServices() {
    corpcron::rpc::ListServicesRequest req;
    req.set_auth_token(authToken());
    req.set_service_name(serviceNameEdit->text().trimmed().isEmpty()
                             ? "rpc"
                             : serviceNameEdit->text().trimmed().toStdString());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kListServicesRequestSerialId, payload, "ListServices");
}

void MainWindow::onRefreshMetrics() {
    corpcron::rpc::GetMetricsRequest req;
    req.set_auth_token(authToken());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kGetMetricsRequestSerialId, payload, "GetMetrics");
}

void MainWindow::onStartDependencies() {
    runTool("docker", {"compose", "up", "-d"}, "docker compose up");
}

void MainWindow::onStopDependencies() {
    runTool("docker", {"compose", "down"}, "docker compose down");
}

void MainWindow::onResetDependencies() {
    if (QMessageBox::question(this, "确认重置", "这会执行 docker compose down -v 并删除演示数据库卷，确定继续吗？") !=
        QMessageBox::Yes) {
        return;
    }
    runTool("docker", {"compose", "down", "-v"}, "docker compose down -v");
}

void MainWindow::onStartServer() {
    startServerProcess(serverProcess, "config/server.conf", "服务端");
}

void MainWindow::onStopServer() {
    if (serverProcess->state() == QProcess::NotRunning) {
        appendToolOutput("[当前 Qt 客户端没有启动服务端]\n");
        return;
    }
    appendToolOutput("[正在停止服务端]\n");
    serverProcess->terminate();
}

void MainWindow::onStartSecondServer() {
    startServerProcess(server2Process, "config/server2.conf", "二节点");
}

void MainWindow::onStopSecondServer() {
    if (server2Process->state() == QProcess::NotRunning) {
        appendToolOutput("[当前 Qt 客户端没有启动二节点]\n");
        return;
    }
    appendToolOutput("[正在停止二节点]\n");
    server2Process->terminate();
}

void MainWindow::onDemoCheck() {
    runTool("./scripts/demo_check.sh", {}, "demo_check");
}

void MainWindow::onCleanDemoData() {
    if (QMessageBox::question(this, "确认清理",
                              "这会删除所有任务、执行历史和 CorpCron Redis key，确定继续吗？") !=
        QMessageBox::Yes) {
        return;
    }
    runTool("./scripts/clean_demo_data.sh", {}, "clean_demo_data");
}

void MainWindow::onRunCheck() {
    runTool("./scripts/check.sh", {}, "check");
}

void MainWindow::onRunIntegrationCheck() {
    runTool("./scripts/check.sh", {"--compose"}, "integration check");
}

void MainWindow::onDockerBuild() {
    runTool("docker", {"build", "-t", "corpcron:local", "."}, "docker build");
}

void MainWindow::onShowBenchmarkResult() {
    runTool("bash", {"-lc", "cat docs/assets/benchmark/latest.txt 2>/dev/null || echo 'No benchmark result yet.'"},
            "查看压测结果");
}

void MainWindow::onShowDeployDoc() {
    runTool("bash", {"-lc", "printf '%s\\n' '== deploy.md ==' && cat docs/guide/deploy.md && printf '%s\\n' '\\n== systemd/corpcron.service ==' && cat systemd/corpcron.service"},
            "查看部署文档");
}

void MainWindow::onStartMonitoringStack() {
    runTool("docker", {"compose", "-f", "deploy/monitoring/docker-compose.monitoring.yml", "up", "-d"},
            "启动监控栈");
}

void MainWindow::onStopMonitoringStack() {
    runTool("docker", {"compose", "-f", "deploy/monitoring/docker-compose.monitoring.yml", "down"},
            "停止监控栈");
}

void MainWindow::onOpenPrometheus() {
    QDesktopServices::openUrl(QUrl("http://127.0.0.1:9090"));
    appendToolOutput("[打开] http://127.0.0.1:9090\n");
}

void MainWindow::onOpenAlertmanager() {
    QDesktopServices::openUrl(QUrl("http://127.0.0.1:9093"));
    appendToolOutput("[打开] http://127.0.0.1:9093\n");
}

void MainWindow::onOpenGrafana() {
    QDesktopServices::openUrl(QUrl("http://127.0.0.1:3000"));
    appendToolOutput("[打开] http://127.0.0.1:3000 admin/admin\n");
}

void MainWindow::onShowAlerts() {
    runTool("bash", {"-lc", "curl -sS http://127.0.0.1:9091/alerts || true"}, "查看 Alerts");
}

void MainWindow::onShowRawMetrics() {
    runTool("bash", {"-lc", "curl -sS http://127.0.0.1:9091/metrics || true"}, "查看原始 Metrics");
}

void MainWindow::onShowRedisSnapshot() {
    const QString script =
        "printf '%s\\n' '== services:rpc members ==' && "
        "docker exec corpcron-redis redis-cli --raw SMEMBERS services:rpc && "
        "printf '%s\\n' '\\n== services:rpc keys and ttl ==' && "
        "for k in $(docker exec corpcron-redis redis-cli --raw KEYS 'services:rpc:*'); do "
        "ttl=$(docker exec corpcron-redis redis-cli --raw TTL \"$k\"); "
        "printf '%s ttl=%s\\n' \"$k\" \"$ttl\"; "
        "done";
    runTool("bash", {"-lc", script}, "Redis 快照");
}

void MainWindow::onShowMySQLSnapshot() {
    const QString script =
        "printf '%s\\n' '== tasks ==' && "
        "docker exec -e MYSQL_PWD=\"${CORPCRON_MYSQL_PASSWORD:-corpcron_dev_password}\" "
        "corpcron-mysql mysql -ucorpcron corpcron "
        "-e \"SELECT id,handler,status,current_execution_id,running_node,next_run_at,last_run_at,retry_count "
        "FROM tasks ORDER BY created_at DESC LIMIT 10;\" && "
        "printf '%s\\n' '\\n== task_history ==' && "
        "docker exec -e MYSQL_PWD=\"${CORPCRON_MYSQL_PASSWORD:-corpcron_dev_password}\" "
        "corpcron-mysql mysql -ucorpcron corpcron "
        "-e \"SELECT execution_id,task_id,exec_node,success,result,error,start_time,end_time "
        "FROM task_history ORDER BY id DESC LIMIT 10;\"";
    runTool("bash", {"-lc", script}, "MySQL 快照");
}

void MainWindow::onBenchmarkShort() {
    runTool("./scripts/benchmark.sh",
            {hostEdit->text(), QString::number(portSpin->value()),
             QString::number(benchConcurrencySpin->value()),
             QString::number(benchRequestsSpin->value()), "short"},
            "短连接压测");
}

void MainWindow::onBenchmarkReuse() {
    runTool("./scripts/benchmark.sh",
            {hostEdit->text(), QString::number(portSpin->value()),
             QString::number(benchConcurrencySpin->value()),
             QString::number(benchRequestsSpin->value()), "reuse"},
            "长连接压测");
}

void MainWindow::onDeadlineCancellationDemo() {
    runTool("./build/corpcron_rpc_client_test", {}, "Deadline / Cancellation / Streaming 原始客户端测试");
}

void MainWindow::onHalfOpenProbeDemo() {
    runTool("./build/corpcron_rpc_client_pool_test", {}, "HealthCheck + 熔断半开探测测试");
}

void MainWindow::onStreamingStubDemo() {
    runTool("./build/corpcron_rpc_service_bindings_test", {}, "Generated Streaming Stub 测试");
}

void MainWindow::onAuthFailureTest() {
    corpcron::rpc::EchoRequest req;
    req.set_message("auth-failure-demo");
    req.set_auth_token("__wrong_token__");
    std::string payload;
    req.SerializeToString(&payload);
    appendLog("如果服务端未配置 rpc.auth_token，该请求会被正常放行");
    sendFrame(corpcron::rpc::kEchoRequestSerialId, payload, "AuthFailureTest");
}

void MainWindow::onUnknownRpcTest() {
    sendFrame(999999, "", "UnknownRpcTest");
}

void MainWindow::onBadFrameTest() {
    if (socket->state() != QAbstractSocket::ConnectedState) {
        QMessageBox::warning(this, "提示", "请先连接服务器");
        return;
    }
    QByteArray badFrame;
    badFrame.append(char(0));
    badFrame.append(char(0));
    badFrame.append(char(0));
    badFrame.append(char(1));
    badFrame.append(char(0));
    badFrame.append(char(0));
    badFrame.append(char(0));
    badFrame.append(char(1));
    socket->write(badFrame);
    appendLog("BadFrame 请求已发送，服务端应关闭连接并记录 malformed frame");
}

void MainWindow::onTaskSelectionChanged() {
    auto items = tasksTable->selectedItems();
    if (items.isEmpty()) return;
    int row = items.first()->row();
    auto* id_item = tasksTable->item(row, 0);
    if (!id_item) return;
    cancelTaskIdEdit->setText(id_item->text());
    historyTaskIdEdit->setText(id_item->text());
    if (auto* cron = tasksTable->item(row, 3)) editCronEdit->setText(cron->text());
    if (auto* handler = tasksTable->item(row, 1)) editHandlerEdit->setText(handler->text());
    if (auto* max_retries = tasksTable->item(row, 7)) editMaxRetriesSpin->setValue(max_retries->text().toInt());
    if (auto* params = tasksTable->item(row, 11)) editParamsEdit->setPlainText(params->text());
    editTaskIdEdit->setText(id_item->text());
}

void MainWindow::onHistorySelectionChanged() {
    auto items = historyTable->selectedItems();
    if (items.isEmpty() || !historyDetailView) return;
    const int row = items.first()->row();
    auto textAt = [this, row](int col) {
        auto* item = historyTable->item(row, col);
        return item ? item->text() : QString();
    };

    QString detail;
    detail += "Execution ID: " + textAt(0) + "\n";
    detail += "Task ID: " + textAt(1) + "\n";
    detail += "节点: " + textAt(2) + "\n";
    detail += "结果: " + textAt(3) + "\n";
    detail += "开始时间: " + textAt(6) + "\n";
    detail += "结束时间: " + textAt(7) + "\n\n";
    detail += "返回结果:\n" + textAt(4) + "\n\n";
    detail += "错误:\n" + textAt(5);
    historyDetailView->setPlainText(detail);
}

void MainWindow::onAutoRefreshChanged(bool checked) {
    if (checked) {
        autoRefreshTimer->start();
        appendLog("自动刷新已开启");
    } else {
        autoRefreshTimer->stop();
        appendLog("自动刷新已关闭");
    }
}

void MainWindow::onAutoRefreshTick() {
    if (socket->state() != QAbstractSocket::ConnectedState) return;
    onRefreshServices();
    onRefreshTasks();
    onRefreshHistory();
    onRefreshMetrics();
}

void MainWindow::onReadyRead() {
    recvBuffer.append(socket->readAll());
    while (recvBuffer.size() >= static_cast<int>(corpcron::rpc::kHeaderSize)) {
        uint32_t serialId = 0;
        std::string payload;
        size_t frameSize = 0;
        corpcron::rpc::DecodeStatus status = corpcron::rpc::tryDecodeFrame(
            recvBuffer.constData(), static_cast<size_t>(recvBuffer.size()), serialId, payload, frameSize);
        if (status == corpcron::rpc::DecodeStatus::Incomplete) return;
        if (status == corpcron::rpc::DecodeStatus::Malformed ||
            status == corpcron::rpc::DecodeStatus::TooLarge) {
            appendLog("收到非法 RPC 帧，已断开连接");
            recvBuffer.clear();
            socket->disconnectFromHost();
            return;
        }
        handleFrame(serialId, payload);
        recvBuffer.remove(0, static_cast<int>(frameSize));
    }
}

void MainWindow::onSocketConnected() {
    appendLog("连接成功");
    updateUiState();
    onRefreshServices();
    onRefreshTasks();
    onRefreshHistory();
    onRefreshMetrics();
}

void MainWindow::onSocketDisconnected() {
    appendLog("连接已断开");
    updateUiState();
}

void MainWindow::onSocketError(QAbstractSocket::SocketError) {
    appendLog("连接错误: " + socket->errorString());
    updateUiState();
}

bool MainWindow::sendFrame(uint32_t serialId, const std::string& payload, const QString& action) {
    if (socket->state() != QAbstractSocket::ConnectedState) {
        QMessageBox::warning(this, "提示", "请先连接服务器");
        return false;
    }
    QByteArray data = encode(serialId, payload);
    if (data.isEmpty()) {
        appendLog(action + " 发送失败: 请求过大");
        return false;
    }
    qint64 written = socket->write(data);
    if (written != data.size()) {
        appendLog(action + " 发送失败: socket 写入不完整");
        socket->abort();
        return false;
    }
    appendLog(action + " 请求已发送");
    return true;
}

std::string MainWindow::authToken() const {
    return tokenEdit->text().toStdString();
}

QString MainWindow::projectRoot() const {
    if (QFileInfo::exists(QDir::currentPath() + "/scripts/check.sh")) {
        return QDir::currentPath();
    }
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.cd("../..") && QFileInfo::exists(dir.absolutePath() + "/scripts/check.sh")) {
        return dir.canonicalPath();
    }
    return QDir::currentPath();
}

QProcessEnvironment MainWindow::processEnvironment() const {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("DOCKER_HOST");
    if (!tokenEdit->text().trimmed().isEmpty()) {
        env.insert("CORPCRON_RPC_AUTH_TOKEN", tokenEdit->text().trimmed());
    }
    if (!env.contains("CORPCRON_REDIS_PORT")) env.insert("CORPCRON_REDIS_PORT", "6380");
    if (!env.contains("CORPCRON_MYSQL_PORT")) env.insert("CORPCRON_MYSQL_PORT", "3307");
    if (!env.contains("CORPCRON_MYSQL_PASSWORD")) {
        env.insert("CORPCRON_MYSQL_PASSWORD", "corpcron_dev_password");
    }
    return env;
}

void MainWindow::runTool(const QString& program, const QStringList& arguments, const QString& label) {
    if (toolProcess->state() != QProcess::NotRunning) {
        appendToolOutput("[已有工具命令正在运行]\n");
        return;
    }
    QString root = projectRoot();
    toolProcess->setWorkingDirectory(root);
    toolProcess->setProcessEnvironment(processEnvironment());
    appendToolOutput(QString("\n$ %1 %2\n").arg(program, arguments.join(' ')));
    appendLog(label + " 已启动");
    toolProcess->start(program, arguments);
    updateUiState();
}

void MainWindow::startServerProcess(QProcess *process, const QString& configPath, const QString& label) {
    if (process->state() != QProcess::NotRunning) {
        appendToolOutput("[" + label + " 已在运行]\n");
        return;
    }
    QString root = projectRoot();
    process->setWorkingDirectory(root);
    QProcessEnvironment env = processEnvironment();
    if (configPath == "config/server.conf") {
        env.insert("CORPCRON_SERVER_LISTEN_PORT", QString::number(portSpin->value()));
    }
    process->setProcessEnvironment(env);
    appendToolOutput(QString("\n$ ./build/corpcron_server --config %1\n").arg(configPath));
    if (!tokenEdit->text().trimmed().isEmpty()) {
        appendToolOutput("[env] CORPCRON_RPC_AUTH_TOKEN=<configured>\n");
    }
    process->start(root + "/build/corpcron_server", {"--config", configPath});
    updateUiState();
}

void MainWindow::appendToolOutput(const QString& message) {
    toolOutput->moveCursor(QTextCursor::End);
    toolOutput->insertPlainText(message);
    toolOutput->moveCursor(QTextCursor::End);
}

void MainWindow::appendLog(const QString& message) {
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
    logView->appendPlainText(QString("[%1] %2").arg(ts, message));
}

void MainWindow::updateUiState() {
    bool connected = socket && socket->state() == QAbstractSocket::ConnectedState;
    bool connecting = socket && socket->state() == QAbstractSocket::ConnectingState;
    connectBtn->setEnabled(!connected && !connecting);
    disconnectBtn->setEnabled(connected || connecting);
    echoBtn->setEnabled(connected);
    healthCheckBtn->setEnabled(connected);
    streamMetricsBtn->setEnabled(connected);
    submitBtn->setEnabled(connected);
    cancelBtn->setEnabled(connected);
    updateTaskBtn->setEnabled(connected);
    enableTaskBtn->setEnabled(connected);
    disableTaskBtn->setEnabled(connected);
    deleteTaskBtn->setEnabled(connected);
    runNowBtn->setEnabled(connected);
    refreshTasksBtn->setEnabled(connected);
    prevTasksPageBtn->setEnabled(connected && tasksPageOffset_ > 0);
    nextTasksPageBtn->setEnabled(connected && tasksPageOffset_ + tasksLimit_ < tasksTotal_);
    refreshHistoryBtn->setEnabled(connected);
    prevHistoryPageBtn->setEnabled(connected && historyPageOffset_ > 0);
    nextHistoryPageBtn->setEnabled(connected && historyPageOffset_ + historyLimit_ < historyTotal_);
    refreshServicesBtn->setEnabled(connected);
    refreshMetricsBtn->setEnabled(connected);
    authFailureBtn->setEnabled(connected);
    unknownRpcBtn->setEnabled(connected);
    badFrameBtn->setEnabled(connected);

    bool toolIdle = toolProcess && toolProcess->state() == QProcess::NotRunning;
    bool serverIdle = serverProcess && serverProcess->state() == QProcess::NotRunning;
    bool server2Idle = server2Process && server2Process->state() == QProcess::NotRunning;
    startDepsBtn->setEnabled(toolIdle);
    stopDepsBtn->setEnabled(toolIdle);
    resetDepsBtn->setEnabled(toolIdle);
    demoCheckBtn->setEnabled(toolIdle);
    cleanDataBtn->setEnabled(toolIdle);
    runCheckBtn->setEnabled(toolIdle);
    runIntegrationCheckBtn->setEnabled(toolIdle);
    dockerBuildBtn->setEnabled(toolIdle);
    showBenchmarkResultBtn->setEnabled(toolIdle);
    showDeployDocBtn->setEnabled(toolIdle);
    startMonitoringBtn->setEnabled(toolIdle);
    stopMonitoringBtn->setEnabled(toolIdle);
    openPrometheusBtn->setEnabled(true);
    openAlertmanagerBtn->setEnabled(true);
    openGrafanaBtn->setEnabled(true);
    showAlertsBtn->setEnabled(toolIdle);
    showRawMetricsBtn->setEnabled(toolIdle);
    showRedisSnapshotBtn->setEnabled(toolIdle);
    showMySQLSnapshotBtn->setEnabled(toolIdle);
    benchmarkShortBtn->setEnabled(toolIdle);
    benchmarkReuseBtn->setEnabled(toolIdle);
    deadlineCancellationBtn->setEnabled(toolIdle);
    halfOpenProbeBtn->setEnabled(toolIdle);
    streamingStubBtn->setEnabled(toolIdle);
    startServerBtn->setEnabled(serverIdle);
    stopServerBtn->setEnabled(!serverIdle);
    startSecondServerBtn->setEnabled(server2Idle);
    stopSecondServerBtn->setEnabled(!server2Idle);
    statusLabel->setText(connected ? "已连接" : (connecting ? "连接中" : "未连接"));
    if (overviewConnectionValue) {
        overviewConnectionValue->setText(connected ? "已连接" : (connecting ? "连接中" : "未连接"));
    }
}

void MainWindow::handleFrame(uint32_t serialId, const std::string& payload) {
    if (serialId == corpcron::rpc::kRpcErrorSerialId) {
        corpcron::rpc::RpcError error;
        if (error.ParseFromString(payload)) {
            appendLog(QString("RPC 错误 code=%1 message=%2")
                          .arg(error.code())
                          .arg(QString::fromStdString(error.message())));
        } else {
            appendLog("RPC 错误响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kEchoResponseSerialId) {
        corpcron::rpc::EchoResponse resp;
        if (resp.ParseFromString(payload)) {
            appendLog("Echo 响应: " + QString::fromStdString(resp.message()));
        } else {
            appendLog("Echo 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kSubmitTaskResponseSerialId) {
        corpcron::rpc::SubmitTaskResponse resp;
        if (resp.ParseFromString(payload)) {
            if (resp.success()) {
                QString taskId = QString::fromStdString(resp.task_id());
                cancelTaskIdEdit->setText(taskId);
                historyTaskIdEdit->setText(taskId);
                appendLog("任务提交成功 task_id=" + taskId);
                onRefreshTasks();
            } else {
                appendLog("任务提交失败: " + QString::fromStdString(resp.error()));
            }
        } else {
            appendLog("SubmitTask 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kCancelTaskResponseSerialId) {
        corpcron::rpc::CancelTaskResponse resp;
        if (resp.ParseFromString(payload)) {
            appendLog(resp.success()
                          ? "任务取消成功"
                          : "任务取消失败: " + QString::fromStdString(resp.error()));
            if (resp.success()) onRefreshTasks();
        } else {
            appendLog("CancelTask 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kListTasksResponseSerialId) {
        corpcron::rpc::ListTasksResponse resp;
        if (resp.ParseFromString(payload)) {
            if (resp.success()) {
                populateTasks(resp);
                appendLog(QString("任务列表已刷新，本页 %1 条 / 共 %2 条")
                              .arg(resp.tasks_size())
                              .arg(resp.total()));
            } else {
                appendLog("任务列表刷新失败: " + QString::fromStdString(resp.error()));
            }
        } else {
            appendLog("ListTasks 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kListHistoryResponseSerialId) {
        corpcron::rpc::ListHistoryResponse resp;
        if (resp.ParseFromString(payload)) {
            if (resp.success()) {
                populateHistory(resp);
                appendLog(QString("执行历史已刷新，本页 %1 条 / 共 %2 条")
                              .arg(resp.history_size())
                              .arg(resp.total()));
            } else {
                appendLog("执行历史刷新失败: " + QString::fromStdString(resp.error()));
            }
        } else {
            appendLog("ListHistory 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kListServicesResponseSerialId) {
        corpcron::rpc::ListServicesResponse resp;
        if (resp.ParseFromString(payload)) {
            if (resp.success()) {
                populateServices(resp);
                appendLog(QString("服务发现已刷新，共 %1 个节点").arg(resp.endpoints_size()));
            } else {
                appendLog("服务发现刷新失败: " + QString::fromStdString(resp.error()));
            }
        } else {
            appendLog("ListServices 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kUpdateTaskResponseSerialId) {
        corpcron::rpc::UpdateTaskResponse resp;
        if (resp.ParseFromString(payload)) {
            appendLog(resp.success()
                          ? "任务修改成功"
                          : "任务修改失败: " + QString::fromStdString(resp.error()));
            if (resp.success()) onRefreshTasks();
        } else {
            appendLog("UpdateTask 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kEnableTaskResponseSerialId) {
        corpcron::rpc::EnableTaskResponse resp;
        if (resp.ParseFromString(payload)) {
            appendLog(resp.success()
                          ? "任务状态修改成功"
                          : "任务状态修改失败: " + QString::fromStdString(resp.error()));
            if (resp.success()) onRefreshTasks();
        } else {
            appendLog("EnableTask 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kDeleteTaskResponseSerialId) {
        corpcron::rpc::DeleteTaskResponse resp;
        if (resp.ParseFromString(payload)) {
            appendLog(resp.success()
                          ? "任务删除成功"
                          : "任务删除失败: " + QString::fromStdString(resp.error()));
            if (resp.success()) {
                editTaskIdEdit->clear();
                cancelTaskIdEdit->clear();
                historyTaskIdEdit->clear();
                onRefreshTasks();
            }
        } else {
            appendLog("DeleteTask 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kRunTaskNowResponseSerialId) {
        corpcron::rpc::RunTaskNowResponse resp;
        if (resp.ParseFromString(payload)) {
            appendLog(resp.success()
                          ? "立即执行成功: " + QString::fromStdString(resp.result())
                          : "立即执行失败: " + QString::fromStdString(resp.error()));
            onRefreshHistory();
            onRefreshTasks();
        } else {
            appendLog("RunTaskNow 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kGetMetricsResponseSerialId) {
        corpcron::rpc::GetMetricsResponse resp;
        if (resp.ParseFromString(payload)) {
            if (resp.success()) {
                populateMetrics(resp);
                appendLog("运行指标已刷新");
            } else {
                appendLog("运行指标刷新失败: " + QString::fromStdString(resp.error()));
            }
        } else {
            appendLog("GetMetrics 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kHealthCheckResponseSerialId) {
        corpcron::rpc::HealthCheckResponse resp;
        if (resp.ParseFromString(payload)) {
            appendLog(QString("HealthCheck: serving=%1 status=%2 node=%3 time=%4")
                          .arg(resp.serving() ? "true" : "false")
                          .arg(QString::fromStdString(resp.status()))
                          .arg(QString::fromStdString(resp.node_id()))
                          .arg(resp.unix_time_ms()));
        } else {
            appendLog("HealthCheck 响应解析失败");
        }
        return;
    }

    if (serialId == corpcron::rpc::kStreamMetricsResponseSerialId) {
        corpcron::rpc::StreamMetricsResponse resp;
        if (resp.ParseFromString(payload)) {
            appendLog(QString("StreamMetrics frame=%1 end=%2 rpc_requests=%3 rpc_errors=%4")
                          .arg(resp.sequence())
                          .arg(resp.end_of_stream() ? "true" : "false")
                          .arg(resp.has_metrics() ? resp.metrics().rpc_requests_total() : 0)
                          .arg(resp.has_metrics() ? resp.metrics().rpc_error_total() : 0));
            if (resp.has_metrics()) {
                populateMetrics(resp.metrics());
            }
            if (resp.end_of_stream()) {
                appendLog("StreamMetrics 流结束");
            }
        } else {
            appendLog("StreamMetrics 响应解析失败");
        }
        return;
    }

    appendLog(QString("未知响应 serial_id=%1 payload_size=%2")
                  .arg(serialId)
                  .arg(payload.size()));
}

void MainWindow::populateTasks(const corpcron::rpc::ListTasksResponse& response) {
    tasksTable->setRowCount(response.tasks_size());
    tasksTotal_ = response.total();
    tasksPageOffset_ = response.offset();
    tasksLimit_ = response.limit() > 0 ? response.limit() : taskPageSizeSpin->value();
    if (overviewTaskCountValue) overviewTaskCountValue->setText(QString::number(tasksTotal_));
    for (int row = 0; row < response.tasks_size(); ++row) {
        const auto& task = response.tasks(row);
        QString status = "未知";
        if (task.status() == 0) status = "停用";
        if (task.status() == 1) status = "待调度";
        if (task.status() == 2) status = "执行中";
        QStringList values{
            QString::fromStdString(task.id()),
            QString::fromStdString(task.handler()),
            status,
            QString::fromStdString(task.cron_expr()),
            QString::fromStdString(task.next_run_at()),
            QString::fromStdString(task.last_run_at()),
            QString::number(task.retry_count()),
            QString::number(task.max_retries()),
            QString::fromStdString(task.current_execution_id()),
            QString::fromStdString(task.running_node()),
            QString::fromStdString(task.started_at()),
            QString::fromStdString(task.params())
        };
        for (int col = 0; col < values.size(); ++col) {
            auto *cell = new QTableWidgetItem(values[col]);
            cell->setToolTip(values[col]);
            tasksTable->setItem(row, col, cell);
        }
    }
    updateTaskPaginationUi();
}

void MainWindow::populateHistory(const corpcron::rpc::ListHistoryResponse& response) {
    historyTable->setRowCount(response.history_size());
    historyTotal_ = response.total();
    historyPageOffset_ = response.offset();
    historyLimit_ = response.limit() > 0 ? response.limit() : historyLimitSpin->value();
    if (overviewHistoryCountValue) overviewHistoryCountValue->setText(QString::number(historyTotal_));
    for (int row = 0; row < response.history_size(); ++row) {
        const auto& item = response.history(row);
        QStringList values{
            QString::fromStdString(item.execution_id()),
            QString::fromStdString(item.task_id()),
            QString::fromStdString(item.exec_node()),
            item.success() ? "成功" : "失败",
            QString::fromStdString(item.result()),
            QString::fromStdString(item.error()),
            QString::fromStdString(item.start_time()),
            QString::fromStdString(item.end_time())
        };
        for (int col = 0; col < values.size(); ++col) {
            auto *cell = new QTableWidgetItem(values[col]);
            cell->setToolTip(values[col]);
            historyTable->setItem(row, col, cell);
        }
    }
    if (historyDetailView && response.history_size() == 0) {
        historyDetailView->clear();
    }
    updateHistoryPaginationUi();
}

void MainWindow::populateServices(const corpcron::rpc::ListServicesResponse& response) {
    servicesTable->setRowCount(response.endpoints_size());
    if (overviewServiceCountValue) overviewServiceCountValue->setText(QString::number(response.endpoints_size()));
    for (int row = 0; row < response.endpoints_size(); ++row) {
        servicesTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(response.endpoints(row))));
    }
}

void MainWindow::populateMetrics(const corpcron::rpc::GetMetricsResponse& response) {
    if (overviewRpcRequestsValue) overviewRpcRequestsValue->setText(QString::number(response.rpc_requests_total()));
    if (overviewRpcErrorsValue) overviewRpcErrorsValue->setText(QString::number(response.rpc_error_total()));
    if (overviewActiveConnectionsValue) overviewActiveConnectionsValue->setText(QString::number(response.active_connections()));
    if (overviewTaskSuccessValue) overviewTaskSuccessValue->setText(QString::number(response.task_success_total()));
    if (overviewTaskFailureValue) overviewTaskFailureValue->setText(QString::number(response.task_failure_total()));

    struct MetricRow {
        const char* name;
        qulonglong value;
    };
    const QVector<MetricRow> rows{
        {"rpc_requests_total", response.rpc_requests_total()},
        {"rpc_success_total", response.rpc_success_total()},
        {"rpc_error_total", response.rpc_error_total()},
        {"active_connections", response.active_connections()},
        {"rejected_connections", response.rejected_connections()},
        {"malformed_frames", response.malformed_frames()},
        {"bytes_in_total", response.bytes_in_total()},
        {"bytes_out_total", response.bytes_out_total()},
        {"task_success_total", response.task_success_total()},
        {"task_failure_total", response.task_failure_total()},
        {"lock_acquire_success_total", response.lock_acquire_success_total()},
        {"lock_acquire_failure_total", response.lock_acquire_failure_total()},
        {"max_task_duration_ms", response.max_task_duration_ms()},
        {"task_duration_p95_ms", response.task_duration_p95_ms()},
        {"task_duration_p99_ms", response.task_duration_p99_ms()},
        {"task_duration_samples_total", response.task_duration_samples_total()},
        {"schedule_delay_max_ms", response.schedule_delay_max_ms()},
        {"schedule_delay_p95_ms", response.schedule_delay_p95_ms()},
        {"schedule_delay_p99_ms", response.schedule_delay_p99_ms()},
        {"schedule_delay_samples_total", response.schedule_delay_samples_total()}
    };
    metricsTable->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        metricsTable->setItem(row, 0, new QTableWidgetItem(rows[row].name));
        metricsTable->setItem(row, 1, new QTableWidgetItem(QString::number(rows[row].value)));
    }
}

void MainWindow::updateTaskPaginationUi() {
    const int start = tasksTotal_ == 0 ? 0 : tasksPageOffset_ + 1;
    const int end = std::min(tasksTotal_, tasksPageOffset_ + tasksLimit_);
    if (tasksPageLabel) {
        tasksPageLabel->setText(QString("第 %1-%2 条 / 共 %3 条")
                                    .arg(start)
                                    .arg(end)
                                    .arg(tasksTotal_));
    }
    const bool connected = socket && socket->state() == QAbstractSocket::ConnectedState;
    if (prevTasksPageBtn) prevTasksPageBtn->setEnabled(connected && tasksPageOffset_ > 0);
    if (nextTasksPageBtn) {
        nextTasksPageBtn->setEnabled(connected && tasksPageOffset_ + tasksLimit_ < tasksTotal_);
    }
}

void MainWindow::updateHistoryPaginationUi() {
    const int start = historyTotal_ == 0 ? 0 : historyPageOffset_ + 1;
    const int end = std::min(historyTotal_, historyPageOffset_ + historyLimit_);
    if (historyPageLabel) {
        historyPageLabel->setText(QString("第 %1-%2 条 / 共 %3 条")
                                      .arg(start)
                                      .arg(end)
                                      .arg(historyTotal_));
    }
    const bool connected = socket && socket->state() == QAbstractSocket::ConnectedState;
    if (prevHistoryPageBtn) prevHistoryPageBtn->setEnabled(connected && historyPageOffset_ > 0);
    if (nextHistoryPageBtn) {
        nextHistoryPageBtn->setEnabled(connected && historyPageOffset_ + historyLimit_ < historyTotal_);
    }
}
