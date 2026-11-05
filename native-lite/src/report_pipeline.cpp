#include "session.h"
#include <QDateTime>
#include <QMetaObject>
#include <QThread>
#include <stdexcept>

void Session::prepareReport(ClientReportControlBlock block, const QString& ref) {
    if (!reportBindings_.contains(ref) && reportBindings_.size() >= 64)
        throw std::runtime_error("后台报告上限 64 个");
    auto binding = std::make_shared<ReportBinding>();
    binding->session = this;
    binding->generation = generation_.load();
    binding->reference = ref;
    binding->dataset =
        Lite::normalized(QString::fromUtf8(ClientReportControlBlock_getDataSetReference(block)));
    binding->confRev = ClientReportControlBlock_getConfRev(block);
    binding->ticket = ++nextReportTicket_;
    auto existing = reportBindings_.value(ref);
    if (existing && existing->enabled && existing->confRev == binding->confRev &&
        existing->dataset == binding->dataset)
        return;
    IedClientError error;
    binding->members = Lite::strings(IedConnection_getDataSetDirectory(
        connection_, &error, Lite::mmsDataSet(binding->dataset).toUtf8().constData(), nullptr));
    Lite::check(error, "准备报告成员映射");
    if (binding->members.isEmpty() || binding->members.size() > 4096)
        throw std::runtime_error("报告成员数须为 1–4096");
    QSet<QString> prepared;
    for (const auto& member : binding->members) {
        if (QThread::currentThread()->isInterruptionRequested())
            throw std::runtime_error("报告初始化已取消");
        auto obj = Lite::objectRef(member);
        auto fc = Lite::memberFc(member);
        if (fc != IEC61850_FC_ST && fc != IEC61850_FC_MX)
            throw std::runtime_error("该报告包含非 ST/MX 成员，保留按需读取");
        if (!prepared.contains(obj)) {
            emit connectionProgress(
                -1, QString("建立报告基线：已准备 %1 个对象，当前 %2").arg(prepared.size()).arg(obj));
            auto baseline = object(obj);
            if (!baseline["complete"].toBool())
                throw std::runtime_error("报告基线读取不完整，未停止轮询");
            prepared.insert(obj);
        }
        auto snapshot = cachedObject(obj, fc);
        if (snapshot.isEmpty())
            throw std::runtime_error("报告成员缺少可识别主值");
        reportRows_[obj + '#' + QString::number(fc)] = snapshot["rows"].toList().first().toMap();
    }
    // Installation replaces the callback under the library report lock; keep the old context alive until it
    // returns.
    IedConnection_installReportHandler(connection_, ref.toUtf8().constData(),
                                       ClientReportControlBlock_getRptId(block), onReport, binding.get());
    reportBindings_[ref] = binding;
    installedReports_.insert(ref);
    binding->enabled = ClientReportControlBlock_getRptEna(block);
    members_[binding->dataset] = binding->members;
    auto rows = cachedDataset(binding->dataset);
    QVariantList baseline;
    for (const auto& value : rows) {
        auto row = value.toMap();
        row["sampleSource"] = "报告基线读取";
        baseline << row;
    }
    emit reportData(connectionEndpoint_, binding->dataset, baseline, rows);
    queueReportUi(binding->dataset, baseline);
    emit event("后台报告已接入：" + ref + " → " + binding->dataset);
}
bool Session::reportCovers(const QString& ref) const {
    const auto attrs = watches_.value(ref);
    bool runtime = false;
    for (auto attr = attrs.cbegin(); attr != attrs.cend(); ++attr) {
        auto fc = attr.key().section('[', 1).section(']', 0, 0);
        if (fc != "ST" && fc != "MX")
            continue;
        runtime = true;
        bool covered = false;
        auto path = attr.key().section('[', 0, 0);
        for (const auto& binding : reportBindings_)
            if (binding->enabled && binding->received) {
                for (const auto& member : binding->members) {
                    auto memberFc =
                        QString::fromLatin1(FunctionalConstraint_toString(Lite::memberFc(member)));
                    auto memberPath = Lite::normalized(member.section('[', 0, 0));
                    if (memberFc == fc && (path == memberPath || path.startsWith(memberPath + '.')))
                        covered = true;
                }
            }
        if (!covered)
            return false;
    }
    return runtime;
}
QVariantList Session::cachedDataset(const QString& ref) const {
    QVariantList rows;
    QSet<QString> seen;
    for (const auto& member : members_.value(ref)) {
        auto key = Lite::objectRef(member) + '#' + QString::number(Lite::memberFc(member));
        if (!seen.contains(key) && reportRows_.contains(key)) {
            rows << reportRows_[key];
            seen.insert(key);
        }
    }
    return rows;
}
void Session::onReport(void* context, ClientReport report) {
    auto binding = static_cast<ReportBinding*>(context);
    auto self = binding->session;
    // Never issue MMS requests in the receiver callback. Clone borrowed values before returning.
    if (self->queuedReports_.fetch_add(1) >= 128) {
        --self->queuedReports_;
        self->reportGap_ = true;
        return;
    }
    auto packet = std::make_shared<ReportPacket>();
    packet->reference = binding->reference;
    packet->dataset = binding->dataset;
    packet->generation = binding->generation;
    packet->ticket = binding->ticket;
    packet->sequence = ClientReport_getSeqNum(report);
    packet->hasConfRev = ClientReport_hasConfRev(report);
    packet->confRev = ClientReport_getConfRev(report);
    packet->overflow = ClientReport_hasBufOvfl(report) && ClientReport_getBufOvfl(report);
    auto values = ClientReport_getDataSetValues(report);
    bool valid = values && MmsValue_getArraySize(values) == binding->members.size();
    if (ClientReport_hasDataSetName(report)) {
        auto name = ClientReport_getDataSetName(report);
        valid &= name && Lite::normalized(QString::fromUtf8(name)) == binding->dataset;
    }
    int bytes = 0;
    if (valid)
        for (int i = 0; i < binding->members.size(); ++i) {
            if (ClientReport_getReasonForInclusion(report, i) == IEC61850_REASON_NOT_INCLUDED)
                continue;
            auto value = MmsValue_getElement(values, i);
            if (!value || (bytes += MmsValue_getMaxEncodedSize(value)) > 65536) {
                valid = false;
                break;
            }
            packet->indexes << i;
            packet->values << std::shared_ptr<MmsValue>(MmsValue_clone(value), MmsValue_delete);
        }
    QMetaObject::invokeMethod(
        self,
        [self, packet, valid] {
            --self->queuedReports_;
            if (packet->generation != self->generation_.load())
                return;
            if (!valid || self->reportGap_.exchange(false)) {
                for (auto& binding : self->reportBindings_)
                    binding->enabled = false;
                emit self->event(
                    "报告采集出现缺口或映射长度异常，已恢复按需轮询；请重新应用报告参数建立基线");
                return;
            }
            self->processReport(*packet);
        },
        Qt::QueuedConnection);
}
void Session::processReport(const ReportPacket& packet) {
    auto binding = reportBindings_.value(packet.reference);
    if (!binding || !binding->enabled || binding->dataset != packet.dataset ||
        binding->ticket != packet.ticket)
        return;
    if ((packet.hasConfRev && packet.confRev != binding->confRev) || packet.overflow) {
        binding->enabled = false;
        emit event("报告配置变化或缓冲溢出：" + packet.reference +
                   "；已恢复按需轮询，请重新读取并应用报告参数");
        return;
    }
    QHash<QString, std::shared_ptr<MmsValue>> staged;
    QHash<QString, QPair<QString, FunctionalConstraint>> changed;
    for (int n = 0; n < packet.indexes.size(); ++n) {
        auto member = binding->members.value(packet.indexes[n]);
        auto obj = Lite::objectRef(member);
        auto fc = Lite::memberFc(member);
        auto key = obj + '#' + QString::number(fc);
        auto spec = specs_.value(key);
        auto previous = objectValues_.value(key);
        if (!spec || !previous) {
            binding->enabled = false;
            emit event("报告缓存失效，请重新应用参数");
            return;
        }
        if (!staged.contains(key))
            staged[key] = std::shared_ptr<MmsValue>(MmsValue_clone(previous.get()), MmsValue_delete);
        auto path = Lite::normalized(member.section('[', 0, 0));
        auto relative = path.mid(obj.size());
        if (relative.startsWith('.'))
            relative.remove(0, 1);
        auto target = relative.isEmpty()
                          ? staged[key].get()
                          : MmsVariableSpecification_getChildValue(
                                spec, staged[key].get(), relative.replace('.', '$').toLatin1().constData());
        if (!target || !packet.values[n] || !MmsValue_update(target, packet.values[n].get())) {
            binding->enabled = false;
            emit event("报告成员类型不匹配：" + member + "；已恢复按需轮询");
            return;
        }
        changed[key] = {obj, fc};
    }
    binding->received = true;
    QVariantList rows;
    for (auto it = staged.cbegin(); it != staged.cend(); ++it)
        objectValues_[it.key()] = it.value();
    for (auto it = changed.cbegin(); it != changed.cend(); ++it) {
        const auto obj = it.value().first;
        auto snapshot = cachedObject(obj, it.value().second);
        auto row = snapshot["rows"].toList().first().toMap();
        row["reportSequence"] = packet.sequence;
        reportRows_[it.key()] = row;
        rows << row;
        if (watches_.contains(obj)) {
            auto attrs = snapshot["attributes"].toMap();
            auto& previous = watches_[obj];
            for (auto attr = attrs.cbegin(); attr != attrs.cend(); ++attr) {
                if (previous.contains(attr.key()) && previous[attr.key()] != attr.value())
                    emit attributeChanged(obj, attr.key(), previous[attr.key()].toString(),
                                          attr.value().toString(), row["time"].toString());
                previous[attr.key()] = attr.value();
            }
            snapshot["source"] = "MMS 报告";
            QVariantList monitorRows;
            QVariantMap monitorAttributes;
            for (auto fc : {IEC61850_FC_ST, IEC61850_FC_MX}) {
                const auto cached = cachedObject(obj, fc);
                monitorRows.append(cached.value("rows").toList());
                const auto cachedAttributes = cached.value("attributes").toMap();
                for (auto attr = cachedAttributes.cbegin(); attr != cachedAttributes.cend(); ++attr)
                    monitorAttributes.insert(attr.key(), attr.value());
            }
            snapshot["rows"] = monitorRows;
            snapshot["attributes"] = monitorAttributes;
            emit monitorSnapshot(obj, snapshot, {});
        }
    }
    if (!rows.isEmpty()) {
        emit reportData(connectionEndpoint_, binding->dataset, rows, cachedDataset(binding->dataset));
        queueReportUi(binding->dataset, rows);
    }
}
void Session::attachEnabledReports(const QStringList& reports) {
    int index = 0;
    for (const auto& ref : reports) {
        if (QThread::currentThread()->isInterruptionRequested())
            return;
        emit connectionProgress(75 + (++index) * 24 / qMax(1, reports.size()), "检查已启用报告 " + ref);
        try {
            report({{"ref", ref}}, false);
        } catch (const std::exception& e) {
            emit event("报告接入未完成：" + ref + "：" + QString::fromUtf8(e.what()));
        }
        if (!connection_ || IedConnection_getState(connection_) != IED_STATE_CONNECTED)
            break;
    }
}

void Session::queueReportUi(QString dataset, const QVariantList& changes) {
    if (pendingReportChanges_.size() + changes.size() > 8192) {
        for (auto& binding : reportBindings_)
            binding->enabled = false;
        emit event("报告界面缓冲超限，历史存在缺口；已恢复按需轮询，请重新应用报告参数");
        return;
    }
    pendingReportChanges_ += changes;
    pendingReportDatasets_.insert(dataset);
    if (reportUiScheduled_)
        return;
    reportUiScheduled_ = true;
    const auto generation = generation_.load();
    QTimer::singleShot(100, this, [this, generation] {
        if (generation != generation_.load())
            return;
        auto changes = pendingReportChanges_;
        QVariantMap datasets;
        for (const auto& dataset : pendingReportDatasets_)
            datasets[dataset] = cachedDataset(dataset);
        pendingReportChanges_.clear();
        pendingReportDatasets_.clear();
        reportUiScheduled_ = false;
        emit reportBatch(connectionEndpoint_, changes, datasets);
    });
}
