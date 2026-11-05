#include "session.h"
#include <QMetaObject>
#include <QTimer>
#include <memory>
#include <stdexcept>

void Session::onTermination(void* context, ControlObjectClient control) {
    auto self = static_cast<Session*>(context);
    const auto error = ControlObjectClient_getLastApplError(control);
    const QString ref = QString::fromUtf8(ControlObjectClient_getObjectReference(control));
    emit self->event(
        QString("遥控命令终结 %1：error=%2，addCause=%3").arg(ref).arg(error.error).arg(error.addCause));
}
QVariantMap Session::control(const QVariantMap& args) {
    const QString ref = args["ref"].toString();
    auto ctl = controls_.value(ref, nullptr);
    if (!ctl) {
        ctl = ControlObjectClient_create(ref.toUtf8().constData(), connection_);
        if (!ctl)
            throw std::runtime_error("无法创建遥控对象：对象不支持遥控或模型读取失败");
        controls_[ref] = ctl;
        ControlObjectClient_setOrigin(ctl, "IEC61850ClientLite", 3);
        ControlObjectClient_setCommandTerminationHandler(ctl, onTermination, this);
    }
    auto model = ControlObjectClient_getControlModel(ctl);
    auto type = ControlObjectClient_getCtlValType(ctl);
    QString action = args.value("action", "inspect").toString();
    if (action != "inspect") {
        ControlObjectClient_setTestMode(ctl, args["test"].toBool());
        ControlObjectClient_setInterlockCheck(ctl, args["interlock"].toBool());
        ControlObjectClient_setSynchroCheck(ctl, args["synchro"].toBool());
        MmsValue* raw = nullptr;
        bool valid = true;
        QString input = args["value"].toString().trimmed();
        if (type == MMS_BOOLEAN) {
            valid = input == "0" || input == "1" || input == "true" || input == "false";
            raw = MmsValue_newBoolean(input == "1" || input == "true");
        } else if (type == MMS_INTEGER)
            raw = MmsValue_newIntegerFromInt32(input.toInt(&valid));
        else if (type == MMS_UNSIGNED)
            raw = MmsValue_newUnsignedFromUint32(input.toUInt(&valid));
        else if (type == MMS_FLOAT)
            raw = MmsValue_newFloat(input.toFloat(&valid));
        else
            throw std::runtime_error("该 ctlVal 类型暂不支持，未下发命令");
        std::unique_ptr<MmsValue, decltype(&MmsValue_delete)> value(raw, MmsValue_delete);
        if (!valid)
            throw std::runtime_error("遥控值格式无效");
        bool ok = false;
        if (action == "select") {
            if (model == CONTROL_MODEL_SBO_NORMAL)
                ok = ControlObjectClient_select(ctl);
            else if (model == CONTROL_MODEL_SBO_ENHANCED)
                ok = ControlObjectClient_selectWithValue(ctl, raw);
            else
                throw std::runtime_error("该对象为直接控制，不支持选择");
        } else if (action == "operate")
            ok = ControlObjectClient_operate(ctl, raw, 0);
        else if (action == "cancel")
            ok = ControlObjectClient_cancel(ctl);
        else
            throw std::runtime_error("未知遥控动作");
        if (!ok) {
            auto error = ControlObjectClient_getLastApplError(ctl);
            throw std::runtime_error(QString("遥控失败：MMS=%1，error=%2，addCause=%3")
                                         .arg(ControlObjectClient_getLastError(ctl))
                                         .arg(error.error)
                                         .arg(error.addCause)
                                         .toStdString());
        }
        emit event(ref + ' ' + action + "：请求被接受" +
                   ((model == CONTROL_MODEL_DIRECT_ENHANCED || model == CONTROL_MODEL_SBO_ENHANCED) &&
                            action == "operate"
                        ? "，请查看命令终结事件"
                        : ""));
    }
    return {{"ref", ref}, {"model", int(model)}, {"type", int(type)}, {"action", action}};
}
