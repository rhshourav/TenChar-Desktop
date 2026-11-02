#pragma once
// The bridge between the Qt Quick interface and the engine. The interface reads properties and calls
// invokable methods; it never sees a key, a token or a ratchet. Everything it can show comes from a Snapshot.
#include <QElapsedTimer>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

#include "message_model.hpp"
#include "tc/session.hpp"

class QQuickWindow;
class QSystemTrayIcon;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString screen READ screen NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(QString code READ code NOTIFY changed)
    Q_PROPERTY(QString entry READ entry NOTIFY changed)
    Q_PROPERTY(bool joining READ joining NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool unlockFailed READ unlockFailed NOTIFY changed)
    Q_PROPERTY(QString setupError READ setupError NOTIFY changed)
    Q_PROPERTY(bool firstRun READ firstRun NOTIFY changed)
    Q_PROPERTY(QString connection READ connection NOTIFY changed)
    Q_PROPERTY(QString verification READ verification NOTIFY changed)
    Q_PROPERTY(QString relayUrl READ relayUrl NOTIFY changed)
    Q_PROPERTY(QString fallbackUrl READ fallbackUrl NOTIFY changed)
    Q_PROPERTY(bool fallbackEnabled READ fallbackEnabled NOTIFY changed)
    Q_PROPERTY(QString autoLock READ autoLock NOTIFY changed)
    Q_PROPERTY(QObject* messages READ messages CONSTANT)
    Q_PROPERTY(QVariantMap strings READ strings NOTIFY stringsChanged)
    Q_PROPERTY(QString language READ language NOTIFY stringsChanged)
    Q_PROPERTY(bool bangla READ bangla NOTIFY stringsChanged)
    Q_PROPERTY(bool reducedTransparency READ reducedTransparency NOTIFY prefsChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY prefsChanged)
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY prefsChanged)
    Q_PROPERTY(bool notifications READ notifications NOTIFY prefsChanged)
    Q_PROPERTY(int maxChars READ maxChars CONSTANT)
    Q_PROPERTY(int maxBytes READ maxBytes CONSTANT)

public:
    AppController(tc::Engine& engine, QObject* parent = nullptr);

    QString screen() const;
    QString notice() const;
    QString code() const { return QString::fromStdString(snap_.code); }
    QString entry() const { return QString::fromStdString(snap_.entry); }
    bool joining() const { return snap_.joining; }
    bool busy() const { return snap_.busy; }
    bool unlockFailed() const { return snap_.unlockFailed; }
    QString setupError() const;
    bool firstRun() const { return snap_.firstRun; }
    QString connection() const;
    QString verification() const { return QString::fromStdString(snap_.verification); }
    QString relayUrl() const { return QString::fromStdString(snap_.settings.relayUrl); }
    QString fallbackUrl() const { return QString::fromStdString(snap_.settings.fallbackUrl); }
    bool fallbackEnabled() const { return snap_.settings.fallbackEnabled; }
    QString autoLock() const;
    QObject* messages() { return &model_; }
    QVariantMap strings() const { return strings_; }
    QString language() const { return language_; }
    bool bangla() const;
    bool reducedTransparency() const { return reducedTransparency_; }
    bool reducedMotion() const { return reducedMotion_; }
    bool highContrast() const { return highContrast_; }
    bool notifications() const { return notifications_; }
    int maxChars() const { return tc::MaxChars; }
    int maxBytes() const { return tc::MaxUtf8Bytes; }

    void setWindow(QQuickWindow* window);
    void setTray(QSystemTrayIcon* tray);
    /// Developer tool: show a screen with sample data instead of the live engine.
    void preview(const QString& name);

    // engine, forwarded
    Q_INVOKABLE void finishSetup(const QString& pass, const QString& confirm);
    Q_INVOKABLE void unlock(const QString& pass);
    Q_INVOKABLE void lock();
    Q_INVOKABLE void pairAgain();
    Q_INVOKABLE bool saveRelay(const QString& v);
    Q_INVOKABLE void resetRelay();
    Q_INVOKABLE bool saveFallback(const QString& v);
    Q_INVOKABLE void resetFallback();
    Q_INVOKABLE void enableFallback(bool on);
    Q_INVOKABLE void chooseAutoLock(const QString& which);
    Q_INVOKABLE void back();
    Q_INVOKABLE void open(const QString& screen);
    Q_INVOKABLE void startWaiting();
    Q_INVOKABLE void openEnterCode();
    Q_INVOKABLE void onEntry(const QString& text);
    Q_INVOKABLE void join();
    Q_INVOKABLE void cancelWaiting();
    Q_INVOKABLE void verifyMatches();
    Q_INVOKABLE void verifyMismatch();
    Q_INVOKABLE void forgetConversation();
    Q_INVOKABLE void send(const QString& text);
    // text helpers (the same rules the engine uses)
    Q_INVOKABLE QString clamp(const QString& draft) const;
    Q_INVOKABLE int graphemeCount(const QString& text) const;
    Q_INVOKABLE int utf8Length(const QString& text) const;
    Q_INVOKABLE QString formatCode(const QString& text) const;
    // interface preferences (not secret, kept in QSettings)
    Q_INVOKABLE void setLanguage(const QString& lang);
    Q_INVOKABLE void setReducedTransparency(bool on);
    Q_INVOKABLE void setReducedMotion(bool on);
    Q_INVOKABLE void setHighContrast(bool on);
    Q_INVOKABLE void setNotifications(bool on);
    // window and activity
    Q_INVOKABLE void activity();
    Q_INVOKABLE void hideToTray();
    Q_INVOKABLE void copyCode();
    Q_INVOKABLE void quit();

signals:
    void changed();
    void stringsChanged();
    void prefsChanged();
    void showRequested();

private slots:
    void refresh();
    void onActiveChanged();
    void inactivityTimeout();

private:
    void apply(const tc::Snapshot& s);
    void loadStrings();
    bool sensitiveScreen() const;
    void armInactivity();

    tc::Engine& engine_;
    tc::Snapshot snap_;
    MessageModel model_;
    QSettings prefs_;
    QVariantMap strings_;
    QString language_;  // "system", "en" or "bn"
    bool reducedTransparency_ = false, reducedMotion_ = false, highContrast_ = false, notifications_ = true;
    QQuickWindow* window_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QTimer lockTimer_, inactivity_, clipboardTimer_;
    QElapsedTimer leftAt_;
    bool left_ = false;
    int incoming_ = 0;
    bool preview_ = false;
    QString copiedCode_;
};
