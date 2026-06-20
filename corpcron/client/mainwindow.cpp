#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDateTime>
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

    auto *echoBox = new QGroupBox("Echo 测试");
    auto *echoLayout = new QHBoxLayout(echoBox);
    echoEdit = new QLineEdit("hello");
    echoBtn = new QPushButton("发送 Echo");
    echoLayout->addWidget(echoEdit);
    echoLayout->addWidget(echoBtn);
    layout->addWidget(echoBox);

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
    layout->addWidget(submitBox);

    auto *cancelBox = new QGroupBox("取消任务");
    auto *cancelLayout = new QHBoxLayout(cancelBox);
    cancelTaskIdEdit = new QLineEdit;
    cancelTaskIdEdit->setPlaceholderText("task_id");
    cancelBtn = new QPushButton("取消任务");
    cancelLayout->addWidget(cancelTaskIdEdit);
    cancelLayout->addWidget(cancelBtn);
    layout->addWidget(cancelBox);

    auto *logBox = new QGroupBox("响应日志");
    auto *logLayout = new QVBoxLayout(logBox);
    logView = new QPlainTextEdit;
    logView->setReadOnly(true);
    clearLogBtn = new QPushButton("清空日志");
    logLayout->addWidget(logView);
    logLayout->addWidget(clearLogBtn);
    layout->addWidget(logBox, 1);

    connect(connectBtn, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    connect(echoBtn, &QPushButton::clicked, this, &MainWindow::onEcho);
    connect(submitBtn, &QPushButton::clicked, this, &MainWindow::onSubmit);
    connect(cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancelTask);
    connect(clearLogBtn, &QPushButton::clicked, logView, &QPlainTextEdit::clear);

    socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::readyRead, this, &MainWindow::onReadyRead);
    connect(socket, &QTcpSocket::connected, this, &MainWindow::onSocketConnected);
    connect(socket, &QTcpSocket::disconnected, this, &MainWindow::onSocketDisconnected);
    connect(socket, &QTcpSocket::errorOccurred, this, &MainWindow::onSocketError);
    updateUiState();
}

MainWindow::~MainWindow() {
    if (socket->state() == QAbstractSocket::ConnectedState)
        socket->disconnectFromHost();
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
    if (cancelTaskIdEdit->text().isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写 task_id");
        return;
    }
    corpcron::rpc::CancelTaskRequest req;
    req.set_task_id(cancelTaskIdEdit->text().toStdString());
    req.set_auth_token(authToken());

    std::string payload;
    req.SerializeToString(&payload);
    sendFrame(corpcron::rpc::kCancelTaskRequestSerialId, payload, "CancelTask");
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
                appendLog("任务提交成功 task_id=" + taskId);
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
        } else {
            appendLog("CancelTask 响应解析失败");
        }
        return;
    }

    appendLog(QString("未知响应 serial_id=%1 payload_size=%2")
                  .arg(serialId)
                  .arg(payload.size()));
}
