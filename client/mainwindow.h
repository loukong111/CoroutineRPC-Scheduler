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
#include <QComboBox>
#include <QTableWidget>
#include <QTimer>
#include <QProcess>
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
    void onTaskFilterChanged();
    void onPrevTasksPage();
    void onNextTasksPage();
    void onRefreshHistory();
    void onHistoryFilterChanged();
    void onPrevHistoryPage();
    void onNextHistoryPage();
    void onRefreshServices();
    void onRefreshMetrics();
    void onHealthCheck();
    void onStreamMetrics();
    void onStartDependencies();
    void onStopDependencies();
    void onResetDependencies();
    void onStartServer();
    void onStopServer();
    void onStartSecondServer();
    void onStopSecondServer();
    void onDemoCheck();
    void onCleanDemoData();
    void onRunCheck();
    void onRunIntegrationCheck();
    void onDockerBuild();
    void onShowBenchmarkResult();
    void onShowDeployDoc();
    void onStartMonitoringStack();
    void onStopMonitoringStack();
    void onOpenPrometheus();
    void onOpenAlertmanager();
    void onOpenGrafana();
    void onShowAlerts();
    void onShowRawMetrics();
    void onShowRedisSnapshot();
    void onShowMySQLSnapshot();
    void onBenchmarkShort();
    void onBenchmarkReuse();
    void onDeadlineCancellationDemo();
    void onHalfOpenProbeDemo();
    void onStreamingStubDemo();
    void onAuthFailureTest();
    void onUnknownRpcTest();
    void onBadFrameTest();
    void onTaskSelectionChanged();
    void onHistorySelectionChanged();
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

    QLabel *overviewConnectionValue;
    QLabel *overviewTaskCountValue;
    QLabel *overviewHistoryCountValue;
    QLabel *overviewServiceCountValue;
    QLabel *overviewRpcRequestsValue;
    QLabel *overviewRpcErrorsValue;
    QLabel *overviewActiveConnectionsValue;
    QLabel *overviewTaskSuccessValue;
    QLabel *overviewTaskFailureValue;

    QLineEdit *echoEdit;
    QPushButton *echoBtn;
    QPushButton *healthCheckBtn;
    QSpinBox *streamSamplesSpin;
    QPushButton *streamMetricsBtn;

    QLineEdit *cronEdit;
    QTextEdit *paramsEdit;
    QLineEdit *handlerEdit;
    QPushButton *submitBtn;

    QTableWidget *tasksTable;
    QCheckBox *enabledOnlyCheck;
    QComboBox *taskStatusCombo;
    QLineEdit *taskKeywordEdit;
    QSpinBox *taskPageSizeSpin;
    QPushButton *prevTasksPageBtn;
    QPushButton *nextTasksPageBtn;
    QLabel *tasksPageLabel;
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
    QComboBox *historySuccessCombo;
    QLineEdit *historyKeywordEdit;
    QPushButton *prevHistoryPageBtn;
    QPushButton *nextHistoryPageBtn;
    QLabel *historyPageLabel;
    QPushButton *refreshHistoryBtn;
    QTableWidget *historyTable;
    QPlainTextEdit *historyDetailView;

    QLineEdit *serviceNameEdit;
    QPushButton *refreshServicesBtn;
    QTableWidget *servicesTable;

    QPushButton *refreshMetricsBtn;
    QTableWidget *metricsTable;

    QPushButton *startDepsBtn;
    QPushButton *stopDepsBtn;
    QPushButton *resetDepsBtn;
    QPushButton *startServerBtn;
    QPushButton *stopServerBtn;
    QPushButton *startSecondServerBtn;
    QPushButton *stopSecondServerBtn;
    QPushButton *demoCheckBtn;
    QPushButton *cleanDataBtn;
    QPushButton *runCheckBtn;
    QPushButton *runIntegrationCheckBtn;
    QPushButton *dockerBuildBtn;
    QPushButton *showBenchmarkResultBtn;
    QPushButton *showDeployDocBtn;
    QPushButton *startMonitoringBtn;
    QPushButton *stopMonitoringBtn;
    QPushButton *openPrometheusBtn;
    QPushButton *openAlertmanagerBtn;
    QPushButton *openGrafanaBtn;
    QPushButton *showAlertsBtn;
    QPushButton *showRawMetricsBtn;
    QPushButton *showRedisSnapshotBtn;
    QPushButton *showMySQLSnapshotBtn;
    QSpinBox *benchConcurrencySpin;
    QSpinBox *benchRequestsSpin;
    QPushButton *benchmarkShortBtn;
    QPushButton *benchmarkReuseBtn;
    QPushButton *deadlineCancellationBtn;
    QPushButton *halfOpenProbeBtn;
    QPushButton *streamingStubBtn;
    QPushButton *authFailureBtn;
    QPushButton *unknownRpcBtn;
    QPushButton *badFrameBtn;
    QPlainTextEdit *toolOutput;

    QPlainTextEdit *logView;
    QPushButton *clearLogBtn;

    QTcpSocket *socket;
    QProcess *toolProcess;
    QProcess *serverProcess;
    QProcess *server2Process;
    QByteArray recvBuffer;
    QTimer *autoRefreshTimer;
    int tasksPageOffset_ = 0;
    int tasksTotal_ = 0;
    int tasksLimit_ = 50;
    int historyPageOffset_ = 0;
    int historyTotal_ = 0;
    int historyLimit_ = 50;

    bool sendFrame(uint32_t serialId, const std::string& payload, const QString& action);
    std::string authToken() const;
    QString projectRoot() const;
    QProcessEnvironment processEnvironment() const;
    void runTool(const QString& program, const QStringList& arguments, const QString& label);
    void startServerProcess(QProcess *process, const QString& configPath, const QString& label);
    void appendToolOutput(const QString& message);
    void appendLog(const QString& message);
    void updateUiState();
    void handleFrame(uint32_t serialId, const std::string& payload);
    void populateTasks(const corpcron::rpc::ListTasksResponse& response);
    void populateHistory(const corpcron::rpc::ListHistoryResponse& response);
    void populateServices(const corpcron::rpc::ListServicesResponse& response);
    void populateMetrics(const corpcron::rpc::GetMetricsResponse& response);
    void updateTaskPaginationUi();
    void updateHistoryPaginationUi();
};

#endif
