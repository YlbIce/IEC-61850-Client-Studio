#pragma once
#include "history_model.h"
#include <QWidget>
class EventLog;
class QTabWidget;
class QTableView;
class QLineEdit;
class QCheckBox;
class QLabel;
class HistoryPanel : public QWidget {
public:
    explicit HistoryPanel(QWidget* parent = nullptr);
    EventLog* eventLog() const {
        return log_;
    }
    HistoryModel* model() const {
        return model_;
    }
    void selectObject(QString device, QString reference);

private:
    void updateFilter();
    void copySelected();
    void exportCurrent();
    HistoryModel* model_;
    HistoryFilter* filter_;
    EventLog* log_;
    QTabWidget* tabs_;
    QTableView* table_;
    QLineEdit* search_;
    QCheckBox* follow_;
    QLabel* selected_;
    QLabel* count_;
    QWidget* history_;
    QString device_, reference_;
};
