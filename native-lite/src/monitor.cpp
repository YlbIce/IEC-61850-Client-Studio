#include "session.h"
#include <QDateTime>
#include <QThread>
#include <stdexcept>

QVariantMap Session::watch(const QString& operation, const QString& ref) {
    if (operation == "watchAdd" && !watches_.contains(ref)) {
        if (watches_.size() >= 64)
            throw std::runtime_error("最多同时监视 64 个对象，请先移除不需要的监视");
        descriptions_.remove(ref);
        auto baseline = object(ref);
        if (!baseline["complete"].toBool())
            throw std::runtime_error("监视对象的主值 / 质量 / 时标读取不完整，未建立基线");
        if (baseline["attributes"].toMap().size() > 256)
            throw std::runtime_error("该对象超过 256 个监视属性，请选择范围更小的数据对象");
        watches_[ref] = baseline["attributes"].toMap();
        watchOrder_ << ref;
        emit monitoringChanged(watchOrder_);
        baseline["receivedAt"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        emit monitorSnapshot(ref, baseline, {});
        emit event("加入监视：" + ref + "（已建立基线；轮询主值、q、t、dU，每秒最多 4 个对象）");
        if (!monitorTimer_) {
            monitorTimer_ = new QTimer(this);
            monitorTimer_->setInterval(1000);
            connect(monitorTimer_, &QTimer::timeout, this, &Session::pollMonitors);
        }
        monitorTimer_->start();
    } else if (operation == "watchRemove") {
        watches_.remove(ref);
        watchErrors_.remove(ref);
        watchOrder_.removeAll(ref);
        emit event("移除监视：" + ref);
    } else if (operation == "watchClear") {
        watches_.clear();
        watchErrors_.clear();
        watchOrder_.clear();
        emit event("已清空监视对象");
    }
    if (watches_.isEmpty() && monitorTimer_)
        monitorTimer_->stop();
    emit monitoringChanged(watchOrder_);
    return {{"references", watchOrder_}};
}
void Session::pollMonitors() {
    if (!connection_ || IedConnection_getState(connection_) != IED_STATE_CONNECTED) {
        notifyConnectionLost();
        return;
    }
    int count = qMin(4, watchOrder_.size());
    for (int i = 0; i < count; ++i) {
        if (QThread::currentThread()->isInterruptionRequested())
            return;
        if (watchOrder_.isEmpty())
            return;
        watchCursor_ %= watchOrder_.size();
        const QString ref = watchOrder_[watchCursor_++];
        if (reportCovers(ref))
            continue;
        try {
            descriptions_.remove(ref); // dU is monitored as well, not frozen at its initial description.
            auto snapshot = object(ref);
            if (!snapshot["complete"].toBool())
                throw std::runtime_error("属性读取不完整，本轮不与基线比较");
            auto next = snapshot["attributes"].toMap();
            const auto previous = watches_.value(ref);
            if (watchErrors_.remove(ref))
                emit event("监视恢复：" + ref + "；中断期间可能存在未捕获的变化");
            QString time = snapshot["rows"].toList().value(0).toMap()["time"].toString();
            for (auto it = next.cbegin(); it != next.cend(); ++it) {
                if (previous.contains(it.key()) && previous[it.key()] != it.value())
                    emit attributeChanged(ref, it.key(), previous[it.key()].toString(), it.value().toString(),
                                          time);
            }
            watches_[ref] = next;
            snapshot["receivedAt"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
            emit monitorSnapshot(ref, snapshot, {});
        } catch (const std::exception& error) {
            QString message = QString::fromUtf8(error.what());
            if (watchErrors_.value(ref) != message)
                emit event("监视读取失败：" + ref + "，" + message);
            watchErrors_[ref] = message;
            emit monitorSnapshot(ref, {}, message);
        }
    }
}
