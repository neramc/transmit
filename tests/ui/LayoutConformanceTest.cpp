// Does the interface actually fit?
//
// The other UI test asks whether the QML resolves. This one asks whether what
// it produces is usable: nothing overlapping, nothing spilling out of its
// parent, no text cut off without an ellipsis to say so, no control too small
// to hit, and no page that needs to be scrolled sideways.
//
// None of that can be checked by looking at a screenshot, because a screenshot
// is one window size on one machine with one set of fonts installed. This
// walks the real item tree at every window size docs/design.md section 6 names,
// in both colour schemes, on every page - and it does not care what font the
// runner has, because it compares each item against its own measurements
// rather than against a picture taken somewhere else.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSet>
#include <QStringList>
#include <QTest>

#include <cmath>

namespace {

/// Every resolution docs/design.md section 6 requires the application to be
/// useful at. The window is given the whole of each; a real one has a title
/// bar and a task bar, but a window that fits the screen exactly is the harder
/// case and the one that finds the problems.
struct Resolution {
    const char* name;
    int width;
    int height;
};

constexpr Resolution kResolutions[] = {
    {"1280x720", 1280, 720},   {"1366x768", 1366, 768},   {"1440x900", 1440, 900},
    {"1920x1080", 1920, 1080}, {"2560x1440", 2560, 1440}, {"3840x2160", 3840, 2160},
};

constexpr const char* kPages[] = {"home", "export", "import", "report", "settings"};

/// Sub-pixel positions are ordinary in Qt Quick - a centred item in a
/// container of odd width sits on a half pixel - and none of the faults this
/// looks for are half a pixel wide.
constexpr qreal kTolerance = 1.0;

/// Section 33 requires every interactive element to be reachable and usable.
/// A control smaller than this is one somebody with an ordinary hand and an
/// ordinary mouse will miss.
constexpr qreal kMinimumControlSize = 16.0;

QString describe(const QQuickItem* item) {
    if (item == nullptr) {
        return QStringLiteral("(null)");
    }
    QString name = QString::fromLatin1(item->metaObject()->className());
    // QML types are generated as "Foo_QMLTYPE_12"; the suffix is noise.
    if (const qsizetype marker = name.indexOf(QStringLiteral("_QML")); marker > 0) {
        name.truncate(marker);
    }
    if (!item->objectName().isEmpty()) {
        name += QStringLiteral("[%1]").arg(item->objectName());
    }
    // The QML file it came from. Without it a failure names a class - and
    // "QQuickItem" is every plain Item in the interface, which is no help at
    // all when the point of the message is to say where to go and look.
    if (const QQmlContext* context = qmlContext(item); context != nullptr) {
        const QString file = context->baseUrl().fileName();
        if (!file.isEmpty()) {
            name += QStringLiteral(" (%1)").arg(file);
        }
    }
    return QStringLiteral("%1 at (%2,%3) %4x%5")
        .arg(name)
        .arg(item->x(), 0, 'f', 1)
        .arg(item->y(), 0, 'f', 1)
        .arg(item->width(), 0, 'f', 1)
        .arg(item->height(), 0, 'f', 1);
}

bool isLaidOut(const QQuickItem* item) {
    return item != nullptr && item->isVisible() && item->width() > 0.5 && item->height() > 0.5;
}

/// Whether this item is part of the interface as written, rather than one of
/// Qt's own.
///
/// Everything the application draws comes from a QML file and carries the
/// context it was created in. What does not: a Flickable's contentItem, and -
/// the reason this exists - a ListView delegate that has scrolled out and been
/// put back in the view's reuse pool. A pooled delegate is still a child of
/// the contentItem, still reports itself visible, and still carries the width
/// it had when it was last shown, which reads exactly like an item too wide
/// for the list it is in. It is not in the list at all.
///
/// Those items are still walked through, because the contentItem is how the
/// real delegates are reached. They are just not judged.
bool cameFromQml(const QQuickItem* item) {
    return qmlContext(item) != nullptr;
}

/// True for a Row, Column, RowLayout, GridLayout and so on - anything whose
/// job is to place its children so they do not collide. Matched on the type
/// name because the layout classes are not in a public header this test can
/// include.
bool arrangesItsChildren(const QQuickItem* item) {
    const QString name = QString::fromLatin1(item->metaObject()->className());
    return name.contains(QStringLiteral("QQuickRow")) ||
           name.contains(QStringLiteral("QQuickColumn")) ||
           name.contains(QStringLiteral("QQuickRowLayout")) ||
           name.contains(QStringLiteral("QQuickColumnLayout")) ||
           name.contains(QStringLiteral("QQuickGridLayout"));
}

bool isText(const QQuickItem* item) {
    return QString::fromLatin1(item->metaObject()->className())
        .contains(QStringLiteral("QQuickText"));
}

/// True only for a Flickable. Text has contentWidth too, so testing for that
/// property alone made every truncated label look like a scrollable region -
/// which both excused it from the overflow check and reported it under the
/// wrong rule. flickableDirection is the one that only a Flickable has.
bool isFlickable(const QQuickItem* item) {
    return item->property("flickableDirection").isValid() &&
           item->property("contentWidth").isValid();
}

/// A Flickable that is genuinely scrollable in a direction is allowed to hold
/// content larger than itself in that direction - that is what it is for.
bool scrollsHorizontally(const QQuickItem* item) {
    return isFlickable(item) &&
           item->property("contentWidth").toReal() > item->width() + kTolerance;
}

bool scrollsVertically(const QQuickItem* item) {
    return isFlickable(item) &&
           item->property("contentHeight").toReal() > item->height() + kTolerance;
}

}  // namespace

class LayoutConformanceTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void everyPageFitsAtEverySize_data();
    void everyPageFitsAtEverySize();

private:
    void showPage(const QString& page);
    void resizeTo(int width, int height);
    void setScheme(const QString& scheme);
    void setSidebarCollapsed(bool collapsed);
    void evaluate(const QString& expression);
    [[nodiscard]] QQuickWindow* window() const;
    [[nodiscard]] QQuickItem* contentRoot() const;

    /// Walks every laid-out descendant, calling `visit(item, parent)`.
    /// `descend` decides whether to go inside an item at all, which is how
    /// scrollable content is left out of the checks it is exempt from.
    template<typename Visit, typename Descend>
    static void walk(QQuickItem* item, QQuickItem* parent, const Visit& visit,
                     const Descend& descend);

    /// The five checks. Each appends to `problems` and says which page and
    /// which rule, so one failure names everything needed to find it.
    void checkOverflow(const QString& page, QStringList& problems) const;
    void checkOverlap(const QString& page, QStringList& problems) const;
    void checkTruncation(const QString& page, QStringList& problems) const;
    void checkControlSizes(const QString& page, QStringList& problems) const;
    void checkSidewaysScroll(const QString& page, QStringList& problems) const;

    std::unique_ptr<QQmlApplicationEngine> engine_;
};

void LayoutConformanceTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    engine_ = std::make_unique<QQmlApplicationEngine>();
    engine_->addImportPath(QStringLiteral(":/qt/qml"));
    engine_->load(QUrl(QStringLiteral("qrc:/qt/qml/Transmit/Main.qml")));
    QVERIFY2(!engine_->rootObjects().isEmpty(), "the main window did not load");

    // No animations. Pages cross-fade, so for the length of the fade both the
    // outgoing and the incoming page are visible and every measurement is
    // taken during a state the interface is only passing through. Waiting the
    // fade out would work and cost ten seconds a run; switching motion off is
    // exact, and layout does not depend on it.
    evaluate(QStringLiteral("AppController.reduceMotion = true"));
    QTest::qWait(60);

    // Every page is built once up front. The pages are behind lazy loaders, so
    // the first visit to each is the slow one and it does not belong inside a
    // measurement.
    for (const char* page : kPages) {
        showPage(QString::fromLatin1(page));
    }
    QTest::qWait(300);
}

