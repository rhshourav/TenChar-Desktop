// macOS: exclude the window from screen capture. Compiled only on macOS. NOT built or run in the sandbox this
// was written in: it needs a Mac to verify (and some capture paths on newer macOS versions ignore the setting).
#include <QWindow>

#include "platform.hpp"

#import <AppKit/AppKit.h>

namespace platform {
bool excludeFromCapture(QWindow* window) {
    if (!window) return false;
    NSView* view = reinterpret_cast<NSView*>(window->winId());
    NSWindow* nsWindow = view.window;
    if (!nsWindow) return false;
    nsWindow.sharingType = NSWindowSharingNone;
    return true;
}
}  // namespace platform
