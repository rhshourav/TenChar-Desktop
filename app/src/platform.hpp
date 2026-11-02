#pragma once
// The few native calls the app needs. Each is best effort and reports whether it did anything.
class QWindow;
class QMimeData;

namespace platform {
/// Excludes the window from screen capture: Windows display affinity (Windows 10 version 2004 or later),
/// macOS window sharing type none. Linux has no dependable method and returns false.
bool excludeFromCapture(QWindow* window);
/// No core files, and (where the OS allows) no ptrace by other processes.
void hardenProcess();
/// Marks clipboard content as secret so clipboard history and managers skip it.
void markClipboardSensitive(QMimeData* data);
}  // namespace platform
