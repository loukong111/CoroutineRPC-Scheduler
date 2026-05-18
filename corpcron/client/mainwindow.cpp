#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QHostAddress>
#include "rpc.pb.h"

static QByteArray encode(uint32_t serialId, const std::string& payload) {
    uint32_t totalLen = 4 + payload.size();
    QByteArray buf;
    buf.resize(4 + totalLen);
    buf[0] = (totalLen >> 24) & 0xFF;
    buf[1] = (totalLen >> 16) & 0xFF;
    buf[2] = (totalLen >> 8) & 0xFF;
    buf[3] = totalLen & 0xFF;
    buf[4] = (serialId >> 24) & 0xFF;
    buf[5] = (serialId >> 16) & 0xFF;
    buf[6] = (serialId >> 8) & 0xFF;
    buf[7] = serialId & 0xFF;
    memcpy(buf.data() + 8, payload.data(), payload.size());
    return buf;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);

    cronEdit = new QLineEdit;
    cronEdit->setPlaceholderText("Cron表达式，例如: * * * * * ?");
    paramsEdit = new QTextEdit;
    paramsEdit->setPlaceholderText("参数 (字符串)");
    handlerEdit = new QLineEdit;
    handlerEdit->setPlaceholderText("处理器名称，例如: Echo");
    submitBtn = new QPushButton("提交任务");
    resultLabel = new QLabel("等待提交");

    layout->addWidget(new QLabel("Cron表达式:"));
    layout->addWidget(cronEdit);
    layout->addWidget(new QLabel("参数:"));
    layout->addWidget(paramsEdit);
    layout->addWidget(new QLabel("处理器:"));
    layout->addWidget(handlerEdit);
    layout->addWidget(submitBtn);
    layout->addWidget(resultLabel);

    connect(submitBtn, &QPushButton::clicked, this, &MainWindow::onSubmit);

    socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::readyRead, this, &MainWindow::onReadyRead);
    socket->connectToHost(QHostAddress::LocalHost, 8081);
    if (!socket->waitForConnected(3000)) {
        QMessageBox::warning(this, "错误", "无法连接到服务器");
    }
}

MainWindow::~MainWindow() {
    if (socket->state() == QAbstractSocket::ConnectedState)
        socket->disconnectFromHost();
}

void MainWindow::onSubmit() {
    if (cronEdit->text().isEmpty() || handlerEdit->text().isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写 Cron 表达式和处理器名称");
        return;
    }
    sendRequest();
}

void MainWindow::sendRequest() {
    corpcron::rpc::SubmitTaskRequest req;
    req.set_cron_expr(cronEdit->text().toStdString());
    req.set_params(paramsEdit->toPlainText().toStdString());
    req.set_handler(handlerEdit->text().toStdString());

    std::string payload;
    req.SerializeToString(&payload);
    QByteArray data = encode(3, payload);
    socket->write(data);
    resultLabel->setText("已提交，等待响应...");
}

void MainWindow::onReadyRead() {
    recvBuffer.append(socket->readAll());
    if (recvBuffer.size() >= 8) {
        uint32_t totalLen = ((unsigned char)recvBuffer[0] << 24) |
                            ((unsigned char)recvBuffer[1] << 16) |
                            ((unsigned char)recvBuffer[2] << 8) |
                            (unsigned char)recvBuffer[3];
        if (recvBuffer.size() >= 4 + totalLen) {
            uint32_t serialId = ((unsigned char)recvBuffer[4] << 24) |
                                ((unsigned char)recvBuffer[5] << 16) |
                                ((unsigned char)recvBuffer[6] << 8) |
                                (unsigned char)recvBuffer[7];
            std::string payload(recvBuffer.constData() + 8, totalLen - 4);
            if (serialId == 4) {
                corpcron::rpc::SubmitTaskResponse resp;
                if (resp.ParseFromString(payload)) {
                    if (resp.success()) {
                        resultLabel->setText(QString("任务已提交，ID: %1").arg(QString::fromStdString(resp.task_id())));
                    } else {
                        resultLabel->setText(QString("提交失败: %1").arg(QString::fromStdString(resp.error())));
                    }
                }
            }
            recvBuffer.clear();
        }
    }
}