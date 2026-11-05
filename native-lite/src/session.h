#pragma once
#include "report_pipeline.h"
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <atomic>
extern "C" {
#include "iec61850_client.h"
#include "mms_type_spec.h"
}

// All protocol operations run in the worker thread. Library callbacks only post
// notifications; they never access widgets or issue synchronous MMS requests.
class Session : public QObject {
    Q_OBJECT
public:
    explicit Session(QObject* parent = nullptr);
    ~Session() override;
public slots:
    void execute(QString operation, QVariantMap args = {});
    void shutdown();
signals:
    void result(QString operation, QVariantMap data);
    void failed(QString operation, QString message);
    void event(QString message);
    void reportData(QString device, QString dataset, QVariantList changes, QVariantList rows);
    void connectionProgress(int percent, QString phase);
    void reportBatch(QString device, QVariantList changes, QVariantMap datasets);
    void attributeChanged(QString reference, QString attribute, QString before, QString after,
                          QString deviceTime);
    void monitoringChanged(QStringList references);
    void monitorSnapshot(QString reference, QVariantMap snapshot, QString error);
    void connectionLost();

private:
    QString connectionEndpoint_;
    IedConnection connection_ = nullptr;
    struct ConnectionContext {
        Session* session;
        unsigned generation;
    };
    std::unique_ptr<ConnectionContext> connectionContext_;
    uint64_t nextReportTicket_ = 0;
    QHash<QString, MmsVariableSpecification*> specs_;
    QHash<QString, QString> descriptions_;
    QHash<QString, QStringList> members_;
    QHash<QString, ControlObjectClient> controls_;
    QSet<QString> installedReports_;
    QTimer* monitorTimer_ = nullptr;
    QHash<QString, QVariantMap> watches_;
    QHash<QString, QString> watchErrors_;
    QStringList watchOrder_;
    int watchCursor_ = 0;
    QVariantMap watch(const QString& operation, const QString& ref);
    void pollMonitors();
    void notifyConnectionLost();
    bool lostNotified_ = false;
    QHash<QString, std::shared_ptr<ReportBinding>> reportBindings_;
    QHash<QString, std::shared_ptr<MmsValue>> objectValues_;
    QHash<QString, QVariantMap> reportRows_;
    std::atomic_int queuedReports_{0};
    std::atomic_bool reportGap_{false};
    void prepareReport(ClientReportControlBlock block, const QString& ref);
    void processReport(const ReportPacket& packet);
    bool reportCovers(const QString& ref) const;
    QVariantList cachedDataset(const QString& ref) const;
    QVariantMap cachedObject(const QString& ref, FunctionalConstraint fc);
    void attachEnabledReports(const QStringList& reports);
    void queueReportUi(QString dataset, const QVariantList& changes);
    QVariantList pendingReportChanges_;
    QSet<QString> pendingReportDatasets_;
    bool reportUiScheduled_ = false;

    std::atomic_uint generation_{0};
    QVariantMap discover();
    QVariantMap objects(const QString& ln);
    QVariantMap dataSet(const QString& ref);
    QVariantMap object(const QString& ref);
    QVariantMap report(const QVariantMap& args, bool write);
    QVariantMap control(const QVariantMap& args);
    MmsVariableSpecification* specification(const QString& ref, FunctionalConstraint fc);
    QString description(const QString& ref);
    QVariantMap point(const QString& ref, FunctionalConstraint fc, MmsVariableSpecification* spec,
                      MmsValue* value);
    static void onReport(void*, ClientReport);
    static void onTermination(void*, ControlObjectClient);
};

namespace Lite {
QString text(MmsValue* value);
QString quality(MmsValue* value);
QString timestamp(MmsValue* value);
QStringList strings(LinkedList list);
QString normalized(QString ref);
QString mmsDataSet(QString ref);
QString objectRef(QString member);
FunctionalConstraint memberFc(const QString& member);
void check(IedClientError error, const QString& context);
} // namespace Lite
