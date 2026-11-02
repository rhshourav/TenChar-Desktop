#include "app_controller.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeData>
#include <QQuickWindow>
#include <QSystemTrayIcon>

#include "platform.hpp"
#include "tc/pairing.hpp"

using tc::Screen;

namespace {
const char* screenName(Screen s) {
    switch (s) {
        case Screen::Setup: return "setup";
        case Screen::Unlock: return "unlock";
        case Screen::Pair: return "pair";
        case Screen::EnterCode: return "entercode";
        case Screen::Waiting: return "waiting";
        case Screen::Verify: return "verify";
        case Screen::Chat: return "chat";
        case Screen::Settings: return "settings";
        case Screen::About: return "about";
        case Screen::KeysReset: return "keysreset";
    }
    return "unlock";
}
constexpr int ClipboardClearMs = 60'000;
}  // namespace

AppController::AppController(tc::Engine& engine, QObject* parent)
    : QObject(parent), engine_(engine), prefs_(QStringLiteral("TenChar"), QStringLiteral("TenChar")) {
    language_ = prefs_.value("language", "system").toString();
    reducedTransparency_ = prefs_.value("reducedTransparency", false).toBool();
    reducedMotion_ = prefs_.value("reducedMotion", false).toBool();
    highContrast_ = prefs_.value("highContrast", false).toBool();
    notifications_ = prefs_.value("notifications", true).toBool();
    loadStrings();

    lockTimer_.setSingleShot(true);
    inactivity_.setSingleShot(true);
    clipboardTimer_.setSingleShot(true);
    connect(&lockTimer_, &QTimer::timeout, this, [this] { engine_.lock(); });
    connect(&inactivity_, &QTimer::timeout, this, &AppController::inactivityTimeout);
    connect(&clipboardTimer_, &QTimer::timeout, this, [this] {
        auto* cb = QGuiApplication::clipboard();
        if (!copiedCode_.isEmpty() && cb->text() == copiedCode_) cb->clear();
        copiedCode_.clear();
    });
    // The engine calls this from its own threads; hop to the GUI thread.
    engine_.setOnChange([this] { QMetaObject::invokeMethod(this, &AppController::refresh, Qt::QueuedConnection); });
    apply(engine_.snapshot());
}

QString AppController::screen() const { return screenName(snap_.screen); }
QString AppController::notice() const {
    switch (snap_.notice) {
        case tc::Notice::Mismatch: return "mismatch";
        case tc::Notice::Forgotten: return "forgotten";
        case tc::Notice::Timeout: return "timeout";
        case tc::Notice::Unreachable: return "unreachable";
        case tc::Notice::Tampered: return "tampered";
        case tc::Notice::Full: return "full";
        case tc::Notice::Storage: return "storage";
        case tc::Notice::None: break;
    }
    return {};
}
QString AppController::setupError() const {
    switch (snap_.setupError) {
        case tc::SetupError::TooShort: return "short";
        case tc::SetupError::Mismatch: return "mismatch";
        case tc::SetupError::Failed: return "failed";
        case tc::SetupError::None: break;
    }
    return {};
}
QString AppController::connection() const {
    return snap_.connection == tc::Connection::Gone ? "gone" : snap_.connection == tc::Connection::Offline ? "offline" : "online";
}
QString AppController::autoLock() const {
    return snap_.settings.autoLock == tc::AutoLock::Immediately ? "immediately" : snap_.settings.autoLock == tc::AutoLock::Seconds30 ? "30s" : "5m";
}
bool AppController::bangla() const {
    return language_ == "bn" || (language_ == "system" && QLocale::system().language() == QLocale::Bengali);
}

void AppController::loadStrings() {
    auto load = [](const char* path) {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
    };
    strings_ = load(":/strings/en.json");
    if (bangla()) {
        const QVariantMap bn = load(":/strings/bn.json");
        for (auto it = bn.begin(); it != bn.end(); ++it) strings_[it.key()] = it.value();
    }
}

void AppController::refresh() {
    if (!preview_) apply(engine_.snapshot());
}

