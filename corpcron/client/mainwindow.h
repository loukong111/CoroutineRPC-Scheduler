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
#include <QCheckBox>
#include <QTableWidget>
#include <QTimer>
#include "rpc.pb.h"

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
    void onUpdateTask();
    void onEnableSelectedTask();
    void onDisableSelectedTask();
    void onDeleteSelectedTask();
    void onRunSelectedTaskNow();
    void onRefreshTasks();
    void onRefreshHistory();
    void onRefreshServices();
    void onTaskSelectionChanged();
    void onAutoRefreshChanged(bool checked);
    void onAutoRefreshTick();
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

    QTableWidget *tasksTable;
    QCheckBox *enabledOnlyCheck;
    QPushButton *refreshTasksBtn;
    QCheckBox *autoRefreshCheck;

    QLineEdit *editTaskIdEdit;
    QLineEdit *editCronEdit;
    QLineEdit *editHandlerEdit;
    QTextEdit *editParamsEdit;
    QSpinBox *editMaxRetriesSpin;
    QPushButton *updateTaskBtn;
    QPushButton *enableTaskBtn;
    QPushButton *disableTaskBtn;
    QPushButton *deleteTaskBtn;
    QPushButton *runNowBtn;

    QLineEdit *cancelTaskIdEdit;
    QPushButton *cancelBtn;

    QLineEdit *historyTaskIdEdit;
    QSpinBox *historyLimitSpin;
    QPushButton *refreshHistoryBtn;
    QTableWidget *historyTable;

    QLineEdit *serviceNameEdit;
    QPushButton *refreshServicesBtn;
    QTableWidget *servicesTable;

    QPlainTextEdit *logView;
    QPushButton *clearLogBtn;

    QTcpSocket *socket;
    QByteArray recvBuffer;
    QTimer *autoRefreshTimer;

    bool sendFrame(uint32_t serialId, const std::string& payload, const QString& action);
    std::string authToken() const;
    void appendLog(const QString& message);
    void updateUiState();
    void handleFrame(uint32_t serialId, const std::string& payload);
    void populateTasks(const corpcron::rpc::ListTasksResponse& response);
    void populateHistory(const corpcron::rpc::ListHistoryResponse& response);
    void populateServices(const corpcron::rpc::ListServicesResponse& response);
};

#endif