void LayoutConformanceTest::cleanupTestCase() {
    engine_.reset();
}

QQuickWindow* LayoutConformanceTest::window() const {
    return engine_->rootObjects().isEmpty()
               ? nullptr
               : qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst());
}

QQuickItem* LayoutConformanceTest::contentRoot() const {
    QQuickWindow* const w = window();
    return w == nullptr ? nullptr : w->contentItem();
}

void LayoutConformanceTest::evaluate(const QString& expression) {
    QObject* const shell =
        engine_->rootObjects().constFirst()->findChild<QObject*>(QStringLiteral("appShell"));
    QVERIFY(shell != nullptr);
    QQmlExpression evaluated(qmlContext(shell), shell, expression);
    evaluated.evaluate();
    QVERIFY2(!evaluated.hasError(), qPrintable(evaluated.error().toString()));
}

void LayoutConformanceTest::showPage(const QString& page) {
    evaluate(QStringLiteral("AppController.currentPage = '%1'").arg(page));
    QTest::qWait(80);
}

void LayoutConformanceTest::setSidebarCollapsed(bool collapsed) {
    evaluate(QStringLiteral("shell.sidebarCollapsed = %1")
                 .arg(collapsed ? QStringLiteral("true") : QStringLiteral("false")));
    QTest::qWait(60);
}

void LayoutConformanceTest::setScheme(const QString& scheme) {
    evaluate(QStringLiteral("AppController.themeMode = '%1'").arg(scheme));
    QTest::qWait(60);
}

void LayoutConformanceTest::resizeTo(int width, int height) {
    QQuickWindow* const w = window();
    QVERIFY(w != nullptr);
    w->resize(width, height);
    // Two waits: the first lets the resize reach the layouts, the second lets
    // anything the layouts triggered - a responsive breakpoint, a sidebar
    // collapsing - finish before anything is measured.
    QTest::qWait(80);
    QTest::qWait(80);

    // And then the delegates a resizing ListView has released. They are still
    // in childItems() until their deferred deletion runs, still visible, and
    // still carrying the width they had before the resize - which reads as an
    // item wider than the list it is in. It is not: it is on its way out.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(20);
}

template<typename Visit, typename Descend>
void LayoutConformanceTest::walk(QQuickItem* item, QQuickItem* parent, const Visit& visit,
                                 const Descend& descend) {
    if (!isLaidOut(item)) {
        return;
    }
    if (cameFromQml(item)) {
        visit(item, parent);
    }
    if (!descend(item)) {
        return;
    }
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children) {
        walk(child, item, visit, descend);
    }
}

// ------------------------------------------------------------ overflow

void LayoutConformanceTest::checkOverflow(const QString& page, QStringList& problems) const {
    walk(
        contentRoot(), nullptr,
        [&problems, &page](QQuickItem* item, QQuickItem* parent) {
            if (parent == nullptr || parent->clip()) {
                // A parent that clips has said, in as many words, that its
                // children are meant to be cut off at its edge.
                return;
            }
            if (scrollsHorizontally(parent) || scrollsVertically(parent)) {
                return;
            }
            const QRectF bounds(0, 0, parent->width(), parent->height());
            const QRectF child(item->x(), item->y(), item->width(), item->height());
            if (!bounds.adjusted(-kTolerance, -kTolerance, kTolerance, kTolerance)
                     .contains(child)) {
                // A few levels of ancestry, which is what turns "a QQuickItem
                // is too wide" into somewhere to go and look. The whole chain
                // reaches the window and is mostly noise.
                QString chain;
                int levels = 0;
                for (QQuickItem* up = parent; up != nullptr && levels < 3;
                     up = up->parentItem(), ++levels) {
                    chain += QStringLiteral(" < ") + describe(up);
                }
                problems
                    << QStringLiteral("%1: %2 sticks out of%3").arg(page, describe(item), chain);
            }
        },
        [](QQuickItem* item) {
            // Popups and menus are placed against the window rather than
            // against whatever happens to be their parent item.
            return !QString::fromLatin1(item->metaObject()->className())
                        .contains(QStringLiteral("Popup"));
        });
}

