#pragma once
#include <QAbstractTableModel>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QVariantMap>

class PointModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum { TestStatusRole = Qt::UserRole + 1 };
    explicit PointModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex&, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex&) const override;
    bool setData(const QModelIndex&, const QVariant&, int role) override;
    void update(QString endpoint, QString dataset, QVariantList rows);
    void markAll(bool tested);
    bool setTestResult(int row, QString status, QString reason = {});
    QVariantMap testRecord(int row) const;
    static QString statusText(const QString& status);
    QString reference(int row) const;
signals:
    void storageError(QString message);
    void testResultsChanged();
    void resultRecorded(QString reference, QString status, QString reason);

private:
    QString key(int row) const;
    QVariantList rows_;
    QString endpoint_, dataset_;
    QSettings settings_;
    QVector<QString> keys_;
    QVector<QVariantMap> records_;
};

class TestFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit TestFilterModel(QObject* parent = nullptr);
    void setStatus(QString status);

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
    QString status_ = "all";
};
