#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("pcpapc172");
    QCoreApplication::setApplicationName("GDLauncher");
    QCoreApplication::setApplicationVersion("2.1.5");

    MainWindow window;
    window.show();

    return app.exec();
}
