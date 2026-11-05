#include "session.h"
#include "connection_options.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <memory>
#include <stdexcept>

Session::Session(QObject* parent) : QObject(parent) {}
Session::~Session() {
    shutdown();
}
void Session::shutdown() {
    if (monitorTimer_)
        monitorTimer_->stop();
    lostNotified_ = false;
    watches_.clear();
    watchErrors_.clear();
    watchOrder_.clear();
    watchCursor_ = 0;
    emit monitoringChanged({});
    ++generation_;
    if (connection_) {
        // Connection destruction joins the receiver and destroys its controls.
        IedConnection_destroy(connection_);
        connection_ = nullptr;
    }
    for (auto spec : specs_)
        if (spec)
            MmsVariableSpecification_destroy(spec);
    specs_.clear();
    descriptions_.clear();
    members_.clear();
    controls_.clear();
    installedReports_.clear();
    connectionContext_.reset();
    reportBindings_.clear();
    objectValues_.clear();
    reportRows_.clear();
    reportGap_ = false;
    pendingReportChanges_.clear();
    pendingReportDatasets_.clear();
    reportUiScheduled_ = false;
}
void Session::execute(QString operation, QVariantMap args) {
    bool connecting = false;
    try {
        QVariantMap output;
        if (operation == "disconnect")
            shutdown();
        else if (operation == "connect") {
            emit connectionProgress(0, "校验连接参数");
            ConnectionOptions::validate(args);
            shutdown();
            connecting = true;
            connectionEndpoint_ = args["host"].toString().trimmed().toLower() + ':' + args["port"].toString();
            connection_ = IedConnection_create();
            ConnectionOptions::apply(connection_, args);
            emit event("开始建立 MMS 关联：" + ConnectionOptions::summary(args));
            QElapsedTimer elapsed;
            elapsed.start();
            IedClientError error;
            emit connectionProgress(5, "建立 TCP / COTP / ACSE / MMS 关联");
            IedConnection_connect(connection_, &error, args["host"].toString().toUtf8().constData(),
                                  args["port"].toInt());
            if (error != IED_ERROR_OK) {
                shutdown();
                const QString detail = error == IED_ERROR_CONNECTION_REJECTED
                                           ? "连接未建立（IED_ERROR_CONNECTION_REJECTED，错误 "
                                             "5）。协议库会将多种连接失败汇总为此代码；请核对端口、并发连接数"
                                             "及 ISO/ACSE 地址，必要时对比握手报文。"
                                           : QString("连接失败：%1（错误 %2）")
                                                 .arg(IedClientError_toString(error))
                                                 .arg(int(error));
                throw std::runtime_error(
                    (detail + QString(" 耗时 %1 ms").arg(elapsed.elapsed())).toStdString());
            }
            connectionContext_ =
                std::make_unique<ConnectionContext>(ConnectionContext{this, generation_.load()});
            IedConnection_installStateChangedHandler(
                connection_,
                [](void* context, IedConnection source, IedConnectionState state) {
                    if (state != IED_STATE_CLOSED)
                        return;
                    auto identity = static_cast<ConnectionContext*>(context);
                    auto self = identity->session;
                    const auto generation = identity->generation;
                    QMetaObject::invokeMethod(
                        self,
                        [self, generation, source] {
                            if (generation == self->generation_.load() && self->connection_ == source &&
                                self->connection_ &&
                                IedConnection_getState(self->connection_) == IED_STATE_CLOSED)
                                self->notifyConnectionLost();
                        },
                        Qt::QueuedConnection);
                },
                connectionContext_.get());
            emit event(QString("MMS 关联已建立（%1 ms），开始读取在线模型").arg(elapsed.elapsed()));
            emit connectionProgress(20, "读取逻辑设备目录");
            try {
                output = discover();
            } catch (const std::exception& error) {
                if (IedConnection_getState(connection_) != IED_STATE_CONNECTED)
                    throw;
                emit event("关联仍正常，在线模型未完整读取，可点击刷新模型重试：" +
                           QString::fromUtf8(error.what()));
                output = {{"nodes", QVariantList{}},
                          {"datasets", QStringList{}},
                          {"reports", QStringList{}},
                          {"partial", true}};
            }
            attachEnabledReports(output["reports"].toStringList());
            if (IedConnection_getState(connection_) != IED_STATE_CONNECTED)
                throw std::runtime_error("初始化期间连接已断开");
            emit connectionProgress(100, "连接就绪");
        } else {
            if (!connection_ || IedConnection_getState(connection_) != IED_STATE_CONNECTED)
                throw std::runtime_error("连接已断开，请重新连接 IED");
            if (operation == "discover") {
                // Active report decoding retains the connection's immutable type metadata.
                output = discover();
                attachEnabledReports(output["reports"].toStringList());
                emit connectionProgress(100, "模型刷新完成");
            } else if (operation == "objects")
                output = objects(args["ref"].toString());
            else if (operation == "dataset")
                output = dataSet(args["ref"].toString());
            else if (operation == "object")
                output = object(args["ref"].toString());
            else if (operation == "report" || operation == "saveReport")
                output = report(args, operation == "saveReport");
            else if (operation == "control")
                output = control(args);
            else if (operation.startsWith("watch"))
                output = watch(operation, args["ref"].toString());
            else
                throw std::runtime_error("未知命令");
        }
        emit result(operation, output);
    } catch (const std::exception& e) {
        if (connecting && connection_) {
            emit event("连接流程未完成，关闭本次会话；若日志已显示关联建立，请检查后续模型读取错误。");
            shutdown();
        }
        emit failed(operation, QString::fromUtf8(e.what()));
    }
}

void Session::notifyConnectionLost() {
    if (lostNotified_)
        return;
    lostNotified_ = true;
    if (monitorTimer_)
        monitorTimer_->stop();
    for (const auto& ref : watchOrder_)
        emit monitorSnapshot(ref, {}, "连接已断开");
    emit event("链路断开：MMS 关联已关闭，监视已暂停");
    emit connectionLost();
}
