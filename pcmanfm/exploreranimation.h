#pragma once
#include <QString>

class QWidget;
class QMenu;

namespace PCManFM {
// State styling is immediate: the reference cadence cannot resolve sub-frame timing.
class ExplorerAnimation final {
public:
    static QString menuStyleSheet();
    static QString buttonStyleSheet();
    static void styleMenu(QMenu* menu);
    static void styleButton(QWidget* button);
};
}