void AppController::apply(const tc::Snapshot& s) {
    int incoming = 0;
    for (const auto& m : s.messages) incoming += m.mine ? 0 : 1;
    const bool unlockedNow = s.screen == Screen::Chat || s.screen == Screen::Settings || s.screen == Screen::About;
    if (incoming > incoming_ && incoming_ > 0 && unlockedNow && notifications_ && tray_ && window_ && !window_->isActive())
        tray_->showMessage(QStringLiteral("TenChar"), strings_.value("tray_new_message").toString());  // never sender or content
    incoming_ = incoming;
    const bool screenChanged = snap_.screen != s.screen;
    snap_ = s;
    model_.update(s.messages);
    emit changed();
    if (screenChanged) armInactivity();
}

bool AppController::sensitiveScreen() const { return snap_.screen != Screen::Setup && snap_.screen != Screen::Unlock && snap_.screen != Screen::KeysReset; }

void AppController::armInactivity() {
    inactivity_.stop();
    const auto ms = tc::autoLockMillis(snap_.settings.autoLock);
    // "Immediately" means on leaving the window; while the window is in front the longest delay counts as inactivity.
    if (sensitiveScreen() && window_ && window_->isActive()) inactivity_.start(static_cast<int>(ms == 0 ? tc::AutoLockLongMs : ms));
}
void AppController::activity() { if (sensitiveScreen()) armInactivity(); }
void AppController::inactivityTimeout() { if (window_ && window_->isActive() && sensitiveScreen()) engine_.lock(); }

void AppController::setWindow(QQuickWindow* window) {
    window_ = window;
    if (!window) return;
    connect(window, &QQuickWindow::activeChanged, this, &AppController::onActiveChanged);
    platform::excludeFromCapture(window);
    onActiveChanged();
}
void AppController::setTray(QSystemTrayIcon* tray) { tray_ = tray; }

void AppController::onActiveChanged() {
    const bool active = window_ && window_->isActive();
    engine_.setForeground(active);
    if (active) {
        lockTimer_.stop();
        if (left_ && sensitiveScreen()) {   // a frozen process may have missed the timer
            const auto ms = tc::autoLockMillis(snap_.settings.autoLock);
            if (leftAt_.hasExpired(ms)) engine_.lock();
        }
        left_ = false;
        armInactivity();
    } else if (sensitiveScreen()) {
        inactivity_.stop();
        const auto ms = tc::autoLockMillis(snap_.settings.autoLock);
        left_ = true;
        leftAt_.start();
        if (ms == 0) engine_.lock();
        else lockTimer_.start(static_cast<int>(ms));
    }
}

void AppController::hideToTray() {
    engine_.lock();   // closing the window locks, and nothing talks to a relay while hidden
    if (window_) window_->hide();
}
void AppController::quit() { QCoreApplication::quit(); }

void AppController::copyCode() {
    const QString text = code();
    if (text.isEmpty()) return;
    auto* data = new QMimeData;
    data->setText(text);
    platform::markClipboardSensitive(data);
    QGuiApplication::clipboard()->setMimeData(data);
    copiedCode_ = text;
    clipboardTimer_.start(ClipboardClearMs);   // cleared after 60 seconds if unchanged
}

// ---- forwarding ------------------------------------------------------------------------------------

void AppController::finishSetup(const QString& pass, const QString& confirm) { engine_.finishSetup(pass.toStdString(), confirm.toStdString()); }
void AppController::unlock(const QString& pass) { engine_.unlock(pass.toStdString()); }
void AppController::lock() { engine_.lock(); }
void AppController::pairAgain() { engine_.pairAgain(); }
bool AppController::saveRelay(const QString& v) { return engine_.saveRelay(v.toStdString()); }
void AppController::resetRelay() { engine_.resetRelay(); }
bool AppController::saveFallback(const QString& v) { return engine_.saveFallback(v.toStdString()); }
void AppController::resetFallback() { engine_.resetFallback(); }
void AppController::enableFallback(bool on) { engine_.enableFallback(on); }
void AppController::chooseAutoLock(const QString& w) {
    engine_.chooseAutoLock(w == "immediately" ? tc::AutoLock::Immediately : w == "5m" ? tc::AutoLock::Minutes5 : tc::AutoLock::Seconds30);
}
void AppController::back() { engine_.back(); }
void AppController::open(const QString& s) {
    engine_.open(s == "settings" ? Screen::Settings : s == "about" ? Screen::About : Screen::Chat);
}
void AppController::startWaiting() { engine_.startWaiting(); }
void AppController::openEnterCode() { engine_.openEnterCode(); }
void AppController::onEntry(const QString& t) { engine_.onEntry(t.toStdString()); }
void AppController::join() { engine_.join(); }
void AppController::cancelWaiting() { engine_.cancelWaiting(); }
void AppController::verifyMatches() { engine_.verifyMatches(); }
void AppController::verifyMismatch() { engine_.verifyMismatch(); }
void AppController::forgetConversation() { engine_.forgetConversation(); }
void AppController::send(const QString& t) { engine_.send(t.toStdString()); }
QString AppController::clamp(const QString& d) const { return QString::fromStdString(engine_.clamp(d.toStdString())); }
int AppController::graphemeCount(const QString& t) const { return static_cast<int>(engine_.graphemeCount(t.toStdString())); }
QString AppController::formatCode(const QString& t) const { return QString::fromStdString(tc::pairing::formatPartial(t.toStdString())); }
int AppController::utf8Length(const QString& t) const { return t.toUtf8().size(); }

