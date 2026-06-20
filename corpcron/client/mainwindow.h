#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnect();
    void onDisconnect();
    void onEcho();
    void onSubmit();
    void onCancelTask();
    void onReadyRead();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    QLineEdit *hostEdit;
    QSpinBox *portSpin;
    QLineEdit *tokenEdit;
    QPushButton *connectBtn;
    QPushButton *disconnectBtn;
    QLabel *statusLabel;

    QLineEdit *echoEdit;
    QPushButton *echoBtn;

    QLineEdit *cronEdit;
    QTextEdit *paramsEdit;
    QLineEdit *handlerEdit;
    QPushButton *submitBtn;

    QLineEdit *cancelTaskIdEdit;
    QPushButton *cancelBtn;

    QPlainTextEdit *logView;
    QPushButton *clearLogBtn;

    QTcpSocket *socket;
    QByteArray recvBuffer;

    bool sendFrame(uint32_t serialId, const std::string& payload, const QString& action);
    std::string authToken() const;
    void appendLog(const QString& message);
    void updateUiState();
    void handleFrame(uint32_t serialId, const std::string& payload);
};

#endif
