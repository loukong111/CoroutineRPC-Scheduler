#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QMessageBox>
#include <QDateTime>
#include <QStringList>
#include <QVector>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include "corpcron/rpc/protocol.hpp"
#include "rpc.pb.h"

static QByteArray encode(uint32_t serialId, const std::string& payload) {
    std::string frame;
    if (!corpcron::rpc::tryEncode(serialId, payload, frame)) {
        return {};
    }
    return QByteArray(frame.data(), static_cast<int>(frame.size()));
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("CorpCron RPC Client");
    resize(760, 640);

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);

    auto *connectionBox = new QGroupBox("连接配置");
    auto *connectionLayout = new QHBoxLayout(connectionBox);
    hostEdit = new QLineEdit("127.0.0.1");
    portSpin = new QSpinBox;
    portSpin->setRange(1, 65535);
    portSpin->setValue(8081);
    tokenEdit = new QLineEdit;
    tokenEdit->setPlaceholderText("Auth Token，可为空");
    connectBtn = new QPushButton("连接");
    disconnectBtn = new QPushButton("断开");
    statusLabel = new QLabel("未连接");
    connectionLayout->addWidget(new QLabel("Host"));
    connectionLayout->addWidget(hostEdit);
    connectionLayout->addWidget(new QLabel("Port"));
    connectionLayout->addWidget(portSpin);
    connectionLayout->addWidget(new QLabel("Token"));
    connectionLayout->addWidget(tokenEdit);
    connectionLayout->addWidget(connectBtn);
    connectionLayout->addWidget(disconnectBtn);
    connectionLayout->addWidget(statusLabel);
    layout->addWidget(connectionBox);

    auto *tabs = new QTabWidget;
    layout->addWidget(tabs, 1);

    auto *operationTab = new QWidget;
    auto *operationLayout = new QVBoxLayout(operationTab);

    auto *echoBox = new QGroupBox("Echo 测试");
    auto *echoLayout = new QHBoxLayout(echoBox);
    echoEdit = new QLineEdit("hello");
    echoBtn = new QPushButton("发送 Echo");
    echoLayout->addWidget(echoEdit);
    echoLayout->addWidget(echoBtn);
    operationLayout->addWidget(echoBox);

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
    cancelLayout->addWidget(cancelTaskIdEdit);
    cancelLayout->addWidget(cancelBtn);
    operationLayout->addWidget(cancelBox);
    operationLayout->addStretch(1);
    tabs->addTab(operationTab, "RPC 操作");

    auto *tasksTab = new QWidget;
    auto *tasksLayout = new QVBoxLayout(tasksTab);
    auto *tasksToolbar = new QHBoxLayout;
    enabledOnlyCheck = new QCheckBox("只看启用任务");
    autoRefreshCheck = new QCheckBox("自动刷新");
    refreshTasksBtn = new QPushButton("刷新任务");
    tasksToolbar->addWidget(enabledOnlyCheck);
    tasksToolbar->addWidget(autoRefreshCheck);
    tasksToolbar->addStretch(1);
    tasksToolbar->addWidget(refreshTasksBtn);
    tasksLayout->addLayout(tasksToolbar);
    tasksTable = new QTableWidget;
    tasksTable->setColumnCount(9);
    tasksTable->setHorizontalHeaderLabels({"ID", "Handler", "Status", "Cron", "Next Run", "Last Run", "Retry", "Max", "Params"});
    tasksTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tasksTable->setSelectionMode(QAbstractItemView::SingleSelection);
    tasksTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tasksTable->horizontalHeader()->setStretchLastSection(true);
    tasksTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tasksLayout->addWidget(tasksTable);

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
    editLayout->addRow("Max Retries", editMaxRetriesSpin);
    editLayout->addRow(taskButtons);
    tasksLayout->addWidget(editBox);
    tabs->addTab(tasksTab, "任务列表");

    auto *historyTab = new QWidget;
    auto *historyLayout = new QVBoxLayout(historyTab);
    auto *historyToolbar = new QHBoxLayout;
    historyTaskIdEdit = new QLineEdit;
    historyTaskIdEdit->setPlaceholderText("task_id 为空时查看最近历史");
    historyLimitSpin = new QSpinBox;
    historyLimitSpin->setRange(1, 500);
    historyLimitSpin->setValue(50);
    refreshHistoryBtn = new QPushButton("刷新历史");
    historyToolbar->addWidget(new QLabel("Task ID"));
    historyToolbar->addWidget(historyTaskIdEdit, 1);
    historyToolbar->addWidget(new QLabel("Limit"));
    historyToolbar->addWidget(historyLimitSpin);
    historyToolbar->addWidget(refreshHistoryBtn);
    historyLayout->addLayout(historyToolbar);
    historyTable = new QTableWidget;
    historyTable->setColumnCount(7);
    historyTable->setHorizontalHeaderLabels({"Task ID", "Node", "Success", "Result", "Error", "Start", "End"});
    historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyTable->horizontalHeader()->setStretchLastSection(true);
    historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    historyLayout->addWidget(historyTable);
    tabs->addTab(historyTab, "执行历史");

    auto *servicesTab = new QWidget;
    auto *servicesLayout = new QVBoxLayout(servicesTab);
    auto *servicesToolbar = new QHBoxLayout;
    serviceNameEdit = new QLineEdit("rpc");
    refreshServicesBtn = new QPushButton("刷新服务");
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
    servicesTable->horizontalHeader()->setStretchLastSection(true);
    servicesLayout->addWidget(servicesTable);
    tabs->addTab(servicesTab, "服务发现");

    auto *metricsTab = new QWidget;
    auto *metricsLayout = new QVBoxLayout(metricsTab);
    auto *metricsToolbar = new QHBoxLayout;
    refreshMetricsBtn = new QPushButton("刷新指标");
    metricsToolbar->addStretch(1);
    metricsToolbar->addWidget(refreshMetricsBtn);
    metricsLayout->addLayout(metricsToolbar);
    metricsTable = new QTableWidget;
    metricsTable->setColumnCount(2);
    metricsTable->setHorizontalHeaderLabels({"Metric", "Value"});
    metricsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    metricsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    metricsTable->horizontalHeader()->setStretchLastSection(true);
    metricsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    metricsLayout->addWidget(metricsTable);
    tabs->addTab(metricsTab, "运行指标");

    auto *demoTab = new QWidget;
    auto *demoLayout = new QVBoxLayout(demoTab);
    auto *runtimeBox = new QGroupBox("环境与服务");
    auto *runtimeLayout = new QHBoxLayout(runtimeBox);
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
    runtimeLayout->addWidget(startDepsBtn);
    runtimeLayout->addWidget(stopDepsBtn);
    runtimeLayout->addWidget(resetDepsBtn);
    runtimeLayout->addWidget(startServerBtn);
    runtimeLayout->addWidget(stopServerBtn);
    runtimeLayout->addWidget(startSecondServerBtn);
    runtimeLayout->addWidget(stopSecondServerBtn);
    runtimeLayout->addWidget(demoCheckBtn);
    runtimeLayout->addWidget(cleanDataBtn);
    runtimeLayout->addWidget(runCheckBtn);
    runtimeLayout->addWidget(runIntegrationCheckBtn);
    runtimeLayout->addWidget(dockerBuildBtn);
    runtimeLayout->addWidget(showBenchmarkResultBtn);
    runtimeLayout->addWidget(showDeployDocBtn);
    demoLayout->addWidget(runtimeBox);

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
    benchmarkLayout->addWidget(new QLabel("Concurrency"));
    benchmarkLayout->addWidget(benchConcurrencySpin);
    benchmarkLayout->addWidget(new QLabel("Requests"));
    benchmarkLayout->addWidget(benchRequestsSpin);
    benchmarkLayout->addWidget(benchmarkShortBtn);
    benchmarkLayout->addWidget(benchmarkReuseBtn);
    demoLayout->addWidget(benchmarkBox);

    auto *diagnosticBox = new QGroupBox("协议异常演示");
    auto *diagnosticLayout = new QHBoxLayout(diagnosticBox);
    authFailureBtn = new QPushButton("鉴权失败");
    unknownRpcBtn = new QPushButton("未知方法");
    badFrameBtn = new QPushButton("坏包断连");
    diagnosticLayout->addWidget(authFailureBtn);
    diagnosticLayout->addWidget(unknownRpcBtn);
    diagnosticLayout->addWidget(badFrameBtn);
    diagnosticLayout->addStretch(1);
    demoLayout->addWidget(diagnosticBox);

    toolOutput = new QPlainTextEdit;
    toolOutput->setReadOnly(true);
    demoLayout->addWidget(toolOutput, 1);
    tabs->addTab(demoTab, "演示控制台");

    auto *logBox = new QGroupBox("响应日志");
    auto *logLayout = new QVBoxLayout(logBox);
    logView = new QPlainTextEdit;
    logView->setReadOnly(true);
    clearLogBtn = new QPushButton("清空日志");
    logLayout->addWidget(logView);
    logLayout->addWidget(clearLogBtn);
    tabs->addTab(logBox, "日志");

    connect(connectBtn, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    connect(echoBtn, &QPushButton::clicked, this, &MainWindow::onEcho);
    connect(submitBtn, &QPushButton::clicked, this, &MainWindow::onSubmit);
    connect(cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancelTask);
    connect(updateTaskBtn, &QPushButton::clicked, this, &MainWindow::onUpdateTask);
    connect(enableTaskBtn, &QPushButton::clicked, this, &MainWindow::onEnableSelectedTask);
    connect(disableTaskBtn, &QPushButton::clicked, this, &MainWindow::onDisableSelectedTask);
    connect(deleteTaskBtn, &QPushButton::clicked, this, &MainWindow::onDeleteSelectedTask);
    connect(runNowBtn, &QPushButton::clicked, this, &MainWindow::onRunSelectedTaskNow);
    connect(refreshTasksBtn, &QPushButton::clicked, this, &MainWindow::onRefreshTasks);
    connect(refreshHistoryBtn, &QPushButton::clicked, this, &MainWindow::onRefreshHistory);
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
    connect(benchmarkShortBtn, &QPushButton::clicked, this, &MainWindow::onBenchmarkShort);
    connect(benchmarkReuseBtn, &QPushButton::clicked, this, &MainWindow::onBenchmarkReuse);
    connect(authFailureBtn, &QPushButton::clicked, this, &MainWindow::onAuthFailureTest);
    connect(unknownRpcBtn, &QPushButton::clicked, this, &MainWindow::onUnknownRpcTest);
    connect(badFrameBtn, &QPushButton::clicked, this, &MainWindow::onBadFrameTest);
    connect(tasksTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onTaskSelectionChanged);
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
        appendToolOutput(QString("\n[tool finished] exit=%1 status=%2\n")
                             .arg(exitCode)
                             .arg(status == QProcess::NormalExit ? "normal" : "crash"));
        updateUiState();
    });

    serverProcess = new QProcess(this);
    serverProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(serverProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        appendToolOutput(QString::fromLocal8Bit(serverProcess->readAllStandardOutput()));
    });
    connect(serverProcess, &QProcess::started, this, [this]() {
        appendToolOutput("[server started]\n");
        updateUiState();
    });
    connect(serverProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        appendToolOutput(QString("\n[server finished] exit=%1 status=%2\n")
                             .arg(exitCode)
                             .arg(status == QProcess::NormalExit ? "normal" : "crash"));
        updateUiState();
    });

    server2Process = new QProcess(this);
    server2Process->setProcessChannelMode(QProcess::MergedChannels);
    connect(server2Process, &QProcess::readyReadStandardOutput, this, [this]() {
        appendToolOutput(QString::fromLocal8Bit(server2Process->readAllStandardOutput()));
    });
    connect(server2Process, &QProcess::started, this, [this]() {
        appendToolOutput("[server2 started]\n");
        updateUiState();
    });
    connect(server2Process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        appendToolOutput(QString("\n[server2 finished] exit=%1 status=%2\n")
                             .arg(exitCode)
                             .arg(status == QProcess::NormalExit ? "normal" : "crash"));
        updateUiState();
    });
    updateUiState();
}

