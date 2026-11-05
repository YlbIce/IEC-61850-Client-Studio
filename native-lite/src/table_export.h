#pragma once
#include <QString>
class QAbstractItemModel;
bool exportModelCsv(const QAbstractItemModel& model, const QString& path);