void AppController::setLanguage(const QString& lang) {
    language_ = lang;
    prefs_.setValue("language", lang);
    loadStrings();
    emit stringsChanged();
}
#define TC_PREF(Name, member, key)                       \
    void AppController::Name(bool on) {                  \
        member = on;                                     \
        prefs_.setValue(key, on);                        \
        emit prefsChanged();                             \
    }
TC_PREF(setReducedTransparency, reducedTransparency_, "reducedTransparency")
TC_PREF(setReducedMotion, reducedMotion_, "reducedMotion")
TC_PREF(setHighContrast, highContrast_, "highContrast")
TC_PREF(setNotifications, notifications_, "notifications")

// ---- developer preview with sample data ----------------------------------------------------------------

void AppController::preview(const QString& name) {
    preview_ = true;
    tc::Snapshot s;
    s.settings.setupDone = true;
    auto msg = [](std::int64_t id, const char* text, bool mine, tc::Delivery d) { return tc::ChatMessage{id, text, mine, d}; };
    if (name == "setup") { s.screen = Screen::Setup; s.firstRun = true; }
    else if (name == "unlock") { s.screen = Screen::Unlock; }
    else if (name == "unlockfail") { s.screen = Screen::Unlock; s.unlockFailed = true; }
    else if (name == "pair") { s.screen = Screen::Pair; }
    else if (name == "pairnotice") { s.screen = Screen::Pair; s.notice = tc::Notice::Mismatch; }
    else if (name == "entercode") { s.screen = Screen::EnterCode; s.entry = "ABCD-EFGH-JK"; }
    else if (name == "waiting") { s.screen = Screen::Waiting; s.code = "ABCD-EFGH-JKLM-NPQR"; }
    else if (name == "verify") { s.screen = Screen::Verify; s.verification = "9617 7894"; }
    else if (name == "settings") { s.screen = Screen::Settings; s.verification = "9617 7894"; }
    else if (name == "about") { s.screen = Screen::About; }
    else if (name == "keysreset") { s.screen = Screen::KeysReset; }
    else if (name == "chatempty") { s.screen = Screen::Chat; }
    else {   // "chat", "chatbn", "chatoffline"
        s.screen = Screen::Chat;
        s.messages = {msg(1, "hi", false, tc::Delivery::None), msg(2, "on my way", true, tc::Delivery::Sent),
                      msg(3, "ok!", false, tc::Delivery::None), msg(4, "\xf0\x9f\x98\x80 see you", true, tc::Delivery::Queued)};
        if (name == "chatbn")
            s.messages = {msg(1, "\xe0\xa6\xb9\xe0\xa7\x8d\xe0\xa6\xaf\xe0\xa6\xbe\xe0\xa6\xb2\xe0\xa7\x8b", false, tc::Delivery::None),
                          msg(2, "\xe0\xa6\x86\xe0\xa6\xae\xe0\xa6\xbf \xe0\xa6\x86\xe0\xa6\xb8\xe0\xa6\x9b\xe0\xa6\xbf", true, tc::Delivery::Sent),
                          msg(3, "\xe0\xa6\xa0\xe0\xa6\xbf\xe0\xa6\x95 \xe0\xa6\x86\xe0\xa6\x9b\xe0\xa7\x87", false, tc::Delivery::None)};
        if (name == "chatoffline") s.connection = tc::Connection::Offline;
    }
    apply(s);
}
