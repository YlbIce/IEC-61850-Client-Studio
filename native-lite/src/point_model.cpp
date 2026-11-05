#include "point_model.h"
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>

PointModel::PointModel(QObject* parent) : QAbstractTableModel(parent) {}
int PointModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_.size();
}
int PointModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 7;
}
QString PointModel::reference(int row) const {
    return rows_.value(row).toMap()["ref"].toString();
}
QString PointModel::key(int row) const {
    QByteArray identity = endpoint_.toUtf8() + '\0' + dataset_.toUtf8() + '\0' + reference(row).toUtf8();
    return "tested/" +
           QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}
QVariantMap PointModel::testRecord(int row) const {
    return records_.value(row, {{"status", "untested"}});
}
QString PointModel::statusText(const QString& status) {
    if (status == "success")
        return "已测成功";
    if (status == "failure")
        return "已测失败";
    if (status == "tested")
        return "已测待确认";
    return "未测";
}
QVariant PointModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size())
        return {};
    auto record = testRecord(index.row());
    const auto status = record["status"].toString();
    if (role == TestStatusRole)
        return status;
    if (role == Qt::BackgroundRole) {
        if (status == "success")
            return QColor("#e8f5ee");
        if (status == "failure")
            return QColor("#fdecec");
        if (status == "tested")
            return QColor("#fff5df");
        return QColor(index.row() % 2 ? "#f6f8fb" : "#ffffff");
    }
    if (index.column() >= 5) {
        if (role == Qt::DisplayRole)
            return index.column() == 5 ? statusText(status) : record["reason"].toString();
        if (role == Qt::ToolTipRole)
            return statusText(status) + "\n" + record["time"].toString() + "\n" + record["reason"].toString();
        if (role == Qt::ForegroundRole && index.column() == 5)
            return QColor(status == "success" ? "#236447" : status == "failure" ? "#a43333" : "#58697f");
        return {};
    }
    const QStringList keys = {"ref", "name", "value", "quality", "time"};
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
        return rows_[index.row()].toMap().value(keys[index.column()]);
    return {};
}
QVariant PointModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    return QStringList{"数据对象引用",  "数据名称（dU）", "数据值",         "质量（q）",
                       "设备时标（t）", "测试结果",       "失败原因 / 备注"}
        .value(section);
}
Qt::ItemFlags PointModel::flags(const QModelIndex& index) const {
    return QAbstractTableModel::flags(index);
}
bool PointModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    // Old binary checks never imply a successful test.
    if (index.isValid() && index.column() == 5 && role == Qt::CheckStateRole)
        return setTestResult(index.row(), value.toInt() == Qt::Checked ? "tested" : "untested");
    return false;
}
bool PointModel::setTestResult(int row, QString status, QString reason) {
    if (row < 0 || row >= rows_.size() || dataset_.isEmpty())
        return false;
    if (!QStringList{"untested", "success", "failure", "tested"}.contains(status))
        return false;
    reason = reason.trimmed();
    if (status == "failure" && reason.isEmpty()) {
        emit storageError("测试失败必须填写失败原因");
        return false;
    }
    if (reason.size() > 2000) {
        emit storageError("失败原因 / 备注最多 2000 字");
        return false;
    }
    QVariantMap record{{"status", status},
                       {"reason", reason},
                       {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                       {"version", 2}};
    if (status == "untested") {
        record = {{"status", "untested"}};
        settings_.remove(keys_[row]);
    } else
        settings_.setValue(keys_[row], record);
    settings_.sync();
    if (settings_.status() != QSettings::NoError) {
        emit storageError("测试记录写入本地配置失败，请检查当前用户权限");
        return false;
    }
    records_[row] = record;
    emit dataChanged(
        index(row, 0), index(row, 6),
        {Qt::DisplayRole, Qt::ToolTipRole, Qt::BackgroundRole, Qt::ForegroundRole, TestStatusRole});
    emit testResultsChanged();
    emit resultRecorded(reference(row), statusText(status), reason);
    return true;
}
void PointModel::markAll(bool tested) {
    if (dataset_.isEmpty() || rows_.isEmpty())
        return;
    auto record = QVariantMap{{"status", tested ? "success" : "untested"},
                              {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                              {"version", 2}};
    for (int row = 0; row < rows_.size(); ++row) {
        if (tested)
            settings_.setValue(keys_[row], record);
        else
            settings_.remove(keys_[row]);
    }
    settings_.sync();
    if (settings_.status() != QSettings::NoError) {
        emit storageError("批量测试记录写入失败，请检查当前用户权限");
        return;
    }
    records_.fill(record);
    emit dataChanged(
        index(0, 0), index(rows_.size() - 1, 6),
        {Qt::DisplayRole, Qt::BackgroundRole, Qt::ForegroundRole, Qt::ToolTipRole, TestStatusRole});
    emit testResultsChanged();
    emit resultRecorded(dataset_, tested ? "整数据集标记成功" : "整数据集清除测试结果",
                        QString::number(rows_.size()) + " 个对象");
}
void PointModel::update(QString endpoint, QString dataset, QVariantList rows) {
    bool same = endpoint == endpoint_ && dataset == dataset_ && rows.size() == rows_.size();
    if (same)
        for (int i = 0; i < rows.size(); ++i)
            if (rows[i].toMap()["ref"] != rows_[i].toMap()["ref"]) {
                same = false;
                break;
            }
    if (!same)
        beginResetModel();
    rows_ = rows;
    endpoint_ = endpoint;
    dataset_ = dataset;
    if (!same) {
        keys_.clear();
        records_.clear();
        keys_.reserve(rows_.size());
        records_.reserve(rows_.size());
        for (int row = 0; row < rows_.size(); ++row) {
            const QString identity = key(row);
            keys_ << identity;
            QVariant saved = dataset.isEmpty() ? QVariant() : settings_.value(identity);
            QVariantMap record = saved.toMap();
            if (record.isEmpty())
                record = {{"status", saved.isValid() ? "tested" : "untested"}, {"time", saved.toString()}};
            records_ << record;
        }
        endResetModel();
        emit testResultsChanged();
    } else if (!rows_.isEmpty())
        emit dataChanged(index(0, 0), index(rows_.size() - 1, 4), {Qt::DisplayRole});
}
TestFilterModel::TestFilterModel(QObject* parent) : QSortFilterProxyModel(parent) {
    setFilterRole(PointModel::TestStatusRole);
    setFilterKeyColumn(5);
    setDynamicSortFilter(true);
}
void TestFilterModel::setStatus(QString status) {
    status_ = status;
    invalidateFilter();
}
bool TestFilterModel::filterAcceptsRow(int row, const QModelIndex& parent) const {
    const QString status = sourceModel()->index(row, 5, parent).data(PointModel::TestStatusRole).toString();
    return status_ == "all" || status_ == status || (status_ == "done" && status != "untested");
}
