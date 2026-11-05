#include "session.h"
#include <QMetaObject>
#include <QTimer>
#include <memory>
#include <stdexcept>

QVariantMap Session::report(const QVariantMap& args, bool write) {
    const QString ref = args["ref"].toString();
    IedClientError error;
    auto block = IedConnection_getRCBValues(connection_, &error, ref.toUtf8().constData(), nullptr);
    std::unique_ptr<sClientReportControlBlock, decltype(&ClientReportControlBlock_destroy)> owner(
        block, ClientReportControlBlock_destroy);
    Lite::check(error, "读取报告控制块");
    if (write && reportBindings_.contains(ref))
        reportBindings_[ref]->enabled = false;
    if (write) {
        // Parameters cannot generally be changed while RptEna is true.
        if (ClientReportControlBlock_getRptEna(block)) {
            ClientReportControlBlock_setRptEna(block, false);
            IedConnection_setRCBValues(connection_, &error, block, RCB_ELEMENT_RPT_ENA, true);
            Lite::check(error, "停用报告（参数未写入）");
        }
        ClientReportControlBlock_setRptId(block, args["rptId"].toString().toUtf8().constData());
        ClientReportControlBlock_setDataSetReference(
            block, Lite::mmsDataSet(args["dataset"].toString()).toUtf8().constData());
        ClientReportControlBlock_setBufTm(block, args["bufTm"].toUInt());
        ClientReportControlBlock_setIntgPd(block, args["intgPd"].toUInt());
        ClientReportControlBlock_setTrgOps(block, args["trgOps"].toUInt());
        ClientReportControlBlock_setOptFlds(block, args["optFlds"].toUInt());
        uint32_t mask = RCB_ELEMENT_RPT_ID | RCB_ELEMENT_DATSET | RCB_ELEMENT_BUF_TM | RCB_ELEMENT_INTG_PD |
                        RCB_ELEMENT_TRG_OPS | RCB_ELEMENT_OPT_FLDS;
        if (ClientReportControlBlock_isBuffered(block)) {
            if (ClientReportControlBlock_hasResvTms(block)) {
                ClientReportControlBlock_setResvTms(block, args["resvTms"].toInt());
                mask |= RCB_ELEMENT_RESV_TMS;
            }
        } else {
            ClientReportControlBlock_setResv(block, args["reserve"].toBool());
            mask |= RCB_ELEMENT_RESV;
        }
        IedConnection_setRCBValues(connection_, &error, block, mask, false);
        Lite::check(error, "写入参数失败，可能已部分写入且报告已停用；请重新读取");
        if (args["purge"].toBool() && ClientReportControlBlock_isBuffered(block)) {
            ClientReportControlBlock_setPurgeBuf(block, true);
            IedConnection_setRCBValues(connection_, &error, block, RCB_ELEMENT_PURGE_BUF, true);
            Lite::check(error, "清空报告缓冲");
        }
        if (args["enabled"].toBool()) {
            block = IedConnection_getRCBValues(connection_, &error, ref.toUtf8().constData(), owner.get());
            Lite::check(error, "准备报告前回读配置版本");
            prepareReport(block, ref);
        }
        ClientReportControlBlock_setRptEna(block, args["enabled"].toBool());
        IedConnection_setRCBValues(connection_, &error, block, RCB_ELEMENT_RPT_ENA, true);
        Lite::check(error, "设置报告使能");
        if (reportBindings_.contains(ref))
            reportBindings_[ref]->enabled = args["enabled"].toBool();
        if (args["gi"].toBool() && args["enabled"].toBool()) {
            ClientReportControlBlock_setGI(block, true);
            IedConnection_setRCBValues(connection_, &error, block, RCB_ELEMENT_GI, true);
            Lite::check(error, "触发 GI");
        }
        block = IedConnection_getRCBValues(connection_, &error, ref.toUtf8().constData(), owner.get());
        Lite::check(error, "参数写入后的回读");
    }
    if (!write && ClientReportControlBlock_getRptEna(block))
        prepareReport(block, ref);
    else if (!write && reportBindings_.contains(ref))
        reportBindings_[ref]->enabled = false;
    auto str = [](const char* value) { return value ? QString::fromUtf8(value) : QString(); };
    return {{"ref", ref},
            {"rptId", str(ClientReportControlBlock_getRptId(block))},
            {"dataset", Lite::normalized(str(ClientReportControlBlock_getDataSetReference(block)))},
            {"enabled", ClientReportControlBlock_getRptEna(block)},
            {"buffered", ClientReportControlBlock_isBuffered(block)},
            {"bufTm", ClientReportControlBlock_getBufTm(block)},
            {"intgPd", ClientReportControlBlock_getIntgPd(block)},
            {"trgOps", ClientReportControlBlock_getTrgOps(block)},
            {"optFlds", ClientReportControlBlock_getOptFlds(block)},
            {"confRev", ClientReportControlBlock_getConfRev(block)},
            {"seqNum", ClientReportControlBlock_getSqNum(block)},
            {"reserve", ClientReportControlBlock_getResv(block)},
            {"hasResvTms", ClientReportControlBlock_hasResvTms(block)},
            {"resvTms", ClientReportControlBlock_getResvTms(block)}};
}
