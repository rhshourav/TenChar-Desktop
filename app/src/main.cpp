#include <QAccessible>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QLinearGradient>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMenu>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QSystemTrayIcon>
#include <QTimer>
#include <cstdlib>
#include <functional>

#include "app_controller.hpp"
#include "platform.hpp"
#include "qt_transport.hpp"
#include "segmenter.hpp"
#include "tc/random.hpp"
#include "tc/session.hpp"

namespace {
constexpr const char* AppVersion = "0.1.0";

/// The mark: a mint-to-lavender disc with a T and a C. (The phone's logo image is not part of this repository.)
QIcon makeIcon() {
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, size, size);
        g.setColorAt(0, QColor("#84F5B7"));
        g.setColorAt(1, QColor("#B491FE"));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(1, 1, size - 2, size - 2));
        p.setPen(QColor("#13141C"));
        QFont f("Inter");
        f.setBold(true);
        f.setPixelSize(static_cast<int>(size * 0.42));
        p.setFont(f);
        p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, "TC");
        icon.addPixmap(pm);
    }
    return icon;
}
}  // namespace

int main(int argc, char** argv) {
    platform::hardenProcess();   // no core files

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    app.setOrganizationName("TenChar");
    app.setApplicationName("TenChar");
    app.setApplicationVersion(AppVersion);
    app.setQuitOnLastWindowClosed(false);   // closing the window hides to the tray

    // Inter for Latin text, Noto Sans Bengali for Bangla: bundled, so the look and the shaping match on every system.
    for (const char* f : {"Inter-Regular", "Inter-Medium", "Inter-SemiBold", "Inter-Bold", "NotoSansBengali-Regular", "NotoSansBengali-Bold"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/%1.ttf").arg(f));
    QFont::insertSubstitution("Inter", "Noto Sans Bengali");
    QFont base("Inter");
    base.setPixelSize(16);
    app.setFont(base);
    const QIcon icon = makeIcon();
    app.setWindowIcon(icon);

    // Data lives in the user's app-data folder, readable only by that user (the Vault tightens the directory).
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/TenChar";
    QDir().mkpath(dataDir);

    // One running copy only: a second start brings the first to the front.
    const QString raiseName = QStringLiteral("tenchar-raise-") + QString::number(qHash(dataDir));
    QLockFile lock(dataDir + "/app.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100)) {
        QLocalSocket s;
        s.connectToServer(raiseName);
        if (s.waitForConnected(500)) { s.write("raise"); s.waitForBytesWritten(500); }
        return 0;
    }

    QString previewName, grabPath, forceLang, sizeSpec;
#ifdef TC_DEV_TOOLS   // screenshots with sample data; not part of release builds
    const QStringList args = app.arguments();
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == "--preview") previewName = args[i + 1];
        if (args[i] == "--grab") grabPath = args[i + 1];
        if (args[i] == "--lang") forceLang = args[i + 1];
        if (args[i] == "--size") sizeSpec = args[i + 1];
    }
#endif

    QtTransport transport(QStringLiteral("TenChar-desktop/") + AppVersion);
    tc::Engine::Deps deps;
#ifdef _WIN32
    deps.dataDir = std::filesystem::path(dataDir.toStdWString());   // not the ANSI code page: user names can be Bangla
#else
    deps.dataDir = std::filesystem::path(dataDir.toStdString());
#endif
    deps.transport = &transport;
    deps.rng = &tc::systemRandom();
    deps.segmenter = qtSegmenter();
    tc::Engine engine(std::move(deps));
    AppController ui(engine);
    if (!forceLang.isEmpty()) ui.setLanguage(forceLang);
    if (!previewName.isEmpty()) ui.preview(previewName);

    QLocalServer raiseServer;
    QLocalServer::removeServer(raiseName);
    raiseServer.listen(raiseName);
    QObject::connect(&raiseServer, &QLocalServer::newConnection, &ui, [&] {
        while (auto* c = raiseServer.nextPendingConnection()) c->deleteLater();
        emit ui.showRequested();
    });

    QQmlApplicationEngine qml;
    qml.rootContext()->setContextProperty("ui", &ui);
    qml.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (qml.rootObjects().isEmpty()) return 1;
    auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
    if (!window) return 1;
    if (!sizeSpec.isEmpty()) {
        const auto parts = sizeSpec.split('x');
        if (parts.size() == 2) window->resize(parts[0].toInt(), parts[1].toInt());
    }

    QSystemTrayIcon tray(icon);
    QMenu trayMenu;
    if (QSystemTrayIcon::isSystemTrayAvailable() && previewName.isEmpty()) {
        const auto s = [&](const char* k) { return ui.strings().value(k).toString(); };
        trayMenu.addAction(s("tray_show"), [&] { emit ui.showRequested(); });
        trayMenu.addAction(s("tray_lock"), [&] { ui.lock(); });
        trayMenu.addAction(s("tray_quit"), [&] { ui.quit(); });
        tray.setContextMenu(&trayMenu);
        tray.setToolTip("TenChar");   // never a count, text or sender
        QObject::connect(&tray, &QSystemTrayIcon::activated, &ui, [&](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) emit ui.showRequested();
        });
        tray.show();
        ui.setTray(&tray);
    }
    ui.setWindow(window);

