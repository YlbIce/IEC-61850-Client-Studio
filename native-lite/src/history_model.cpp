#include "history_model.h"
#include <QColor>
#include <QDateTime>

namespace {
const QStringList fields{"device",  "sequence", "ref",      "name",   "value",
                         "quality", "time",     "received", "source", "before"};
const QStringList headers{"装置", "序号",     "引用名",           "描述", "值 / 链路事件",
                          "品质", "设备时标", "接收时间（本机）", "来源", "旧值 / 详情"};
} // namespace
HistoryModel::HistoryModel(QObject* parent) : QAbstractTableModel(parent) {}
int HistoryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : records_.size();
}
int HistoryModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : fields.size();
}
QVariantMap HistoryModel::record(int row) const {
    return row >= 0 && row < records_.size() ? records_[row] : QVariantMap{};
}
QVariant HistoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= records_.size() || index.column() >= fields.size())
        return {};
    const auto& row = records_[index.row()];
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
        return row.value(fields[index.column()]);
    if (role == Qt::ForegroundRole && row.value("transition").toBool())
        return QColor("#a04416");
    return {};
}
QVariant HistoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole)
        return {};
    return orientation == Qt::Horizontal ? QVariant(headers.value(section)) : QVariant(section + 1);
}
void HistoryModel::append(QVector<QVariantMap> records) {
    if (records.isEmpty())
        return;
    if (records.size() > 5000)
        records = records.mid(records.size() - 5000);
    int remove = qMax(0, records_.size() + records.size() - 5000);
    if (remove) {
        beginRemoveRows({}, 0, remove - 1);
        records_.remove(0, remove);
        endRemoveRows();
    }
    beginInsertRows({}, records_.size(), records_.size() + records.size() - 1);
    for (auto& row : records) {
        row["sequence"] = ++sequence_;
        records_ << row;
    }
    endInsertRows();
}
void HistoryModel::samples(QString device, const QVariantList& rows, QString source) {
    QVector<QVariantMap> added;
    const auto received = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    for (const auto& entry : rows) {
        auto row = entry.toMap();
        if (!row.value("sampleValid").toBool() || row.value("kind").toString().isEmpty())
            continue;
        QString key = device + QChar(0) + row.value("ref").toString() + QChar(0) + row.value("fc").toString();
        const auto previous = baseline_.value(key);
        bool identical = !previous.isEmpty();
        for (const auto& field : {"value", "quality", "time", "name"})
            identical &= previous.value(field) == row.value(field);
        if (identical)
            continue;
        // Bound baseline memory too; evicted entries restart as initial observations, never fabricated
        // transitions.
        if (!baseline_.contains(key) && baseline_.size() >= 20000)
            baseline_.erase(baseline_.begin());
        baseline_[key] = row;
        row["transition"] = row.value("kind") == "digital" && !previous.isEmpty() &&
                            previous.value("value") != row.value("value");
        row["before"] = previous.value("value", "首次采样");
        row["device"] = device;
        row["received"] = received;
        row["source"] = row.value("sampleSource", source);
        added << row;
    }
    append(added);
}
void HistoryModel::link(QString device, QString event, QString detail) {
    append({{{"device", device},
             {"kind", "link"},
             {"value", event},
             {"before", detail},
             {"received", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")},
             {"source", "客户端链路事件"}}});
}
void HistoryModel::resetBaseline() {
    baseline_.clear();
}
void HistoryModel::clear() {
    beginResetModel();
    records_.clear();
    endResetModel();
}
HistoryFilter::HistoryFilter(HistoryModel* source, QObject* parent) : QSortFilterProxyModel(parent) {
    setSourceModel(source);
}
void HistoryFilter::configure(int page, QString device, QString reference, QString search) {
    page_ = page;
    device_ = device;
    reference_ = reference;
    search_ = search.trimmed();
    invalidateFilter();
}
bool HistoryFilter::filterAcceptsRow(int row, const QModelIndex&) const {
    if (page_ == 5)
        return false;
    auto record = static_cast<HistoryModel*>(sourceModel())->record(row);
    auto kind = record.value("kind").toString();
    if (page_ == 0 && !record.value("transition").toBool())
        return false;
    if (page_ == 4 && kind != "link")
        return false;
    if (page_ >= 1 && page_ <= 3) {
        const QStringList kinds{"", "analog", "digital", "counter"};
        if (reference_.isEmpty() || record.value("device").toString() != device_ ||
            record.value("ref").toString() != reference_ || kind != kinds[page_])
            return false;
    }
    if (search_.isEmpty())
        return true;
    for (const auto& value : record)
        if (value.toString().contains(search_, Qt::CaseInsensitive))
            return true;
    return false;
}
