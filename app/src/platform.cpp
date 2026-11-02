#include "platform.hpp"

#include <QMimeData>
#include <QWindow>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <sys/prctl.h>
#include <sys/resource.h>
#elif defined(Q_OS_MACOS)
#include <sys/resource.h>
#endif

namespace platform {

#if !defined(Q_OS_MACOS)
bool excludeFromCapture(QWindow* window) {
#if defined(Q_OS_WIN)
    if (!window) return false;
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    constexpr DWORD ExcludeFromCapture = 0x11;  // WDA_EXCLUDEFROMCAPTURE, Windows 10 version 2004+
    if (SetWindowDisplayAffinity(hwnd, ExcludeFromCapture)) return true;
    return SetWindowDisplayAffinity(hwnd, 1 /* WDA_MONITOR: a black window in captures */) != 0;
#else
    (void)window;
    return false;
#endif
}
#endif  // macOS version lives in platform_mac.mm

void hardenProcess() {
#if defined(Q_OS_LINUX)
    prctl(PR_SET_DUMPABLE, 0);
    struct rlimit zero{0, 0};
    setrlimit(RLIMIT_CORE, &zero);
#elif defined(Q_OS_MACOS)
    struct rlimit zero{0, 0};
    setrlimit(RLIMIT_CORE, &zero);
#elif defined(Q_OS_WIN)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
}

void markClipboardSensitive(QMimeData* data) {
    const QByteArray zero(4, '\0');  // a DWORD 0
#if defined(Q_OS_WIN)
    data->setData(QStringLiteral("application/x-qt-windows-mime;value=\"ExcludeClipboardContentFromMonitorProcessing\""), zero);
    data->setData(QStringLiteral("application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\""), zero);
    data->setData(QStringLiteral("application/x-qt-windows-mime;value=\"CanUploadToCloudClipboard\""), zero);
#elif defined(Q_OS_MACOS)
    data->setData(QStringLiteral("org.nspasteboard.ConcealedType"), QByteArray("1"));
    data->setData(QStringLiteral("org.nspasteboard.TransientType"), QByteArray("1"));
#else
    data->setData(QStringLiteral("x-kde-passwordManagerHint"), QByteArray("secret"));
#endif
}

}  // namespace platform
