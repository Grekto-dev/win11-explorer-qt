#pragma once
#include <QString>

class QWidget;
class QMenu;

namespace PCManFM {
// State styling is immediate: the reference cadence cannot resolve sub-frame timing.
class ExplorerAnimation final {
public:
    // True when the application palette/colour scheme is dark (decided when widgets are styled).
    static bool darkMode();
    // Maps the measured light colours of a stylesheet to the dark counterpart; identity in light mode.
    static QString themed(const QString& lightStyleSheet, bool dark = darkMode());
    static QString menuStyleSheet();
    static QString buttonStyleSheet();
    static void styleMenu(QMenu* menu);
    static void styleButton(QWidget* button);
};
}
