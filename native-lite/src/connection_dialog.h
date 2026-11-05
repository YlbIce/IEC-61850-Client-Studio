#pragma once
#include <QDialog>
#include <QVariantMap>
class QLineEdit;
class QSpinBox;
class QComboBox;
class ConnectionDialog : public QDialog {
public:
    explicit ConnectionDialog(QWidget* parent = nullptr);
    QVariantMap arguments() const;

private:
    QLineEdit* host_;
    QSpinBox* port_;
    QSpinBox* connectTimeout_;
    QSpinBox* requestTimeout_;
    QComboBox* mode_;
    QMap<QString, QLineEdit*> selectors_;
};
