#include "workbench.hpp"

#include <QApplication>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("StarDataParadise"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    const bool smoke = app.arguments().contains(QStringLiteral("--smoke-test"));

    const auto config = smoke ? QString{} : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/profiles-v1.json");
    wb::Window window(config);
    window.show();
    if (smoke)
    {
        QTimer::singleShot(0, &window, [&] {
            app.exit(window.isVisible() && window.session_count() == 0
                && window.statusBar()->currentMessage() == QStringLiteral("Disconnected") ? 0 : 1);
        });
    }
    return app.exec();
}
