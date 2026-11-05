#pragma once
#include <QHash>
#include <QVariantMap>
#include <QWidget>
class QTreeWidget;
class QTreeWidgetItem;

// Presents worker snapshots only; this widget never performs network reads.
class MonitorPanel : public QWidget {
    Q_OBJECT
public:
    explicit MonitorPanel(QWidget* parent = nullptr);
    void setReferences(const QStringList& references);
    void updateSnapshot(QString reference, QVariantMap snapshot, QString error);
signals:
    void removeRequested(QString reference);
    void clearRequested();
    void objectSelected(QString reference);

private:
    QTreeWidget* tree_;
    QHash<QString, QTreeWidgetItem*> objects_;
};
