#include "session.h"
#include <QDateTime>
#include <QThread>
#include <QTimer>
#include <memory>
#include <stdexcept>

namespace {
using Value = std::unique_ptr<MmsValue, decltype(&MmsValue_delete)>;
Value owned(MmsValue* value) {
    return Value(value, MmsValue_delete);
}
const QStringList primary = {"stVal",  "mag", "cVal",    "instMag", "setMag",
                             "setVal", "val", "general", "actVal"};
QStringList fieldsFor(MmsVariableSpecification* spec, QString prefix = {}, int depth = 0) {
    QStringList fields;
    if (!spec || depth > 8 || MmsVariableSpecification_getType(spec) != MMS_STRUCTURE)
        return fields;
    for (int i = 0; i < MmsVariableSpecification_getSize(spec); ++i) {
        auto child = MmsVariableSpecification_getChildSpecificationByIndex(spec, i);
        QString name = QString::fromUtf8(MmsVariableSpecification_getName(child));
        QString path = prefix + name;
        if (primary.contains(name) || name == "q" || name == "t")
            fields << path;
        else
            fields << fieldsFor(child, path + '.', depth + 1);
    }
    return fields;
}
void flatten(MmsVariableSpecification* spec, MmsValue* value, const QString& path, const QString& fc,
             QVariantMap& out, int depth = 0) {
    if (!spec || !value || depth > 12)
        return;
    auto type = MmsValue_getType(value);
    if (type == MMS_STRUCTURE) {
        for (int i = 0; i < MmsValue_getArraySize(value); ++i) {
            auto child = MmsVariableSpecification_getChildSpecificationByIndex(spec, i);
            if (child)
                flatten(child, MmsValue_getElement(value, i),
                        path + '.' + QString::fromUtf8(MmsVariableSpecification_getName(child)), fc, out,
                        depth + 1);
        }
    } else if (type == MMS_ARRAY) {
        for (int i = 0; i < MmsValue_getArraySize(value); ++i)
            flatten(MmsVariableSpecification_getArrayElementSpecification(spec),
                    MmsValue_getElement(value, i), path + QString("(%1)").arg(i), fc, out, depth + 1);
    } else {
        QString decoded = Lite::text(value);
        if (type == MMS_FLOAT)
            decoded = QString::number(MmsValue_toDouble(value), 'g', 17);
        if (path.section('.', -1) == "q")
            decoded = Lite::quality(value);
        if (type == MMS_UTC_TIME)
            decoded = Lite::timestamp(value) + " [raw=" +
                      QByteArray(reinterpret_cast<const char*>(MmsValue_getUtcTimeBuffer(value)), 8).toHex() +
                      ']';
        out[path + '[' + fc + ']'] = decoded;
    }
}
QString historyKind(MmsVariableSpecification* spec, FunctionalConstraint fc) {
    for (const auto& field : fieldsFor(spec)) {
        auto child = MmsVariableSpecification_getNamedVariableRecursive(
            spec, QString(field).replace('.', '$').toLatin1().constData());
        if (field.section('.', -1) == "actVal" && child &&
            MmsVariableSpecification_getType(child) == MMS_INTEGER)
            return "counter";
    }
    return fc == IEC61850_FC_ST ? "digital" : fc == IEC61850_FC_MX ? "analog" : "";
}
QString refFor(const QString& ln, QString name) {
    name = Lite::normalized(name);
    if (name.contains('/'))
        return name;
    if (name.startsWith(ln.section('/', 1) + '.'))
        return ln.section('/', 0, 0) + '/' + name;
    return ln + '.' + name;
}
} // namespace
QVariantMap Session::discover() {
    IedClientError error;
    auto lds = Lite::strings(IedConnection_getLogicalDeviceList(connection_, &error));
    Lite::check(error, "读取逻辑设备");
    QVariantList nodes;
    QStringList datasets, reports;
    int deviceIndex = 0;
    for (const auto& ld : lds) {
        emit connectionProgress(20 + (deviceIndex++) * 50 / qMax(1, lds.size()), "读取逻辑设备 " + ld);
        if (QThread::currentThread()->isInterruptionRequested())
            break;
        auto lns = Lite::strings(
            IedConnection_getLogicalDeviceDirectory(connection_, &error, ld.toUtf8().constData()));
        if (error != IED_ERROR_OK) {
            if (IedConnection_getState(connection_) != IED_STATE_CONNECTED)
                Lite::check(error, ld);
            emit event(ld + " 目录读取失败，保留连接并继续其他设备：" + QString::number(error));
            continue;
        }
        int nodeIndex = 0;
        for (const auto& name : lns) {
            if (QThread::currentThread()->isInterruptionRequested())
                break;
            QString ln = ld + '/' + name;
            emit connectionProgress(20 + ((deviceIndex - 1) * 50 + (++nodeIndex) * 50 / qMax(1, lns.size())) /
                                             qMax(1, lds.size()),
                                    "读取节点目录 " + ln);
            nodes.append(QVariantMap{{"ld", ld}, {"ref", ln}, {"name", name}});
            for (auto name : Lite::strings(IedConnection_getLogicalNodeDirectory(
                     connection_, &error, ln.toUtf8().constData(), ACSI_CLASS_DATA_SET)))
                datasets << refFor(ln, name);
            if (error != IED_ERROR_OK)
                emit event(ln + " 数据集目录读取失败：" + QString::number(error));
            for (bool buffered : {false, true}) {
                auto list = Lite::strings(
                    IedConnection_getLogicalNodeDirectory(connection_, &error, ln.toUtf8().constData(),
                                                          buffered ? ACSI_CLASS_BRCB : ACSI_CLASS_URCB));
                for (auto name : list) {
                    name = Lite::normalized(name);
                    reports << (name.contains('/')
                                    ? name
                                    : ln +
                                          (name.startsWith("BR.") || name.startsWith("RP.") ? "."
                                           : buffered                                       ? ".BR."
                                                                                            : ".RP.") +
                                          name);
                }
                if (error != IED_ERROR_OK)
                    emit event(ln + " 报告目录读取失败：" + QString::number(error));
            }
        }
    }
    return {{"nodes", nodes}, {"datasets", datasets}, {"reports", reports}};
}
QVariantMap Session::objects(const QString& ln) {
    IedClientError error;
    auto names = Lite::strings(IedConnection_getLogicalNodeDirectory(
        connection_, &error, ln.toUtf8().constData(), ACSI_CLASS_DATA_OBJECT));
    Lite::check(error, ln);
    return {{"ref", ln}, {"names", names}};
}
MmsVariableSpecification* Session::specification(const QString& ref, FunctionalConstraint fc) {
    QString key = ref + '#' + QString::number(fc);
    if (!specs_.contains(key)) {
        IedClientError error;
        auto spec = IedConnection_getVariableSpecification(connection_, &error, ref.toUtf8().constData(), fc);
        specs_.insert(key, spec);
    }
    return specs_[key];
}
QString Session::description(const QString& ref) {
    if (!descriptions_.contains(ref)) {
        IedClientError error;
        auto value = owned(IedConnection_readObject(connection_, &error, (ref + ".dU").toUtf8().constData(),
                                                    IEC61850_FC_DC));
        descriptions_[ref] = error == IED_ERROR_OK ? Lite::text(value.get()) : "—";
    }
    return descriptions_[ref];
}
QVariantMap Session::point(const QString& ref, FunctionalConstraint fc, MmsVariableSpecification* spec,
                           MmsValue* value) {
    QVariantMap row{
        {"ref", ref}, {"name", description(ref)}, {"value", "—"}, {"quality", "—"}, {"time", "—"}};
    if (!spec || !value || !MmsVariableSpecification_isValueOfType(spec, value)) {
        row["value"] = value ? Lite::text(value) : "无可用值";
        return row;
    }
    QStringList values, qualities, times;
    for (const auto& name : fieldsFor(spec)) {
        auto child = MmsVariableSpecification_getChildValue(
            spec, value, QString(name).replace('.', '$').toLatin1().constData());
        if (!child)
            continue;
        QString leaf = name.section('.', -1);
        QString label = name.contains('.') ? name + '=' : QString();
        if (leaf == "q")
            qualities << label + Lite::quality(child);
        else if (leaf == "t")
            times << label + Lite::timestamp(child);
        else
            values << name + '=' + Lite::text(child);
    }
    row["value"] = values.isEmpty() ? "未识别主值属性" : values.join("; ");
    row["quality"] = qualities.isEmpty() ? "—" : qualities.join("; ");
    row["time"] = times.isEmpty() ? "—" : times.join("; ");
    row["kind"] = historyKind(spec, fc);
    row["sampleValid"] = !values.isEmpty();
    row["fc"] = QString::fromLatin1(FunctionalConstraint_toString(fc));
    return row;
}
QVariantMap Session::object(const QString& ref) {
    QVariantList rows;
    QVariantMap attributes;
    bool complete = true;
    for (auto fc : {IEC61850_FC_ST, IEC61850_FC_MX}) {
        auto spec = specification(ref, fc);
        if (!spec)
            continue;
        QVariantMap row{{"ref", ref}, {"name", description(ref)}, {"quality", "—"}, {"time", "—"}};
        QStringList values, qualities, times;
        auto cached = std::shared_ptr<MmsValue>(MmsValue_newDefaultValue(spec), MmsValue_delete);
        bool fcComplete = true;
        for (const auto& field : fieldsFor(spec)) {
            IedClientError error;
            auto value = owned(
                IedConnection_readObject(connection_, &error, (ref + '.' + field).toUtf8().constData(), fc));
            if (error == IED_ERROR_OK) {
                auto fieldSpec = MmsVariableSpecification_getNamedVariableRecursive(
                    spec, QString(field).replace('.', '$').toLatin1().constData());
                if (fieldSpec && value && MmsVariableSpecification_isValueOfType(fieldSpec, value.get())) {
                    auto target = MmsVariableSpecification_getChildValue(
                        spec, cached.get(), QString(field).replace('.', '$').toLatin1().constData());
                    if (!target || !MmsValue_update(target, value.get()))
                        fcComplete = false;
                    flatten(fieldSpec, value.get(), ref + '.' + field,
                            QString::fromLatin1(FunctionalConstraint_toString(fc)), attributes);
                } else
                    fcComplete = false;
            } else
                fcComplete = false;
            complete &= fcComplete;
            QString decoded =
                error == IED_ERROR_OK ? Lite::text(value.get()) : QString("读取失败 (%1)").arg(error);
            QString leaf = field.section('.', -1);
            QString label = field.contains('.') ? field + '=' : QString();
            if (leaf == "q")
                qualities << label + (error == IED_ERROR_OK ? Lite::quality(value.get()) : decoded);
            else if (leaf == "t")
                times << label + (error == IED_ERROR_OK ? Lite::timestamp(value.get()) : decoded);
            else
                values << field + '=' + decoded;
        }
        row["quality"] = qualities.isEmpty() ? "—" : qualities.join("; ");
        row["time"] = times.isEmpty() ? "—" : times.join("; ");
        row["value"] = values.isEmpty() ? "未识别主值属性" : values.join("; ");
        row["kind"] = historyKind(spec, fc);
        row["sampleValid"] = complete && !values.isEmpty();
        row["fc"] = QString::fromLatin1(FunctionalConstraint_toString(fc));
        rows << row;
        if (fcComplete && !values.isEmpty() && objectValues_.size() < 32768)
            objectValues_[ref + '#' + QString::number(fc)] = cached;
    }
    if (rows.isEmpty())
        throw std::runtime_error("该对象没有可读取的 ST/MX 主值；未读取其完整数据属性");
    attributes[ref + ".dU[DC]"] = description(ref);
    return {{"ref", ref}, {"rows", rows}, {"attributes", attributes}, {"complete", complete}};
}
QVariantMap Session::dataSet(const QString& ref) {
    for (const auto& binding : reportBindings_) {
        if (binding->enabled && binding->received && binding->dataset == ref)
            return {{"ref", ref}, {"rows", cachedDataset(ref)}, {"source", "报告缓存"}};
    }
    IedClientError error;
    QByteArray mms = Lite::mmsDataSet(ref).toUtf8();
    if (!members_.contains(ref)) {
        auto list =
            Lite::strings(IedConnection_getDataSetDirectory(connection_, &error, mms.constData(), nullptr));
        Lite::check(error, "读取数据集成员");
        members_[ref] = list;
    }
    auto ds = IedConnection_readDataSetValues(connection_, &error, mms.constData(), nullptr);
    std::unique_ptr<sClientDataSet, decltype(&ClientDataSet_destroy)> owner(ds, ClientDataSet_destroy);
    Lite::check(error, "读取数据集");
    auto values = ClientDataSet_getValues(ds);
    const auto& members = members_[ref];
    if (!values || MmsValue_getArraySize(values) != members.size()) {
        members_.remove(ref);
        throw std::runtime_error("数据集成员数量已变化，请重新读取");
    }
    QVariantList rows;
    QSet<QString> seen;
    for (int i = 0; i < members.size(); ++i) {
        if (QThread::currentThread()->isInterruptionRequested())
            break;
        QString member = Lite::normalized(members[i].section('[', 0, 0));
        QString obj = Lite::objectRef(members[i]);
        if (seen.contains(obj))
            continue;
        seen.insert(obj);
        auto fc = Lite::memberFc(members[i]);
        if (member == obj)
            rows << point(obj, fc, specification(obj, fc), MmsValue_getElement(values, i));
        else {
            // FCDA datasets may omit q/t. Read only the four requested fields,
            // once per DO, instead of pretending another member is its quality.
            try {
                auto result = object(obj)["rows"].toList();
                if (!result.isEmpty())
                    rows << result.first();
            } catch (const std::exception& error) {
                rows << QVariantMap{{"ref", obj},
                                    {"name", description(obj)},
                                    {"value", QString::fromUtf8(error.what())},
                                    {"quality", "—"},
                                    {"time", "—"}};
            }
        }
    }
    return {{"ref", ref}, {"rows", rows}, {"memberCount", members.size()}};
}

QVariantMap Session::cachedObject(const QString& ref, FunctionalConstraint fc) {
    auto key = ref + '#' + QString::number(fc);
    auto value = objectValues_.value(key);
    auto spec = specs_.value(key);
    if (!value || !spec)
        return {};
    QVariantMap attributes;
    for (const auto& field : fieldsFor(spec)) {
        auto fieldSpec = MmsVariableSpecification_getNamedVariableRecursive(
            spec, QString(field).replace('.', '$').toLatin1().constData());
        auto child = MmsVariableSpecification_getChildValue(
            spec, value.get(), QString(field).replace('.', '$').toLatin1().constData());
        if (fieldSpec && child)
            flatten(fieldSpec, child, ref + '.' + field,
                    QString::fromLatin1(FunctionalConstraint_toString(fc)), attributes);
    }
    attributes[ref + ".dU[DC]"] = descriptions_.value(ref, "—");
    auto row = point(ref, fc, spec, value.get());
    return {{"ref", ref},
            {"rows", QVariantList{row}},
            {"attributes", attributes},
            {"complete", true},
            {"receivedAt", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")}};
}