MainWindow::~MainWindow() {
    if (socket->state() == QAbstractSocket::ConnectedState)
        socket->disconnectFromHost();
    if (serverProcess && serverProcess->state() != QProcess::NotRunning) {
        serverProcess->terminate();
        if (!serverProcess->waitForFinished(3000)) serverProcess->kill();
    }
    if (server2Process && server2Process->state() != QProcess::NotRunning) {
        server2Process->terminate();
        if (!server2Process->waitForFinished(3000)) server2Process->kill();
    }
    if (toolProcess && toolProcess->state() != QProcess::NotRunning) {
        toolProcess->kill();
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
            status = status_item->text() == "启用" ? 1 : 0;
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
    req.set_limit(200);
    req.set_enabled_only(enabledOnlyCheck->isChecked());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kListTasksRequestSerialId, payload, "ListTasks");
}

void MainWindow::onRefreshHistory() {
    corpcron::rpc::ListHistoryRequest req;
    req.set_auth_token(authToken());
    req.set_task_id(historyTaskIdEdit->text().trimmed().toStdString());
    req.set_limit(historyLimitSpin->value());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kListHistoryRequestSerialId, payload, "ListHistory");
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
    startServerProcess(serverProcess, "config/server.conf", "server");
}

void MainWindow::onStopServer() {
    if (serverProcess->state() == QProcess::NotRunning) {
        appendToolOutput("[server is not running by this Qt client]\n");
        return;
    }
    appendToolOutput("[stopping server]\n");
    serverProcess->terminate();
}

