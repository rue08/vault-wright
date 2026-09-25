#ifndef THEME_H
#define THEME_H

// Centralizes the app's light/dark theme mode -- both the native Qt UI
// (mainwindow.cpp) and each embedded Monaco tab (monacoeditor.cpp) read the
// same effective scheme from here, so a mode change made through the
// View > Theme menu reaches all of them without either side needing to know
// about the other.
//
// The actual light/dark switch is delegated to Qt's own QStyleHints color
// scheme (Qt 6.8+) rather than a hand-rolled QPalette -- that's what drives
// native widget chrome automatically, and its colorSchemeChanged signal is
// the single trigger both mainwindow.cpp and monacoeditor.cpp hook for a
// live update, whether it fired because of an explicit Light/Dark override
// here or because the OS's own appearance changed while in System mode.
namespace Theme {

enum class Mode { System, Light, Dark };

// Persists `mode` to QSettings and applies it via QStyleHints -- System maps
// to QStyleHints::unsetColorScheme() (go back to following the OS), Light/
// Dark to an explicit QStyleHints::setColorScheme() override. Call once at
// startup with mode() to reapply the saved preference (QStyleHints's
// override doesn't itself persist across process restarts) and again
// whenever the user picks a different option from the View > Theme menu.
void setMode(Mode mode);

// The mode last passed to setMode(), restored from QSettings on first call
// this run. Defaults to System.
Mode mode();

// The scheme actually in effect right now, folding in the OS's current
// appearance when mode() == System. Qt::ColorScheme::Unknown (a platform
// that can't report one) is treated as dark, matching the app's original
// dark-only look.
bool isDark();

} // namespace Theme

#endif // THEME_H