// ------------------------------------------------------------- overlap

void LayoutConformanceTest::checkOverlap(const QString& page, QStringList& problems) const {
    walk(
        contentRoot(), nullptr,
        [&problems, &page](QQuickItem* item, QQuickItem*) {
            // Only inside a layout. Two children of a plain Item are free to
            // sit on top of each other; that is how a badge on a corner or a
            // ring around a card is built.
            if (!arrangesItsChildren(item)) {
                return;
            }
            QList<QQuickItem*> placed;
            const QList<QQuickItem*> children = item->childItems();
            for (QQuickItem* child : children) {
                if (!isLaidOut(child)) {
                    continue;
                }
                const QRectF rect(child->x(), child->y(), child->width(), child->height());
                for (QQuickItem* other : placed) {
                    const QRectF against(other->x(), other->y(), other->width(), other->height());
                    const QRectF overlap = rect.intersected(against);
                    if (overlap.width() > kTolerance && overlap.height() > kTolerance) {
                        problems << QStringLiteral("%1: %2 overlaps %3 inside %4")
                                        .arg(page, describe(child), describe(other),
                                             describe(item));
                    }
                }
                placed.append(child);
            }
        },
        [](QQuickItem*) { return true; });
}

// ------------------------------------------------------------ clipping

void LayoutConformanceTest::checkTruncation(const QString& page, QStringList& problems) const {
    walk(
        contentRoot(), nullptr,
        [&problems, &page](QQuickItem* item, QQuickItem*) {
            if (!isText(item) || item->property("text").toString().isEmpty()) {
                return;
            }

            // Qt::ElideNone is 3, not 0 - ElideLeft is 0. Comparing against
            // zero read every label in the interface as eliding, which made
            // this whole check pass on anything.
            const bool elides = item->property("elide").toInt() != Qt::ElideNone;
            const bool wraps = item->property("wrapMode").toInt() != 0;  // 0 is NoWrap

            // Deliberately not Text::truncated. That is only set when the text
            // engine had to cut something off itself, which it does when
            // eliding - so a label with no elide and no room does not set it
            // at all. It paints straight out of its box instead, or gets
            // clipped by a parent, and either way the reader is given no sign.
            // Measuring what the text needs against what it has is the check
            // that catches the case worth catching.
            if (elides) {
                return;  // cut off, and saying so with an ellipsis
            }

            if (wraps) {
                if (item->implicitHeight() > item->height() + kTolerance) {
                    problems << QStringLiteral(
                                    "%1: %2 wraps to %3 high but has only %4 - the last line is "
                                    "lost")
                                    .arg(page, describe(item))
                                    .arg(item->implicitHeight(), 0, 'f', 1)
                                    .arg(item->height(), 0, 'f', 1);
                }
                return;
            }

            if (item->implicitWidth() > item->width() + kTolerance) {
                problems << QStringLiteral(
                                "%1: %2 needs %3 of width and has %4, with no elide to show it "
                                "was cut: \"%5\"")
                                .arg(page, describe(item))
                                .arg(item->implicitWidth(), 0, 'f', 1)
                                .arg(item->width(), 0, 'f', 1)
                                .arg(item->property("text").toString().left(40));
            }
        },
        [](QQuickItem*) { return true; });
}

// -------------------------------------------------------- control size

