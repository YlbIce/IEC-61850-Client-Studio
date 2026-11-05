#include "connection_options.h"
#include <QRegularExpression>
#include <cstring>
#include <stdexcept>

namespace {
QByteArray selector(const QVariantMap& args, const QString& key, QString fallback, int maximum) {
    QString value = args.value(key, fallback).toString();
    value.remove(QRegularExpression("\\s"));
    if (value.size() % 2 || value.size() > maximum * 2 ||
        !QRegularExpression("^[0-9a-fA-F]*$").match(value).hasMatch())
        throw std::runtime_error((key + "：请输入完整十六进制字节，不能超出长度限制").toStdString());
    return QByteArray::fromHex(value.toLatin1());
}
QString apTitle(const QVariantMap& args, const QString& side) {
    QString oid =
        args.value(side + "ApTitle", side == "local" ? "1.1.1.999" : "1.1.1.999.1").toString().trimmed();
    oid.replace(QRegularExpression("\\s+"), ".");
    if (!QRegularExpression("^[0-2](\\.[0-9]+)+$").match(oid).hasMatch())
        throw std::runtime_error("AP-title 应为点或空格分隔的非负整数 OID");
    const auto arcs = oid.split('.');
    QList<uint> values;
    for (const auto& arc : arcs) {
        bool ok;
        uint value = arc.toUInt(&ok);
        if (!ok || value > 2147483647u)
            throw std::runtime_error("AP-title 数值超出协议库范围");
        values << value;
    }
    // This library encodes the first two arcs in a single byte; reject unsupported OIDs.
    if ((values[0] < 2 && values[1] > 39) || values[0] * 40u + values[1] > 127u)
        throw std::runtime_error("AP-title 前两段超出当前协议库支持范围");
    int bytes = 1;
    for (int i = 2; i < values.size(); ++i) {
        auto n = values[i];
        ++bytes;
        while (n > 127) {
            ++bytes;
            n >>= 7;
        }
    }
    if (bytes > 10)
        throw std::runtime_error("AP-title 编码不能超过当前协议库的 10 字节容量");
    bool ok;
    auto qualifier = args.value(side + "AeQualifier", 12).toString().toLongLong(&ok);
    if (!ok || qualifier < 0 || qualifier > 2147483647LL)
        throw std::runtime_error("AE-qualifier 必须是有效非负整数");
    return oid;
}
template <class T> T address(const QByteArray& bytes) {
    T result{};
    result.size = static_cast<uint8_t>(bytes.size());
    std::memcpy(result.value, bytes.constData(), bytes.size());
    return result;
}
} // namespace
void ConnectionOptions::validate(const QVariantMap& args) {
    if (args.value("host").toString().trimmed().isEmpty() || args.value("port").toInt() < 1 ||
        args.value("port").toInt() > 65535)
        throw std::runtime_error("请输入有效的主机地址和端口（1–65535）");
    for (auto key : {"connectTimeout", "requestTimeout"}) {
        int value = args.value(key, 10000).toInt();
        if (value < 1000 || value > 60000)
            throw std::runtime_error("超时范围为 1000–60000 毫秒");
    }
    auto mode = args.value("addressMode", "default").toString();
    if (mode != "default" && mode != "omitApTitle" && mode != "custom")
        throw std::runtime_error("未知连接兼容模式");
    for (const auto& side : {QString("local"), QString("remote")}) {
        if (mode == "custom")
            apTitle(args, side);
        selector(args, side + "T", "0001", 4);
        selector(args, side + "S", "0001", 16);
        selector(args, side + "P", "00000001", 16);
    }
}
void ConnectionOptions::apply(IedConnection connection, const QVariantMap& args) {
    validate(args);
    IedConnection_setConnectTimeout(connection, args.value("connectTimeout", 10000).toUInt());
    IedConnection_setRequestTimeout(connection, args.value("requestTimeout", 10000).toUInt());
    auto iso = MmsConnection_getIsoConnectionParameters(IedConnection_getMmsConnection(connection));
    if (args.value("addressMode").toString() == "omitApTitle") {
        IsoConnectionParameters_setLocalApTitle(iso, nullptr, 0);
        IsoConnectionParameters_setRemoteApTitle(iso, nullptr, 0);
    }
    if (args.value("addressMode").toString() == "custom") {
        IsoConnectionParameters_setLocalApTitle(iso, apTitle(args, "local").toUtf8().constData(),
                                                args.value("localAeQualifier", 12).toInt());
        IsoConnectionParameters_setRemoteApTitle(iso, apTitle(args, "remote").toUtf8().constData(),
                                                 args.value("remoteAeQualifier", 12).toInt());
    }
    IsoConnectionParameters_setLocalAddresses(iso,
                                              address<PSelector>(selector(args, "localP", "00000001", 16)),
                                              address<SSelector>(selector(args, "localS", "0001", 16)),
                                              address<TSelector>(selector(args, "localT", "0001", 4)));
    IsoConnectionParameters_setRemoteAddresses(iso,
                                               address<PSelector>(selector(args, "remoteP", "00000001", 16)),
                                               address<SSelector>(selector(args, "remoteS", "0001", 16)),
                                               address<TSelector>(selector(args, "remoteT", "0001", 4)));
}
QString ConnectionOptions::summary(const QVariantMap& args) {
    QStringList parts;
    parts << (args.value("addressMode").toString() == "omitApTitle"
                  ? "AP-title/AE-qualifier 不发送"
                  : "AP-title 本机=1.1.1.999/12，设备=1.1.1.999.1/12");
    if (args.value("addressMode").toString() == "custom") {
        parts.clear();
        for (const auto& side : {QString("local"), QString("remote")})
            parts << side + " AP-title=" + apTitle(args, side) + "/" +
                         args.value(side + "AeQualifier", 12).toString();
    }
    for (const auto& side : {QString("local"), QString("remote")})
        parts << side + " T/S/P=" + selector(args, side + "T", "0001", 4).toHex() + "/" +
                     selector(args, side + "S", "0001", 16).toHex() + "/" +
                     selector(args, side + "P", "00000001", 16).toHex();
    parts << QString("连接/请求超时=%1/%2 ms")
                 .arg(args.value("connectTimeout", 10000).toInt())
                 .arg(args.value("requestTimeout", 10000).toInt());
    return parts.join("；");
}
