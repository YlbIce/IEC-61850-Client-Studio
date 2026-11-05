#pragma once
#include <QDialog>
#include <QVariantMap>
class ControlDialog : public QDialog {
    Q_OBJECT
public:
    ControlDialog(QVariantMap data, QWidget* parent = nullptr);
signals:
    void commandRequested(QVariantMap arguments);
public slots:
    void handleCompleted(QString operation, QVariantMap result);
    void handleError(QString operation, QString message);

private:
    QString reference_;
    class QLabel* outcome_;
};
