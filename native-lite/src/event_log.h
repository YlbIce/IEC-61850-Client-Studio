#pragma once
#include <QVariantMap>
#include <QWidget>
class QPlainTextEdit;
class QComboBox;
class QLineEdit;
class QCheckBox;
class EventLog : public QWidget {
    Q_OBJECT
public:
    explicit EventLog(QWidget* parent = nullptr);
    void append(QString category, QString message);
    void change(QString reference, QString attribute, QString before, QString after, QString deviceTime);
    QString visibleText() const;
    int entryCount() const {
        return entries_.size();
    }
    bool exportCsv(const QString& path);

private:
    QPlainTextEdit* text_;
    QComboBox* category_;
    QLineEdit* search_;
    QCheckBox* follow_;
    QVector<QVariantMap> entries_;
    void insert(QVariantMap entry);
    bool matches(const QVariantMap&) const;
    QString format(const QVariantMap&) const;
    void rebuild();
};
