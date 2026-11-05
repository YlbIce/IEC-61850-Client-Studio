#pragma once
#include <QAbstractTableModel>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QVariantMap>

class HistoryModel : public QAbstractTableModel {
public:
    explicit HistoryModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void samples(QString device, const QVariantList& rows, QString source);
    void link(QString device, QString event, QString detail);
    void resetBaseline();
    void clear();
    QVariantMap record(int row) const;

private:
    void append(QVector<QVariantMap> records);
    QVector<QVariantMap> records_;
    QHash<QString, QVariantMap> baseline_;
    quint64 sequence_ = 0;
};
class HistoryFilter : public QSortFilterProxyModel {
public:
    HistoryFilter(HistoryModel* source, QObject* parent = nullptr);
    void configure(int page, QString device, QString reference, QString search);

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
    int page_ = 0;
    QString device_, reference_, search_;
};