void MainWindow::onStartSecondServer() {
    startServerProcess(server2Process, "config/server2.conf", "server2");
}

void MainWindow::onStopSecondServer() {
    if (server2Process->state() == QProcess::NotRunning) {
        appendToolOutput("[server2 is not running by this Qt client]\n");
        return;
    }
    appendToolOutput("[stopping server2]\n");
    server2Process->terminate();
}

void MainWindow::onDemoCheck() {
    runTool("./scripts/demo_check.sh", {}, "demo_check");
}

void MainWindow::onCleanDemoData() {
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
            "show benchmark result");
}

void MainWindow::onShowDeployDoc() {
    runTool("bash", {"-lc", "printf '%s\\n' '== deploy.md ==' && cat docs/deploy.md && printf '%s\\n' '\\n== systemd/corpcron.service ==' && cat systemd/corpcron.service"},
            "show deploy doc");
}

void MainWindow::onBenchmarkShort() {
    runTool("./scripts/benchmark.sh",
            {hostEdit->text(), QString::number(portSpin->value()),
             QString::number(benchConcurrencySpin->value()),
             QString::number(benchRequestsSpin->value()), "short"},
            "benchmark short");
}

void MainWindow::onBenchmarkReuse() {
    runTool("./scripts/benchmark.sh",
            {hostEdit->text(), QString::number(portSpin->value()),
             QString::number(benchConcurrencySpin->value()),
             QString::number(benchRequestsSpin->value()), "reuse"},
            "benchmark reuse");
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
    if (auto* params = tasksTable->item(row, 8)) editParamsEdit->setPlainText(params->text());
    editTaskIdEdit->setText(id_item->text());
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
    if (!tokenEdit->text().trimmed().isEmpty()) {
        env.insert("CORPCRON_RPC_AUTH_TOKEN", tokenEdit->text().trimmed());
    }
    env.insert("CORPCRON_REDIS_PORT", "6380");
    env.insert("CORPCRON_MYSQL_PORT", "3307");
    env.insert("CORPCRON_MYSQL_PASSWORD", "corpcron_dev_password");
    return env;
}

