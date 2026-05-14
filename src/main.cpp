#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QFileInfo>

#include "accountmanager.h"
#include "eventmodel.h"
#include "feedmanager.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    bool windowed = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--windowed" || arg == "-w") {
            windowed = true;
            break;
        }
    }

    AccountManager accountManager;
    EventModel model;
    FeedManager feedManager(&model);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("accountManager", &accountManager);
    engine.rootContext()->setContextProperty("eventModel", &model);
    engine.rootContext()->setContextProperty("feedManager", &feedManager);
    engine.rootContext()->setContextProperty("windowedMode", windowed);

    // Resolve QML path: check snap location, installed location, then source location
    QString qmlPath;
    const char* snap = qgetenv("SNAP");
    
    if (snap && strlen(snap) > 0) {
        // Running as a snap
        qmlPath = QString::fromLocal8Bit(snap) + "/share/CalDisplay/qml";
    } else {
        // Check installed location
        QString appDir = QCoreApplication::applicationDirPath();
        QFileInfo installedPath(appDir + "/../share/CalDisplay/qml/main.qml");
        if (installedPath.exists()) {
            qmlPath = appDir + "/../share/CalDisplay/qml";
        } else {
            // Fall back to source location (development)
            qmlPath = QStringLiteral(APP_QML_PATH);
        }
    }
    
    const QString rootQml = qmlPath + "/main.qml";
    engine.load(QUrl::fromLocalFile(rootQml));

    if (engine.rootObjects().isEmpty()) {
        qWarning() << "Failed to load QML from:" << rootQml;
        return -1;
    }

    if (windowed) {
        QQuickWindow* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (window) {
            window->setWidth(1024);
            window->setHeight(768);
            window->setVisibility(QWindow::Windowed);
        }
    }

    return app.exec();
}