void LayoutConformanceTest::checkControlSizes(const QString& page, QStringList& problems) const {
    walk(
        contentRoot(), nullptr,
        [&problems, &page](QQuickItem* item, QQuickItem*) {
            // activeFocusOnTab is what a control sets to join the keyboard
            // order, which makes it a good stand-in for "a person is meant to
            // use this" without needing every control class in scope.
            if (!item->activeFocusOnTab() || !item->isEnabled()) {
                return;
            }
            if (item->width() < kMinimumControlSize || item->height() < kMinimumControlSize) {
                problems << QStringLiteral("%1: %2 is too small to use reliably (want %3)")
                                .arg(page, describe(item))
                                .arg(kMinimumControlSize, 0, 'f', 0);
            }
        },
        [](QQuickItem*) { return true; });
}

// ---------------------------------------------------- sideways scroll

void LayoutConformanceTest::checkSidewaysScroll(const QString& page, QStringList& problems) const {
    // Section 32: "Avoid horizontal scrolling for ordinary application
    // navigation." A page that has to be dragged sideways to be read is the
    // clearest sign a desktop layout has been treated as a web page.
    walk(
        contentRoot(), nullptr,
        [&problems, &page](QQuickItem* item, QQuickItem*) {
            if (!scrollsHorizontally(item)) {
                return;
            }
            // A table or a tree may scroll sideways on purpose; the ban is on
            // the page body doing it. Those opt in by name.
            if (item->objectName().startsWith(QStringLiteral("scrollsSideways"))) {
                return;
            }
            problems << QStringLiteral("%1: %2 needs %3 of width but has %4")
                            .arg(page, describe(item))
                            .arg(item->property("contentWidth").toReal(), 0, 'f', 1)
                            .arg(item->width(), 0, 'f', 1);
        },
        [](QQuickItem*) { return true; });
}

// ----------------------------------------------------------- the pass

void LayoutConformanceTest::everyPageFitsAtEverySize_data() {
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::addColumn<QString>("scheme");
    QTest::addColumn<bool>("collapsed");

    // One row per window size, scheme and sidebar state, with the pages walked
    // inside it. Splitting the five checks into five slots meant resizing the
    // window five times over for the same measurements, which took a minute
    // and a half of every test run and found nothing extra.
    //
    // The collapsed sidebar is a genuinely different layout - 176 pixels move
    // from the navigation to the page - so it is a dimension here rather than
    // something checked once and assumed.
    for (const Resolution& resolution : kResolutions) {
        for (const char* scheme : {"light", "dark"}) {
            for (const bool collapsed : {false, true}) {
                QTest::newRow(qPrintable(QStringLiteral("%1 %2%3").arg(
                    QLatin1String(resolution.name), QLatin1String(scheme),
                    collapsed ? QStringLiteral(" collapsed") : QString())))
                    << resolution.width << resolution.height << QString::fromLatin1(scheme)
                    << collapsed;
            }
        }
    }
}

void LayoutConformanceTest::everyPageFitsAtEverySize() {
    QFETCH(int, width);
    QFETCH(int, height);
    QFETCH(QString, scheme);
    QFETCH(bool, collapsed);

    setScheme(scheme);
    resizeTo(width, height);
    setSidebarCollapsed(collapsed);

    QStringList problems;
    for (const char* name : kPages) {
        const QString page = QString::fromLatin1(name);
        showPage(page);
        checkOverflow(page, problems);
        checkOverlap(page, problems);
        checkTruncation(page, problems);
        checkControlSizes(page, problems);
        checkSidewaysScroll(page, problems);
    }

    QVERIFY2(
        problems.isEmpty(),
        qPrintable(QStringLiteral("at %1x%2 in %3 with the sidebar %4:\n%5")
                       .arg(width)
                       .arg(height)
                       .arg(scheme,
                            collapsed ? QStringLiteral("collapsed") : QStringLiteral("expanded"),
                            problems.join(u'\n'))));
}

QTEST_MAIN(LayoutConformanceTest)
#include "LayoutConformanceTest.moc"