void MainWindow::runTool(const QString& program, const QStringList& arguments, const QString& label) {
    if (toolProcess->state() != QProcess::NotRunning) {
        appendToolOutput("[tool is already running]\n");
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
        appendToolOutput("[" + label + " already running]\n");
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
        appendToolOutput(QString("[env] CORPCRON_RPC_AUTH_TOKEN=%1\n").arg(tokenEdit->text().trimmed()));
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
    submitBtn->setEnabled(connected);
    cancelBtn->setEnabled(connected);
    updateTaskBtn->setEnabled(connected);
    enableTaskBtn->setEnabled(connected);
    disableTaskBtn->setEnabled(connected);
    deleteTaskBtn->setEnabled(connected);
    runNowBtn->setEnabled(connected);
    refreshTasksBtn->setEnabled(connected);
    refreshHistoryBtn->setEnabled(connected);
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
    benchmarkShortBtn->setEnabled(toolIdle);
    benchmarkReuseBtn->setEnabled(toolIdle);
    startServerBtn->setEnabled(serverIdle);
    stopServerBtn->setEnabled(!serverIdle);
    startSecondServerBtn->setEnabled(server2Idle);
    stopSecondServerBtn->setEnabled(!server2Idle);
    statusLabel->setText(connected ? "已连接" : (connecting ? "连接中" : "未连接"));
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
                appendLog(QString("任务列表已刷新，共 %1 条").arg(resp.tasks_size()));
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
                appendLog(QString("执行历史已刷新，共 %1 条").arg(resp.history_size()));
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

    appendLog(QString("未知响应 serial_id=%1 payload_size=%2")
                  .arg(serialId)
                  .arg(payload.size()));
}

void MainWindow::populateTasks(const corpcron::rpc::ListTasksResponse& response) {
    tasksTable->setRowCount(response.tasks_size());
    for (int row = 0; row < response.tasks_size(); ++row) {
        const auto& task = response.tasks(row);
        QString status = task.status() == 1 ? "启用" : "取消";
        QStringList values{
            QString::fromStdString(task.id()),
            QString::fromStdString(task.handler()),
            status,
            QString::fromStdString(task.cron_expr()),
            QString::fromStdString(task.next_run_at()),
            QString::fromStdString(task.last_run_at()),
            QString::number(task.retry_count()),
            QString::number(task.max_retries()),
            QString::fromStdString(task.params())
        };
        for (int col = 0; col < values.size(); ++col) {
            tasksTable->setItem(row, col, new QTableWidgetItem(values[col]));
        }
    }
}

void MainWindow::populateHistory(const corpcron::rpc::ListHistoryResponse& response) {
    historyTable->setRowCount(response.history_size());
    for (int row = 0; row < response.history_size(); ++row) {
        const auto& item = response.history(row);
        QStringList values{
            QString::fromStdString(item.task_id()),
            QString::fromStdString(item.exec_node()),
            item.success() ? "成功" : "失败",
            QString::fromStdString(item.result()),
            QString::fromStdString(item.error()),
            QString::fromStdString(item.start_time()),
            QString::fromStdString(item.end_time())
        };
        for (int col = 0; col < values.size(); ++col) {
            historyTable->setItem(row, col, new QTableWidgetItem(values[col]));
        }
    }
}

void MainWindow::populateServices(const corpcron::rpc::ListServicesResponse& response) {
    servicesTable->setRowCount(response.endpoints_size());
    for (int row = 0; row < response.endpoints_size(); ++row) {
        servicesTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(response.endpoints(row))));
    }
}

void MainWindow::populateMetrics(const corpcron::rpc::GetMetricsResponse& response) {
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
        {"max_task_duration_ms", response.max_task_duration_ms()}
    };
    metricsTable->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        metricsTable->setItem(row, 0, new QTableWidgetItem(rows[row].name));
        metricsTable->setItem(row, 1, new QTableWidgetItem(QString::number(rows[row].value)));
    }
}
