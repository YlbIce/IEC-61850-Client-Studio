#pragma once
#include "point_model.h"
#include <QDialog>
class TestResultDialog : public QDialog {
public:
    TestResultDialog(PointModel* model, const QModelIndex& index, QString initialStatus,
                     QWidget* parent = nullptr);
};
