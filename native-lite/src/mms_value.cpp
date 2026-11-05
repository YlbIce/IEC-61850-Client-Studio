#include "session.h"
#include <QDateTime>
#include <stdexcept>
namespace Lite {
void check(IedClientError error, const QString& context) {
    if (error != IED_ERROR_OK)
        throw std::runtime_error((context + QString("：MMS 错误 %1").arg(int(error))).toStdString());
}
QStringList strings(LinkedList list) {
    QStringList output;
    if (!list)
        return output;
    for (auto p = LinkedList_getNext(list); p; p = LinkedList_getNext(p))
        output.append(QString::fromUtf8(static_cast<char*>(LinkedList_getData(p))));
    LinkedList_destroy(list);
    return output;
}
QString normalized(QString ref) {
    return ref.replace('$', '.');
}
QString mmsDataSet(QString ref) {
    int i = ref.lastIndexOf('.');
    if (i >= 0)
        ref[i] = '$';
    return ref;
}
QString objectRef(QString member) {
    member = normalized(member.section('[', 0, 0));
    int start = member.indexOf('.', member.indexOf('/') + 1);
    int end = member.indexOf('.', start + 1);
    return end < 0 ? member : member.left(end);
}
FunctionalConstraint memberFc(const QString& member) {
    QByteArray fc = member.section('[', 1).section(']', 0, 0).toLatin1();
    return FunctionalConstraint_fromString(fc.constData());
}
QString text(MmsValue* value) {
    if (!value)
        return "—";
    if (MmsValue_getType(value) == MMS_DATA_ACCESS_ERROR)
        return QString("读取失败 (%1)").arg(int(MmsValue_getDataAccessError(value)));
    if (MmsValue_getType(value) == MMS_VISIBLE_STRING || MmsValue_getType(value) == MMS_STRING)
        return QString::fromUtf8(MmsValue_toString(value));
    char buffer[2048] = {};
    MmsValue_printToBuffer(value, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}
QString quality(MmsValue* value) {
    if (!value || MmsValue_getType(value) != MMS_BIT_STRING)
        return text(value);
    auto q = Quality_fromMmsValue(value);
    QStringList flags;
    switch (Quality_getValidity(&q)) {
    case QUALITY_VALIDITY_GOOD:
        flags << "良好";
        break;
    case QUALITY_VALIDITY_INVALID:
        flags << "无效";
        break;
    case QUALITY_VALIDITY_QUESTIONABLE:
        flags << "可疑";
        break;
    default:
        flags << "保留";
    }
    if (q & QUALITY_TEST)
        flags << "测试";
    if (q & QUALITY_OPERATOR_BLOCKED)
        flags << "闭锁";
    if (q & QUALITY_DETAIL_OLD_DATA)
        flags << "旧数据";
    return flags.join(" / ") + QString(" [0x%1]").arg(q, 4, 16, QChar('0'));
}
QString timestamp(MmsValue* value) {
    if (!value || MmsValue_getType(value) != MMS_UTC_TIME)
        return text(value);
    return QDateTime::fromMSecsSinceEpoch(MmsValue_getUtcTimeInMs(value), Qt::UTC)
        .toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'");
}
} // namespace Lite
