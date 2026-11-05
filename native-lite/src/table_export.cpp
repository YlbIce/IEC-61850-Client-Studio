#include "table_export.h"
#include <QAbstractItemModel>
#include <QSaveFile>
#include <QStringList>
bool exportModelCsv(const QAbstractItemModel& model, const QString& path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QByteArray csv("\xef\xbb\xbf");
    for (int r = -1; r < model.rowCount(); ++r) {
        QStringList cells;
        for (int c = 0; c < model.columnCount(); ++c) {
            QString value =
                r < 0 ? model.headerData(c, Qt::Horizontal).toString() : model.index(r, c).data().toString();
            if (!value.isEmpty() && QString("=+-@\t\r").contains(value.front()))
                value.prepend('\'');
            value.replace('"', "\"\"");
            cells << '"' + value + '"';
        }
        csv += (cells.join(',') + "\r\n").toUtf8();
    }
    return file.write(csv) == csv.size() && file.commit();
}
