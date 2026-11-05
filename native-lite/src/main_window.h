#pragma once
#include "event_log.h"
#include "point_model.h"
#include "session.h"
#include <QHash>
#include <QMainWindow>
#include <QThread>
#include <QTimer>
class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class MonitorPanel;
class HistoryPanel;
class QProgressDialog;
class QTableView;
class QPlainTextEdit;
class QTabWidget;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    bool request(const QString& operation, QVariantMap args = {});
    PointModel* points() const {
        return points_;
    }
    bool isBusy() const {
        return busy_;
    }
signals:
    void command(QString operation, QVariantMap args);
    void completed(QString operation, QVariantMap data);
    void error(QString operation, QString message);

private:
    Session* session_;
    QThread thread_;
    QTimer timer_;
    QProgressDialog* progress_ = nullptr;
    bool loadingModel_ = false;
    bool busy_ = false, connected_ = false, autoRefresh_ = true;
    QString endpoint_, selectedDataset_;
    QTreeWidget* tree_;
    QListWidget* datasets_;
    QListWidget* reports_;
    QTableView* table_;
    PointModel* points_;
    EventLog* log_;
    HistoryPanel* history_;
    TestFilterModel* filteredPoints_;
    MonitorPanel* monitors_;
    QSet<QString> watched_;
    QLabel* testCounts_;
    void editTestResult(const QModelIndex& sourceIndex, QString initialStatus = {});
    void updateTestCounts();
    QTabWidget* tabs_;
    QLabel* reportInfo_;
    QLabel* connectionBadge_;
    void updateConnectionBadge(bool connected);
    QHash<QString, QTreeWidgetItem*> nodes_;
    void receive(QString operation, QVariantMap data);
    void connectDialog();
    void refreshValues();
    void reportDialog(QVariantMap data);
    void controlDialog(QVariantMap data);
    void addLog(const QString& message);
};
