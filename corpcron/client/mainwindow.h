#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onSubmit();
    void onReadyRead();

private:
    QLineEdit *cronEdit;
    QTextEdit *paramsEdit;
    QLineEdit *handlerEdit;
    QPushButton *submitBtn;
    QLabel *resultLabel;
    QTcpSocket *socket;
    QByteArray recvBuffer;

    void sendRequest();
};

#endif