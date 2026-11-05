#include "iec61850_client.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <string>
static void report(void *context, ClientReport) {
    ++*static_cast<std::atomic<int> *>(context);
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    auto connection = IedConnection_create();
    IedClientError error;
    IedConnection_connect(connection, &error, "127.0.0.1", std::stoi(argv[1]));
    if (error)
        return 3;
    bool deletable = false;
    auto members =
        IedConnection_getDataSetDirectory(connection, &error, "STUDIOLD0/LLN0.Measurements", &deletable);
    if (error || !members)
        return 4;
    LinkedList_destroy(members);
    std::atomic<int> reports{0};
    const char *name = "STUDIOLD0/LLN0.RP.Measurements01";
    auto rcb = IedConnection_getRCBValues(connection, &error, name, nullptr);
    if (error || !rcb)
        return 5;
    IedConnection_installReportHandler(connection, name, ClientReportControlBlock_getRptId(rcb), report,
                                       &reports);
    ClientReportControlBlock_setRptEna(rcb, true);
    IedConnection_setRCBValues(connection, &error, rcb, RCB_ELEMENT_RPT_ENA, true);
    if (error)
        return 6;
    ClientReportControlBlock_setGI(rcb, true);
    IedConnection_setRCBValues(connection, &error, rcb, RCB_ELEMENT_GI, true);
    if (error)
        return 7;
    for (int i = 0; i < 30 && !reports; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ClientReportControlBlock_setRptEna(rcb, false);
    IedConnection_setRCBValues(connection, &error, rcb, RCB_ELEMENT_RPT_ENA, true);
    IedConnection_uninstallReportHandler(connection, name);
    ClientReportControlBlock_destroy(rcb);
    IedConnection_close(connection);
    IedConnection_destroy(connection);
    std::cout << "Directory and reports: " << reports << std::endl;
    return reports ? 0 : 8;
}
