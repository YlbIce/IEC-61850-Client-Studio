#pragma once
#include <QDialog>
#include <QVariantMap>
class ReportDialog : public QDialog {
    Q_OBJECT
public:
    ReportDialog(QVariantMap data, const QStringList& datasets, QWidget* parent = nullptr);
signals:
    void applyRequested(QVariantMap arguments);
};
