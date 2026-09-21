#include <QApplication>
#include <QStyleFactory>
#include <QIcon>
#include "MainWindow.h"
#include "Settings.h"
#include "ui/Theme.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("pcpapc172");
    QCoreApplication::setApplicationName("GDLauncher");
    QCoreApplication::setApplicationVersion("2.1.5");

    // Fusion renders our QSS identically across platforms instead of picking up
    // each OS's native widget chrome, which is what keeps the look consistent.
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    app.setWindowIcon(QIcon(":/icon.png"));

    Settings::ensureDirs();
    const AppSettings settings = Settings::load();
    Theme::apply(Theme::fromSettingsString(settings.theme));

    MainWindow window;
    window.show();

    return app.exec();
}
