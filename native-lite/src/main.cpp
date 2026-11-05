#include "main_window.h"
#include <QApplication>
#include <QSettings>
#include <QTranslator>
int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    app.setFont(QFont("Microsoft YaHei", 9));
    app.setOrganizationName("IEC61850Studio");
    app.setApplicationName("ClientLite");
    app.setApplicationVersion("0.1.6");
    QTranslator translator;
    if (translator.load("qt_zh_CN", app.applicationDirPath() + "/translations"))
        app.installTranslator(&translator);
    MainWindow window;
    window.show();
    return app.exec();
}
