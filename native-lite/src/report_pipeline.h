#pragma once
#include <QStringList>
#include <QVariantMap>
#include <atomic>
#include <memory>
extern "C" {
#include "iec61850_client.h"
}
class Session;
struct ReportBinding {
    Session* session = nullptr;
    unsigned generation = 0;
    uint64_t ticket = 0;
    QString reference, dataset;
    QStringList members;
    uint32_t confRev = 0;
    bool enabled = false;
    bool received = false;
};
struct ReportPacket {
    QString reference, dataset;
    unsigned generation = 0;
    uint64_t ticket = 0;
    uint32_t confRev = 0;
    bool hasConfRev = false, overflow = false;
    int sequence = 0;
    QVector<int> indexes;
    QVector<std::shared_ptr<MmsValue>> values;
};
