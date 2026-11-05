#include "connection_dialog.h"
#include "connection_options.h"
#include "history_panel.h"
#include "main_window.h"
#include "monitor_panel.h"
#include "session.h"
#include "table_export.h"
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTreeWidget>
#include <QtTest>

class Integration : public QObject {
    Q_OBJECT
private slots:
    void emptyDirectory() {
        QVERIFY(Lite::strings(nullptr).isEmpty());
    }
    void connectionParameters() {
        QVariantMap args{
            {"host", "127.0.0.1"}, {"port", 102}, {"addressMode", "omitApTitle"}, {"remoteT", "00 02"}};
        auto connection = IedConnection_create();
        ConnectionOptions::apply(connection, args);
        auto iso = MmsConnection_getIsoConnectionParameters(IedConnection_getMmsConnection(connection));
        QCOMPARE(iso->remoteApTitleLen, 0);
        QCOMPARE(iso->localApTitleLen, 0);
        QCOMPARE(int(iso->remoteTSelector.size), 2);
        QCOMPARE(int(iso->remoteTSelector.value[1]), 2);
        QCOMPARE(int(iso->localPSelector.size), 4);
        args["addressMode"] = "custom";
        args["remoteApTitle"] = "1 3 9999 33";
        args["remoteAeQualifier"] = 33;
        ConnectionOptions::apply(connection, args);
        QVERIFY(iso->remoteApTitleLen > 0);
        QCOMPARE(iso->remoteAEQualifier, 33);
        args["remoteApTitle"] = "1.3.0.0.0.0.0.0.0.0.0.0.0";
        QVERIFY_EXCEPTION_THROWN(ConnectionOptions::validate(args), std::runtime_error);
        args["remoteApTitle"] = "1.3.9999.33";
        IedConnection_destroy(connection);
        args["remoteT"] = "0G";
        QVERIFY_EXCEPTION_THROWN(ConnectionOptions::validate(args), std::runtime_error);
        args["remoteT"] = "0001020304";
        QVERIFY_EXCEPTION_THROWN(ConnectionOptions::validate(args), std::runtime_error);
        args["remoteT"] = "";
        ConnectionOptions::validate(args);
        args["requestTimeout"] = -1;
        QVERIFY_EXCEPTION_THROWN(ConnectionOptions::validate(args), std::runtime_error);
        ConnectionDialog dialog;
        dialog.show();
        QTest::qWait(80);
        QVERIFY(dialog.grab().save("native-lite-connection.png"));
    }
    void historyRecords() {
        HistoryModel model;
        HistoryFilter filter(&model);
        QVariantMap row{{"ref", "IED/GGIO1.Ind1"}, {"kind", "digital"}, {"fc", "ST"},   {"sampleValid", true},
                        {"value", "false"},        {"quality", "良好"}, {"time", "T1"}, {"name", "开关"}};
        model.samples("a:102", {row}, "监视");
        filter.configure(0, "", "", "");
        QCOMPARE(filter.rowCount(), 0);
        model.samples("a:102", {row}, "数据集");
        QCOMPARE(model.rowCount(), 1);
        row["value"] = "true";
        row["time"] = "T2";
        model.samples("a:102", {row}, "监视");
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.index(0, 9).data().toString(), QString("false"));
        filter.configure(2, "a:102", "IED/GGIO1.Ind1", "");
        QCOMPARE(filter.rowCount(), 2);
        filter.configure(2, "b:102", "IED/GGIO1.Ind1", "");
        QCOMPARE(filter.rowCount(), 0);
        row["sampleValid"] = false;
        model.samples("a:102", {row}, "失败");
        QCOMPARE(model.rowCount(), 2);
        row["sampleValid"] = true;
        model.resetBaseline();
        model.samples("a:102", {row}, "重连");
        filter.configure(0, "", "", "");
        QCOMPARE(filter.rowCount(), 1);
        row["ref"] = "IED/MMTR1.TotWh";
        row["kind"] = "counter";
        row["value"] = "actVal=123456";
        model.samples("a:102", {row}, "监视");
        filter.configure(3, "a:102", "IED/MMTR1.TotWh", "");
        QCOMPARE(filter.rowCount(), 1);
        model.link("a:102", "连接成功", "关联完成");
        filter.configure(4, "", "", "");
        QCOMPARE(filter.rowCount(), 1);
        for (int i = 0; i < 5100; ++i)
            model.link("a:102", "事件", QString::number(i));
        QCOMPARE(model.rowCount(), 5000);
        QTemporaryDir output;
        QVERIFY(exportModelCsv(filter, output.filePath("history.csv")));
        QFile exported(output.filePath("history.csv"));
        QVERIFY(exported.open(QIODevice::ReadOnly));
        auto csv = exported.readAll();
        QVERIFY(csv.startsWith("\xef\xbb\xbf"));
        QVERIFY(csv.contains("a:102"));
        model.clear();
        QCOMPARE(model.rowCount(), 0);
    }
    void virtualTable() {
        PointModel model;
        QVariantList rows;
        for (int i = 0; i < 20000; ++i)
            rows << QVariantMap{{"ref", QString("IED/GGIO1.AnIn%1").arg(i)}, {"value", i}};
        QElapsedTimer timer;
        timer.start();
        model.update("perf:102", "IED/LLN0.Large", rows);
        QCOMPARE(model.rowCount(), 20000);
        QCOMPARE(model.reference(19999), QString("IED/GGIO1.AnIn19999"));
        qInfo() << "20000-row model load milliseconds:" << timer.elapsed();
        timer.restart();
        model.update("perf:102", "IED/LLN0.Large", rows);
        qInfo() << "20000-row model update milliseconds:" << timer.elapsed();
    }
    void markers() {
        PointModel model;
        QVariantList rows{QVariantMap{{"ref", "IED/LLN0.Ind1"}, {"value", "true"}}};
        model.update("device-a:102", "IED/LLN0.Events", rows);
        QVERIFY(model.setData(model.index(0, 5), Qt::Checked, Qt::CheckStateRole));
        PointModel reopened;
        reopened.update("device-a:102", "IED/LLN0.Events", rows);
        QCOMPARE(reopened.testRecord(0)["status"].toString(), QString("tested"));
        reopened.update("device-b:102", "IED/LLN0.Events", rows);
        QCOMPARE(reopened.testRecord(0)["status"].toString(), QString("untested"));
        model.update("device-a:102", "IED/LLN0.Events", rows);
        QCOMPARE(model.testRecord(0)["status"].toString(), QString("tested"));
        QVERIFY(!model.setTestResult(0, "failure", "   "));
        QVERIFY(model.setTestResult(0, "failure", "现场值偏差超过允许范围"));
        PointModel persisted;
        persisted.update("device-a:102", "IED/LLN0.Events", rows);
        QCOMPARE(persisted.testRecord(0)["status"].toString(), QString("failure"));
        QCOMPARE(persisted.testRecord(0)["reason"].toString(), QString("现场值偏差超过允许范围"));
        QCOMPARE(persisted.data(persisted.index(0, 0), Qt::BackgroundRole).value<QColor>(),
                 QColor("#fdecec"));
        TestFilterModel filter;
        filter.setSourceModel(&persisted);
        filter.setStatus("failure");
        QCOMPARE(filter.rowCount(), 1);
        QVERIFY(persisted.setTestResult(0, "success"));
        QCOMPARE(filter.rowCount(), 0);
        filter.setStatus("done");
        QCOMPARE(filter.rowCount(), 1);
        QVERIFY(persisted.setTestResult(0, "untested"));
        QCOMPARE(filter.rowCount(), 0);
        // Timestamp-only 0.1.1 settings must migrate without inventing a pass.
        QByteArray identity("legacy:102");
        identity += '\0';
        identity += "IED/LLN0.Events";
        identity += '\0';
        identity += "IED/LLN0.Ind1";
        QSettings settings;
        settings.setValue(
            "tested/" +
                QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex()),
            "2026-09-01T01:02:03Z");
        settings.sync();
        PointModel legacy;
        legacy.update("legacy:102", "IED/LLN0.Events", rows);
        QCOMPARE(legacy.testRecord(0)["status"].toString(), QString("tested"));
    }
    void logFilteringAndExport() {
        EventLog log;
        log.append("通信", "已连接");
        log.change("IED/GGIO1.AnIn1", "IED/GGIO1.AnIn1.mag.f[MX]", "220", "221", "2026-09-15 UTC");
        log.change("IED/GGIO1.AnIn1", "IED/GGIO1.AnIn1.dU[DC]", "旧名", "=TEST,\"文本\"", "UTC");
        log.findChild<QComboBox*>("logCategory")->setCurrentText("属性变化");
        QVERIFY(!log.visibleText().contains("已连接"));
        QVERIFY(log.visibleText().contains("220 → 221"));
        log.resize(1000, 240);
        log.show();
        QTest::qWait(80);
        log.grab();
        auto textView = log.findChild<QPlainTextEdit*>("eventLogText");
        auto stripes = textView->extraSelections();
        QVERIFY(stripes.size() >= 2);
        QCOMPARE(stripes[0].format.background().color(), QColor("#ffffff"));
        QCOMPARE(stripes[1].format.background().color(), QColor("#e7eff8"));
        QVERIFY(stripes[0].format.boolProperty(QTextFormat::FullWidthSelection));
        QTemporaryDir dir;
        auto path = dir.filePath("events.csv");
        QVERIFY(log.exportCsv(path));
        QFile csv(path);
        QVERIFY(csv.open(QIODevice::ReadOnly));
        auto bytes = csv.readAll();
        QVERIFY(bytes.startsWith("\xef\xbb\xbf"));
        QVERIFY(bytes.contains("'=TEST"));
        for (int i = 0; i < 3100; ++i)
            log.append("通信", QString::number(i));
        QVERIFY(log.entryCount() <= 3000);
    }
    void monitorSnapshotLifecycle() {
        MonitorPanel panel;
        panel.setReferences({"IED/GGIO1.AnIn1"});
        QVariantMap snapshot{
            {"rows", QVariantList{QVariantMap{
                         {"name", "电压"}, {"value", "220"}, {"quality", "良好"}, {"time", "UTC"}}}},
            {"attributes", QVariantMap{{"IED/GGIO1.AnIn1.mag.f[MX]", "220"}}},
            {"receivedAt", "2026-09-15 12:00:00"}};
        panel.updateSnapshot("IED/GGIO1.AnIn1", snapshot, {});
        auto tree = panel.findChild<QTreeWidget*>("monitorTable");
        auto item = tree->topLevelItem(0);
        item->setExpanded(true);
        tree->setCurrentItem(item);
        QCOMPARE(item->text(1), QString("电压"));
        QCOMPARE(item->text(2), QString("220"));
        QCOMPARE(item->child(0)->text(2), QString("220"));
        panel.updateSnapshot("IED/GGIO1.AnIn1", {}, "超时");
        QVERIFY(item->text(5).contains("过期"));
        QCOMPARE(item->text(2), QString("220"));
        panel.updateSnapshot("IED/GGIO1.AnIn1", snapshot, {});
        QCOMPARE(tree->currentItem(), item);
        QVERIFY(item->isExpanded());
        QVERIFY(item->text(5).contains("正常"));
        panel.setReferences({});
        QCOMPARE(tree->topLevelItemCount(), 0);
    }
    void mms() {
        if (qEnvironmentVariableIsEmpty("LITE_TEST_PORT"))
            QSKIP("Set LITE_TEST_PORT to owned local Service Studio instance");
        Session session;
        QSignalSpy results(&session, &Session::result), failures(&session, &Session::failed),
            events(&session, &Session::event);
        auto run = [&](QString op, QVariantMap args = {}) {
            results.clear();
            failures.clear();
            session.execute(op, args);
            if (!failures.isEmpty())
                qWarning() << failures.first();
            return results.isEmpty() ? QVariantMap{} : results.last()[1].toMap();
        };
        auto model =
            run("connect", {{"host", "127.0.0.1"}, {"port", qEnvironmentVariableIntValue("LITE_TEST_PORT")}});
        QVERIFY(!model["nodes"].toList().isEmpty());
        QVERIFY(!model["datasets"].toStringList().isEmpty());
        QVERIFY(!model["reports"].toStringList().isEmpty());
        // The opt-in ACSE mode must work against the owned MMS server too.
        auto compatible = run("connect", {{"host", "127.0.0.1"},
                                          {"port", qEnvironmentVariableIntValue("LITE_TEST_PORT")},
                                          {"addressMode", "omitApTitle"}});
        QVERIFY(!compatible["nodes"].toList().isEmpty());
        QFile profile("profiles/atc61850-local1.json");
        QVERIFY(profile.open(QIODevice::ReadOnly));
        auto configured = QJsonDocument::fromJson(profile.readAll()).object().toVariantMap();
        configured["host"] = "127.0.0.1";
        configured["port"] = qEnvironmentVariableIntValue("LITE_TEST_PORT");
        QVERIFY(!run("connect", configured)["nodes"].toList().isEmpty());
        auto nodes = run("objects", {{"ref", "STUDIOLD0/GGIO1"}});
        QVERIFY(nodes["names"].toStringList().contains("AnIn1"));
        auto ds = run("dataset", {{"ref", "STUDIOLD0/LLN0.Measurements"}});
        QCOMPARE(ds["rows"].toList().size(), 4);
        auto row = ds["rows"].toList().first().toMap();
        QVERIFY(row["name"].toString().contains("电压"));
        QVERIFY(row["value"].toString().contains("220"));
        QCOMPARE(row["kind"].toString(), QString("analog"));
        QVERIFY(row["sampleValid"].toBool());
        QVERIFY(row["quality"].toString().contains("良好"));
        QVERIFY(row["time"].toString().contains("UTC"));
        auto obj = run("object", {{"ref", "STUDIOLD0/GGIO1.AnIn1"}});
        QVERIFY(!obj["rows"].toList().isEmpty());
        QVERIFY(obj["attributes"].toMap().contains("STUDIOLD0/GGIO1.AnIn1.mag.f[MX]"));
        QString rcb;
        for (auto ref : model["reports"].toStringList())
            if (ref.contains("Measurements")) {
                rcb = ref;
                break;
            }
        QVERIFY(!rcb.isEmpty());
        auto params = run("report", {{"ref", rcb}});
        QVERIFY(!params.isEmpty());
        params["enabled"] = true;
        params["gi"] = true;
        params["reserve"] = true;
        params["bufTm"] = 25;
        params["trgOps"] = int(TRG_OPT_DATA_CHANGED | TRG_OPT_GI);
        params["optFlds"] = int(RPT_OPT_SEQ_NUM | RPT_OPT_DATA_SET | RPT_OPT_REASON_FOR_INCLUSION);
        auto saved = run("saveReport", params);
        QVERIFY(saved["enabled"].toBool());
        QCOMPARE(saved["bufTm"].toInt(), 25);
        QTRY_VERIFY_WITH_TIMEOUT(!events.isEmpty(), 5000);
        params["enabled"] = false;
        params["gi"] = false;
        QVERIFY(!run("saveReport", params).isEmpty());
        auto ctl = run("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}, {"action", "inspect"}});
        QCOMPARE(ctl["model"].toInt(), int(CONTROL_MODEL_DIRECT_NORMAL));
        QVERIFY(!run("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}, {"action", "operate"}, {"value", "1"}})
                     .isEmpty());
        auto state = run("object", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}});
        QVERIFY(state["rows"].toList().first().toMap()["value"].toString().contains("true"));
        auto enhanced = run("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO2"}, {"action", "inspect"}});
        QCOMPARE(enhanced["model"].toInt(), int(CONTROL_MODEL_SBO_ENHANCED));
        events.clear();
        QVERIFY(!run("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO2"}, {"action", "select"}, {"value", "1"}})
                     .isEmpty());
        QVERIFY(!run("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO2"}, {"action", "operate"}, {"value", "1"}})
                     .isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (const auto& event : events)
                    if (event[0].toString().contains("命令终结"))
                        return true;
                return false;
            }(),
            5000);
        QSignalSpy changes(&session, &Session::attributeChanged);
        QVERIFY(!run("watchAdd", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}}).isEmpty());
        QTest::qWait(1100);
        QCOMPARE(changes.size(), 0);
        QVERIFY(!run("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}, {"action", "operate"}, {"value", "0"}})
                     .isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (const auto& change : changes)
                    if (change[1].toString().endsWith("stVal[ST]") && change[2] == "true" &&
                        change[3] == "false")
                        return true;
                return false;
            }(),
            5000);
        run("watchRemove", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}});
        changes.clear();
        run("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}, {"action", "operate"}, {"value", "1"}});
        QTest::qWait(1100);
        QVERIFY(changes.isEmpty());
        run("disconnect");
        run("dataset", {{"ref", "STUDIOLD0/LLN0.Measurements"}});
        QVERIFY(!failures.isEmpty());
    }
    void backgroundReports() {
        if (qEnvironmentVariableIsEmpty("LITE_TEST_PORT"))
            QSKIP("Requires owned MMS test server");
        Session session;
        QSignalSpy result(&session, &Session::result), errors(&session, &Session::failed),
            reports(&session, &Session::reportData), lost(&session, &Session::connectionLost),
            progress(&session, &Session::connectionProgress);
        auto run = [&](QString operation, QVariantMap args = {}) {
            result.clear();
            errors.clear();
            session.execute(operation, args);
            if (!errors.isEmpty())
                qWarning() << errors.last();
            return result.isEmpty() ? QVariantMap{} : result.last()[1].toMap();
        };
        QVariantMap model;
        for (int i = 0; i < 3; ++i) {
            model = run("connect",
                        {{"host", "127.0.0.1"}, {"port", qEnvironmentVariableIntValue("LITE_TEST_PORT")}});
            QVERIFY(!model["nodes"].toList().isEmpty());
            QTest::qWait(80);
        }
        QCOMPARE(lost.size(), 0);
        QCOMPARE(progress.last()[0].toInt(), 100);
        QString rcb;
        for (auto ref : model["reports"].toStringList())
            if (ref.contains(".BR.Events"))
                rcb = ref;
        QVERIFY(!rcb.isEmpty());
        auto params = run("report", {{"ref", rcb}});
        params["enabled"] = true;
        params["gi"] = true;
        params["bufTm"] = 0;
        params["trgOps"] = int(TRG_OPT_DATA_CHANGED | TRG_OPT_GI);
        params["optFlds"] =
            int(RPT_OPT_SEQ_NUM | RPT_OPT_DATA_SET | RPT_OPT_CONF_REV | RPT_OPT_REASON_FOR_INCLUSION);
        reports.clear();
        QVERIFY(!run("saveReport", params).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(reports.size() >= 2, 5000);
        auto cached = run("dataset", {{"ref", "STUDIOLD0/LLN0.Events"}});
        QCOMPARE(cached["source"].toString(), QString("报告缓存"));
        // Read a different dataset; the Events subscription must keep recording both transitions.
        QVERIFY(!run("dataset", {{"ref", "STUDIOLD0/LLN0.Measurements"}}).isEmpty());
        run("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}, {"action", "operate"}, {"value", "0"}});
        QTest::qWait(100);
        reports.clear();
        run("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}, {"action", "operate"}, {"value", "1"}});
        run("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}, {"action", "operate"}, {"value", "0"}});
        auto saw = [&](QString value) {
            for (const auto& emission : reports)
                for (const auto& v : emission[2].toList()) {
                    auto row = v.toMap();
                    if (row["ref"] == "STUDIOLD0/GGIO1.SPCSO1" && row["value"].toString().contains(value))
                        return true;
                }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(saw("true") && saw("false"), 5000);
        params["enabled"] = false;
        params["gi"] = false;
        QVERIFY(!run("saveReport", params).isEmpty());
        QVERIFY(run("dataset", {{"ref", "STUDIOLD0/LLN0.Events"}})["source"] != "报告缓存");
        // Create a real FCDA dataset: partial report elements must merge into the object's cached fields.
        auto raw = std::unique_ptr<sIedConnection, decltype(&IedConnection_destroy)>(IedConnection_create(),
                                                                                     IedConnection_destroy);
        IedClientError err;
        IedConnection_connect(raw.get(), &err, "127.0.0.1", qEnvironmentVariableIntValue("LITE_TEST_PORT"));
        QCOMPARE(err, IED_ERROR_OK);
        auto members = LinkedList_create();
        for (auto reference : {"STUDIOLD0/GGIO1.SPCSO1.stVal[ST]", "STUDIOLD0/GGIO1.SPCSO1.q[ST]",
                               "STUDIOLD0/GGIO1.SPCSO1.t[ST]"})
            LinkedList_add(members, const_cast<char*>(reference));
        IedConnection_createDataSet(raw.get(), &err, "STUDIOLD0/LLN0.AttributeEvents", members);
        LinkedList_destroyStatic(members);
        QCOMPARE(err, IED_ERROR_OK);
        params["dataset"] = "STUDIOLD0/LLN0.AttributeEvents";
        params["enabled"] = true;
        params["gi"] = true;
        reports.clear();
        QVERIFY(!run("saveReport", params).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(reports.size() >= 2, 5000);
        reports.clear();
        run("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}, {"action", "operate"}, {"value", "1"}});
        QTRY_VERIFY_WITH_TIMEOUT(saw("true"), 5000);
        auto attributes = run("dataset", {{"ref", "STUDIOLD0/LLN0.AttributeEvents"}});
        QCOMPARE(attributes["source"].toString(), QString("报告缓存"));
        QCOMPARE(attributes["rows"].toList().size(), 1);
        QVERIFY(attributes["rows"].toList().first().toMap()["quality"].toString().contains("良好"));
        params["enabled"] = false;
        params["gi"] = false;
        params["dataset"] = "STUDIOLD0/LLN0.Events";
        QVERIFY(!run("saveReport", params).isEmpty());
        IedConnection_deleteDataSet(raw.get(), &err, "STUDIOLD0/LLN0.AttributeEvents");
        QCOMPARE(err, IED_ERROR_OK);

        run("disconnect");
        QTest::qWait(80);
        QCOMPARE(lost.size(), 0);
    }
    void backgroundWindow() {
        if (qEnvironmentVariableIsEmpty("LITE_TEST_PORT"))
            QSKIP("Requires owned MMS test server");
        MainWindow window;
        window.show();
        QSignalSpy completed(&window, &MainWindow::completed), failures(&window, &MainWindow::error);
        QVERIFY(window.request(
            "connect", {{"host", "127.0.0.1"}, {"port", qEnvironmentVariableIntValue("LITE_TEST_PORT")}}));
        QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(), 15000);
        auto pages = window.findChild<QTabWidget*>();
        pages->setCurrentIndex(1);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 5000);
        // Select Measurements, but keep the online model page visible (no dataset timer reads).
        for (auto list : window.findChildren<QListWidget*>())
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->text() == "STUDIOLD0/LLN0.Measurements")
                    list->setCurrentRow(i);
        Session operatorSession;
        QSignalSpy replies(&operatorSession, &Session::result);
        operatorSession.execute(
            "connect", {{"host", "127.0.0.1"}, {"port", qEnvironmentVariableIntValue("LITE_TEST_PORT")}});
        auto discovery = replies.last()[1].toMap();
        QString rcb;
        for (auto ref : discovery["reports"].toStringList())
            if (ref.contains(".BR.Events"))
                rcb = ref;
        QVERIFY(!rcb.isEmpty());
        operatorSession.execute("control",
                                {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}, {"action", "operate"}, {"value", "0"}});
        operatorSession.execute("report", {{"ref", rcb}});
        auto params = replies.last()[1].toMap();
        params["enabled"] = true;
        params["gi"] = true;
        params["bufTm"] = 0;
        params["trgOps"] = int(TRG_OPT_DATA_CHANGED | TRG_OPT_GI);
        QVERIFY(window.request("saveReport", params));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 5000);
        QTest::qWait(150);
        operatorSession.execute("control",
                                {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}, {"action", "operate"}, {"value", "1"}});
        auto history = window.findChild<QTableView*>("historyTable");
        window.findChild<QTabWidget*>("historyTabs")->setCurrentIndex(0);
        auto received = [&] {
            for (int i = 0; i < history->model()->rowCount(); ++i)
                if (history->model()->index(i, 2).data() == "STUDIOLD0/GGIO1.SPCSO1" &&
                    history->model()->index(i, 4).data().toString().contains("true") &&
                    history->model()->index(i, 8).data() == "MMS 报告")
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(received(), 5000);
        QVERIFY(failures.isEmpty());
        window.resize(1440, 850);
        QTest::qWait(100);
        QVERIFY(window.grab().save("native-lite-background-report.png"));
        params["enabled"] = false;
        params["gi"] = false;
        QVERIFY(window.request("saveReport", params));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 5000);
    }
    void window() {
        MainWindow window;
        window.show();
        QTest::qWait(200);
        QVERIFY(window.isVisible());
        if (!qEnvironmentVariableIsEmpty("LITE_TEST_PORT")) {
            QSignalSpy done(&window, &MainWindow::completed);
            QVERIFY(window.request("connect", {{"host", "127.0.0.1"},
                                               {"port", qEnvironmentVariableIntValue("LITE_TEST_PORT")}}));
            auto connectionProgress = window.findChild<QProgressDialog*>("connectionProgress");
            QVERIFY(connectionProgress && connectionProgress->isVisible());
            QVERIFY(connectionProgress->grab().save("native-lite-progress.png"));
            QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 15000);
            for (auto list : window.findChildren<QListWidget*>()) {
                auto items = list->findItems("STUDIOLD0/LLN0.Measurements", Qt::MatchExactly);
                if (!items.isEmpty())
                    list->setCurrentItem(items.first());
            }
            QTRY_COMPARE_WITH_TIMEOUT(window.points()->rowCount(), 4, 15000);
            QVERIFY(!window.windowIcon().isNull());
            QVERIFY(!window.windowIcon().pixmap(32, 32).isNull());
            window.points()->setTestResult(0, "success");
            window.points()->setTestResult(1, "failure", "现场电流与参考表偏差超限");
            window.points()->setTestResult(3, "tested");
            auto filter = window.findChild<QComboBox*>("testFilter");
            QVERIFY(filter);
            filter->setCurrentIndex(filter->findData("failure"));
            auto table = window.findChild<QTableView*>("datasetTable");
            QVERIFY(table);
            QCOMPARE(table->model()->rowCount(), 1);
            bool triggered = false;
            QTimer::singleShot(60, [&] {
                auto menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (menu) {
                    for (auto action : menu->actions())
                        if (action->text() == "标记已测成功") {
                            action->trigger();
                            triggered = true;
                            break;
                        }
                    menu->close();
                }
            });
            QMetaObject::invokeMethod(table, "customContextMenuRequested", Qt::DirectConnection,
                                      Q_ARG(QPoint, table->visualRect(table->model()->index(0, 2)).center()));
            QVERIFY(triggered);
            QCOMPARE(window.points()->testRecord(1)["status"].toString(), QString("success"));
            QCOMPARE(table->model()->rowCount(), 0);
            window.points()->setTestResult(1, "failure", "现场电流与参考表偏差超限");
            filter->setCurrentIndex(filter->findData("all"));
            QMetaObject::invokeMethod(table, "doubleClicked", Qt::DirectConnection,
                                      Q_ARG(QModelIndex, table->model()->index(1, 5)));
            QTRY_VERIFY(QApplication::activeModalWidget() != nullptr);
            auto resultDialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(resultDialog);
            auto reason = resultDialog->findChild<QPlainTextEdit*>("testFailureReason");
            QVERIFY(reason);
            reason->clear();
            resultDialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            QVERIFY(resultDialog->isVisible());
            reason->setPlainText("现场电流与参考表偏差超限");
            QVERIFY(resultDialog->grab().save("native-lite-test-result.png"));
            resultDialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            QTest::qWait(50);
            QCOMPARE(window.points()->testRecord(1)["reason"].toString(),
                     QString("现场电流与参考表偏差超限"));
            QVERIFY(window.grab().save("native-lite-screenshot.png"));
            window.resize(1024, 700);
            QTest::qWait(80);
            QVERIFY(window.grab().save("native-lite-compact.png"));
            for (auto list : window.findChildren<QListWidget*>()) {
                auto items = list->findItems("STUDIOLD0/LLN0.Events", Qt::MatchExactly);
                if (!items.isEmpty())
                    list->setCurrentItem(items.first());
            }
            QTRY_VERIFY_WITH_TIMEOUT(window.points()->reference(0).contains("Ind1"), 10000);
            int controlRow = -1;
            for (int i = 0; i < window.points()->rowCount(); ++i)
                if (window.points()->reference(i).endsWith("SPCSO1"))
                    controlRow = i;
            QVERIFY(controlRow >= 0);
            QVERIFY(window.points()->setData(window.points()->index(controlRow, 5), Qt::Checked,
                                             Qt::CheckStateRole));
            QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 5000);
            QVERIFY(window.request("watchAdd", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}}));
            QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QTreeWidget*>("monitorTable")->topLevelItemCount(), 1,
                                      5000);
            auto monitorTree = window.findChild<QTreeWidget*>("monitorTable");
            auto monitored = monitorTree->topLevelItem(0);
            QTRY_VERIFY_WITH_TIMEOUT(monitored->text(2).contains("true"), 5000);
            QVERIFY(monitored->text(3).contains("良好"));
            QVERIFY(monitored->text(4).contains("UTC"));
            QVERIFY(monitored->childCount() > 0);
            monitored->setExpanded(true);
            monitorTree->setCurrentItem(monitored);
            // Monitoring must continue on another page, without dataset polling.
            window.findChild<QTabWidget*>()->setCurrentIndex(3);
            Session operatorSession;
            QSignalSpy errors(&operatorSession, &Session::failed);
            operatorSession.execute(
                "connect", {{"host", "127.0.0.1"}, {"port", qEnvironmentVariableIntValue("LITE_TEST_PORT")}});
            operatorSession.execute(
                "control", {{"ref", "STUDIOLD0/GGIO1.SPCSO1"}, {"action", "operate"}, {"value", "0"}});
            QVERIFY(errors.isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(window.findChild<EventLog*>()->visibleText().contains("stVal[ST]"),
                                     6000);
            QTRY_VERIFY_WITH_TIMEOUT(monitored->text(2).contains("false"), 6000);
            QCOMPARE(monitorTree->currentItem(), monitored);
            QVERIFY(monitored->isExpanded());
            window.resize(1440, 850);
            QTest::qWait(80);
            QVERIFY(window.grab().save("native-lite-monitor.png"));
            auto historyTabs = window.findChild<QTabWidget*>("historyTabs");
            auto historyTable = window.findChild<QTableView*>("historyTable");
            historyTabs->setCurrentIndex(0);
            QTRY_VERIFY(historyTable->model()->rowCount() > 0);
            QVERIFY(historyTable->model()
                        ->index(historyTable->model()->rowCount() - 1, 4)
                        .data()
                        .toString()
                        .contains("false"));
            QTest::qWait(100);
            QVERIFY(window.grab().save("native-lite-history.png"));
            historyTabs->setCurrentIndex(2);
            QVERIFY(historyTable->model()->rowCount() >= 2);
            historyTabs->setCurrentIndex(4);
            QVERIFY(historyTable->model()->rowCount() >= 2);
            historyTabs->setCurrentIndex(5);
            window.findChild<QTabWidget*>()->setCurrentIndex(0);
            QTRY_VERIFY_WITH_TIMEOUT(window.points()
                                         ->data(window.points()->index(controlRow, 2), Qt::DisplayRole)
                                         .toString()
                                         .contains("false"),
                                     6000);
            QCOMPARE(window.points()->testRecord(controlRow)["status"].toString(), QString("tested"));
            window.findChild<QTabWidget*>()->setCurrentIndex(2);
            QString reportRef;
            for (auto list : window.findChildren<QListWidget*>())
                for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->text().contains(".RP.Measurements") ||
                        list->item(i)->text().contains(".BR.Measurements"))
                        reportRef = list->item(i)->text();
            QVERIFY(!reportRef.isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(!window.isBusy(), 5000);
            QVERIFY(window.request("report", {{"ref", reportRef}}));
            QTRY_VERIFY_WITH_TIMEOUT(QApplication::activeModalWidget() != nullptr, 5000);
            auto dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QVERIFY(dialog->grab().save("native-lite-report.png"));
            dialog->reject();
            QTest::qWait(50);
            QVERIFY(window.request("control", {{"ref", "STUDIOLD0/GGIO1.SPCSO2"}, {"action", "inspect"}}));
            QTRY_VERIFY_WITH_TIMEOUT(QApplication::activeModalWidget() != nullptr, 5000);
            dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QVERIFY(dialog->grab().save("native-lite-control.png"));
            dialog->reject();
        }
    }
};
int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    app.setOrganizationName("IEC61850StudioTests");
    app.setApplicationName("ClientLiteTests");
    app.setFont(QFont("Microsoft YaHei", 9));
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    Integration tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "integration.moc"
