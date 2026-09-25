#include "theme.h"

#include <QGuiApplication>
#include <QStyleHints>
#include <QSettings>

namespace {
const char *MODE_SETTINGS_KEY = "themeMode";
}

namespace Theme {

void setMode(Mode mode)
{
    QSettings settings;
    QStyleHints *hints = QGuiApplication::styleHints();

    switch (mode)
    {
    case Mode::Light:
        settings.setValue(MODE_SETTINGS_KEY, "light");
        hints -> setColorScheme(Qt::ColorScheme::Light);
        break;
    case Mode::Dark:
        settings.setValue(MODE_SETTINGS_KEY, "dark");
        hints -> setColorScheme(Qt::ColorScheme::Dark);
        break;
    case Mode::System:
    default:
        settings.setValue(MODE_SETTINGS_KEY, "system");
        hints -> unsetColorScheme(); // reverts to tracking the OS's own appearance
        break;
    }
}

Mode mode()
{
    QString stored = QSettings().value(MODE_SETTINGS_KEY, "system").toString();

    if (stored == "light")
        return Mode::Light;
    if (stored == "dark")
        return Mode::Dark;
    return Mode::System;
}

bool isDark()
{
    Qt::ColorScheme scheme = QGuiApplication::styleHints() -> colorScheme();
    return scheme != Qt::ColorScheme::Light;
}

} // namespace Theme