#ifdef TC_DEV_TOOLS
    // --wheel DY and --click X,Y inject input before the screenshot, to test scrolling and clicking without a person.
    {
        const QStringList a = app.arguments();
        const int w = a.indexOf("--wheel"), c = a.indexOf("--click");
        if (w >= 0 && w + 1 < a.size()) {
            const int dy = a.at(w + 1).toInt();
            QTimer::singleShot(1000, &app, [window, dy] {
                for (int i = 0; i < 6; ++i) {
                    QWheelEvent ev(QPointF(200, 300), QPointF(200, 300), QPoint(), QPoint(0, dy), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                    QCoreApplication::sendEvent(window, &ev);
                }
            });
        }
        const int k = a.indexOf("--keys");
        if (k >= 0 && k + 1 < a.size()) {
            const QString seq = a.at(k + 1);
            QTimer::singleShot(900, &app, [window, seq] {
                auto send = [window](int key, const QString& text) {
                    QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, text);
                    QCoreApplication::sendEvent(window, &down);
                    QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier, text);
                    QCoreApplication::sendEvent(window, &up);
                };
                for (int i = 0; i < seq.size(); ++i) {
                    if (seq[i] == '~' && i + 1 < seq.size()) {
                        const QChar c = seq[++i];
                        if (c == 'T') send(Qt::Key_Tab, QString());
                        else if (c == 'E') send(Qt::Key_Return, QStringLiteral("\r"));
                    } else {
                        send(seq[i].toUpper().unicode(), QString(seq[i]));
                    }
                }
            });
        }
        if (c >= 0 && c + 1 < a.size()) {
            const QStringList xy = a.at(c + 1).split(',');
            QTimer::singleShot(700, &app, [window, xy] {
                const QPointF p(xy.value(0).toDouble(), xy.value(1).toDouble());
                QMouseEvent press(QEvent::MouseButtonPress, p, p, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, p, p, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &release);
            });
        }
    }
#endif
    if (!grabPath.isEmpty()) {
        int grabAfter = 1500;
        const int d = app.arguments().indexOf("--delay");
        if (d >= 0 && d + 1 < app.arguments().size()) grabAfter = app.arguments().at(d + 1).toInt();
        QTimer::singleShot(grabAfter, &app, [&] {
            window->grabWindow().save(grabPath);
            QCoreApplication::quit();
        });
    }

#ifdef TC_DEV_TOOLS
    // --a11y-dump FILE: write what a screen reader would be given for the current screen. A file, not the console,
    // because a Windows GUI program has no console to print to.
    const int dumpAt = app.arguments().indexOf("--a11y-dump");
    if (dumpAt >= 0 && dumpAt + 1 < app.arguments().size()) {
        const QString dumpPath = app.arguments().at(dumpAt + 1);
        QTimer::singleShot(1200, &app, [&app, window, dumpPath] {
            QAccessible::setActive(true);
            QStringList lines;
            std::function<void(QAccessibleInterface*, int)> walk = [&](QAccessibleInterface* i, int depth) {
                if (!i) return;
                const QString name = i->text(QAccessible::Name);
                if (!i->state().invisible && !i->state().invalid && (!name.isEmpty() || i->role() == QAccessible::Button))
                    lines << QString(depth * 2, ' ') + "role=" + QString::number(i->role()) + " name=\"" + name + "\"";
                for (int c = 0; c < i->childCount(); ++c) walk(i->child(c), depth + 1);
            };
            walk(QAccessible::queryAccessibleInterface(window), 0);
            QFile out(dumpPath);
            if (out.open(QIODevice::WriteOnly | QIODevice::Text)) out.write(lines.join('\n').toUtf8());
            QCoreApplication::quit();
        });
    }
#endif
    const int rc = app.exec();
    engine.lock();   // wipe keys and messages from memory
    // Leave without running destructors: relay worker threads may still be inside a network call, and the
    // process ending is what releases everything anyway.
    std::_Exit(rc);
}
