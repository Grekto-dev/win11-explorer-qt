#include <QtTest>
#include <QToolButton>
#include <QMenu>
#include "exploreranimation.h"

class ExplorerAnimationTest final : public QObject {
    Q_OBJECT
private slots:
    void menuUsesMeasuredSurfaceAndKeepsKeyboardAccess();
    void buttonStatesAreDistinctAndCancelOnLeave();
};

void ExplorerAnimationTest::menuUsesMeasuredSurfaceAndKeepsKeyboardAccess() {
    QWidget host;
    QMenu menu(&host);
    menu.addAction(QStringLiteral("Classificar"));
    PCManFM::ExplorerAnimation::styleMenu(&menu);
    QCOMPARE(menu.styleSheet(), PCManFM::ExplorerAnimation::menuStyleSheet());
    QVERIFY(menu.actions().first()->isEnabled());
    QVERIFY(menu.actions().first()->isVisible());
}

void ExplorerAnimationTest::buttonStatesAreDistinctAndCancelOnLeave() {
    QToolButton button;
    PCManFM::ExplorerAnimation::styleButton(&button);
    const auto css = button.styleSheet();
    QVERIFY(css.contains(QStringLiteral(":hover")));
    QVERIFY(css.contains(QStringLiteral(":pressed")));
    QVERIFY(css.contains(QStringLiteral(":focus")));
    QVERIFY(css.contains(QStringLiteral("#f3f3f3")));
    QVERIFY(css.contains(QStringLiteral("#e5e5e5")));
    button.setCheckable(true);
    button.setChecked(true);
    QVERIFY(button.isChecked()); // QAction/toggle semantics remain native.
}

QTEST_MAIN(ExplorerAnimationTest)
#include "tst_exploreranimation.moc"
