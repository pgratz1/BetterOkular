/*
    SPDX-FileCopyrightText: 2026 pgratz <pgratz@gratz1.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "pinnedpagespanel.h"

#include <KLocalizedString>

#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/area.h"
#include "core/document.h"
#include "core/generator.h"
#include "core/page.h"
#include "gui/pagepainter.h"
#include "gui/priorities.h"
#include "settings.h"

// space (in pixels) around and between the pages
static const int kPageMargin = 6;
// margin (in pixels) of the area preloaded around the visible part, when tiling
static const int kPixelsToExpand = 512;
// the page under this fraction of the viewport height is the current one
static const double kProbeFraction = 1.0 / 3.0;
// how far (in pixels) from the tab bar a tab must be dragged to move it to the other side
static const int kDragOutDistance = 30;
// below this viewport size (in pixels) the view is considered not laid out yet
static const int kMinViewSize = 40;
static const double kMinZoom = 0.1;
static const double kMaxZoom = 16.0;
static const double kZoomStep = 1.2;

static const int pageflags = PagePainter::Accessibility | PagePainter::EnhanceLinks | PagePainter::EnhanceImages | PagePainter::Highlights | PagePainter::TextSelection | PagePainter::Annotations;

/** PinnedPagesPanel **/

PinnedPagesPanel::PinnedPagesPanel(QWidget *parent, Okular::Document *document, Side side)
    : QWidget(parent)
    , m_document(document)
    , m_side(side)
    , m_shownPin(-1)
    , m_pressedTab(-1)
    , m_draggedOut(false)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_tabBar = new QTabBar(this);
    m_tabBar->setTabsClosable(true);
    m_tabBar->setMovable(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setElideMode(Qt::ElideNone);
    m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tabBar->setToolTip(i18nc("@info:tooltip", "Drag a tab to the other side of the main view to move it there"));
    m_tabBar->installEventFilter(this);
    mainLayout->addWidget(m_tabBar);

    QHBoxLayout *controlsLayout = new QHBoxLayout();
    controlsLayout->setContentsMargins(2, 0, 2, 0);
    controlsLayout->setSpacing(0);
    mainLayout->addLayout(controlsLayout);

    m_pageSpinBox = new QSpinBox(this);
    m_pageSpinBox->setKeyboardTracking(false);
    m_pageSpinBox->setToolTip(i18nc("@info:tooltip", "Page shown in this pinned view"));
    controlsLayout->addWidget(m_pageSpinBox);
    controlsLayout->addStretch(1);

    auto makeButton = [this, controlsLayout](const QString &iconName, const QString &toolTip) {
        QToolButton *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setIcon(QIcon::fromTheme(iconName));
        button->setToolTip(toolTip);
        controlsLayout->addWidget(button);
        return button;
    };
    m_rotateLeftButton = makeButton(QStringLiteral("object-rotate-left"), i18nc("@info:tooltip", "Rotate pinned view counterclockwise"));
    m_rotateRightButton = makeButton(QStringLiteral("object-rotate-right"), i18nc("@info:tooltip", "Rotate pinned view clockwise"));
    m_zoomOutButton = makeButton(QStringLiteral("zoom-out"), i18nc("@info:tooltip", "Zoom out pinned view"));
    m_zoomInButton = makeButton(QStringLiteral("zoom-in"), i18nc("@info:tooltip", "Zoom in pinned view"));
    m_fitWidthButton = makeButton(QStringLiteral("zoom-fit-width"), i18nc("@info:tooltip", "Fit pinned pages to width"));
    m_fitPageButton = makeButton(QStringLiteral("zoom-fit-page"), i18nc("@info:tooltip", "Fit whole pinned pages"));
    m_fitWidthButton->setCheckable(true);
    m_fitPageButton->setCheckable(true);

    m_view = new PinnedPageView(this, document);
    mainLayout->addWidget(m_view, 1);

    connect(m_tabBar, &QTabBar::currentChanged, this, &PinnedPagesPanel::slotCurrentTabChanged);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &PinnedPagesPanel::slotTabCloseRequested);
    connect(m_tabBar, &QTabBar::tabMoved, this, &PinnedPagesPanel::slotTabMoved);
    connect(m_tabBar, &QTabBar::customContextMenuRequested, this, &PinnedPagesPanel::slotTabContextMenu);

    connect(m_pageSpinBox, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_view->isActive()) {
            m_view->scrollToPage(value - 1);
        }
    });
    connect(m_rotateLeftButton, &QToolButton::clicked, this, [this] { m_view->rotateBy(-1); });
    connect(m_rotateRightButton, &QToolButton::clicked, this, [this] { m_view->rotateBy(1); });
    connect(m_zoomOutButton, &QToolButton::clicked, this, [this] { m_view->zoomBy(1.0 / kZoomStep); });
    connect(m_zoomInButton, &QToolButton::clicked, this, [this] { m_view->zoomBy(kZoomStep); });
    connect(m_fitWidthButton, &QToolButton::clicked, this, [this] { m_view->setZoom(FitWidth, 1.0); });
    connect(m_fitPageButton, &QToolButton::clicked, this, [this] { m_view->setZoom(FitPage, 1.0); });
    connect(m_view, &PinnedPageView::zoomChanged, this, &PinnedPagesPanel::updateControls);
    connect(m_view, &PinnedPageView::currentPageChanged, this, &PinnedPagesPanel::slotViewCurrentPageChanged);
    connect(m_view, &PinnedPageView::activated, this, [this](int pageNumber) { m_document->setViewportPage(pageNumber); });

    updateControls();

    m_document->addObserver(this);
}

PinnedPagesPanel::~PinnedPagesPanel()
{
    m_document->removeObserver(this);
}

PinnedPagesPanel::Side PinnedPagesPanel::side() const
{
    return m_side;
}

void PinnedPagesPanel::pinPage(int pageNumber)
{
    if (pageNumber < 0 || pageNumber >= static_cast<int>(m_document->pages())) {
        return;
    }

    for (int i = 0; i < m_pins.count(); ++i) {
        const int shownPage = i == m_shownPin ? m_view->currentPage() : m_pins.at(i).pageNumber;
        if (shownPage == pageNumber) {
            m_tabBar->setCurrentIndex(i);
            return;
        }
    }

    Pin pin;
    pin.pageNumber = pageNumber;
    insertPin(pin);
}

PinnedPagesPanel::Pin PinnedPagesPanel::takePin(int index)
{
    storeCurrentViewState();
    const Pin pin = m_pins.value(index);
    removePinAt(index);
    return pin;
}

void PinnedPagesPanel::insertPin(const Pin &pin)
{
    const int pageCount = m_document->pages();
    if (pageCount <= 0) {
        return;
    }

    storeCurrentViewState();
    const bool wasEmpty = m_pins.isEmpty();
    Pin newPin = pin;
    newPin.pageNumber = qBound(0, newPin.pageNumber, pageCount - 1);
    // the pin must exist before the tab, as adding the first tab emits currentChanged
    m_pins.append(newPin);
    const int index = m_tabBar->addTab(QString());
    updateTabLabel(index);
    m_tabBar->setCurrentIndex(index);

    if (wasEmpty) {
        Q_EMIT hasPinnedPagesChanged(true);
    }
}

int PinnedPagesPanel::pinCount() const
{
    return m_pins.count();
}

bool PinnedPagesPanel::isEmpty() const
{
    return m_pins.isEmpty();
}

bool PinnedPagesPanel::showsPage(int pageNumber) const
{
    for (int i = 0; i < m_pins.count(); ++i) {
        const int shownPage = i == m_shownPin ? m_view->currentPage() : m_pins.at(i).pageNumber;
        if (shownPage == pageNumber) {
            return true;
        }
    }
    return false;
}

void PinnedPagesPanel::notifySetup(const QList<Okular::Page *> &pages, int setupFlags)
{
    if ((setupFlags & Okular::DocumentObserver::DocumentChanged) || pages.isEmpty()) {
        clearPins();
        return;
    }

    // document reloaded or relayouted (e.g. rotated): keep the pins, inside the new page range
    storeCurrentViewState();
    for (int i = 0; i < m_pins.count(); ++i) {
        m_pins[i].pageNumber = qMin(m_pins.at(i).pageNumber, static_cast<int>(pages.count()) - 1);
        updateTabLabel(i);
    }
    if (m_shownPin >= 0) {
        m_view->setPin(m_pins.at(m_shownPin));
    }
    updateControls();
}

void PinnedPagesPanel::notifyPageChanged(int pageNumber, int changedFlags)
{
    if (!m_view->isPageVisible(pageNumber)) {
        return;
    }
    if (changedFlags & (Pixmap | Highlights | TextSelection | Annotations | BoundingBox)) {
        m_view->viewport()->update();
    }
}

void PinnedPagesPanel::notifyContentsCleared(int changedFlags)
{
    if (changedFlags & Pixmap) {
        m_view->requestPixmaps();
    }
}

bool PinnedPagesPanel::canUnloadPixmap(int pageNumber) const
{
    return !m_view->isPageVisible(pageNumber);
}

bool PinnedPagesPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_tabBar) {
        return QWidget::eventFilter(watched, event);
    }

    // QTabBar handles dragging a tab inside the bar itself; here we only
    // notice when a tab is dragged (and dropped) well outside of the bar
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            m_pressedTab = m_tabBar->tabAt(me->position().toPoint());
            m_draggedOut = false;
        }
        break;
    }
    case QEvent::MouseMove: {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        if (m_pressedTab >= 0 && (me->buttons() & Qt::LeftButton)) {
            const QRect area = m_tabBar->rect().adjusted(-kDragOutDistance, -kDragOutDistance, kDragOutDistance, kDragOutDistance);
            const bool out = !area.contains(me->position().toPoint());
            if (out != m_draggedOut) {
                m_draggedOut = out;
                if (out) {
                    m_tabBar->setCursor(Qt::DragMoveCursor);
                } else {
                    m_tabBar->unsetCursor();
                }
            }
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            if (m_draggedOut && m_pressedTab >= 0) {
                const int index = m_pressedTab;
                const QPoint globalPos = me->globalPosition().toPoint();
                // let QTabBar finish its own drag handling first
                QTimer::singleShot(0, this, [this, index, globalPos] { Q_EMIT pinDraggedOut(index, globalPos); });
            }
            m_tabBar->unsetCursor();
            m_pressedTab = -1;
            m_draggedOut = false;
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void PinnedPagesPanel::slotCurrentTabChanged(int index)
{
    storeCurrentViewState();
    showPin(index);
}

void PinnedPagesPanel::slotTabCloseRequested(int index)
{
    removePinAt(index);
}

void PinnedPagesPanel::slotTabMoved(int from, int to)
{
    m_pins.move(from, to);
    auto follow = [from, to](int &index) {
        if (index == from) {
            index = to;
        } else if (from < index && index <= to) {
            --index;
        } else if (to <= index && index < from) {
            ++index;
        }
    };
    follow(m_shownPin);
    follow(m_pressedTab);
}

void PinnedPagesPanel::slotTabContextMenu(const QPoint pos)
{
    const int index = m_tabBar->tabAt(pos);
    if (index < 0) {
        return;
    }

    QMenu menu(this);
    const bool onLeft = m_side == LeftSide;
    const QAction *moveAction =
        menu.addAction(QIcon::fromTheme(onLeft ? QStringLiteral("go-next") : QStringLiteral("go-previous")), onLeft ? i18nc("@action:inmenu", "Move to Right Side") : i18nc("@action:inmenu", "Move to Left Side"));
    const QAction *closeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-close")), i18nc("@action:inmenu", "Close Pinned View"));

    const QAction *chosen = menu.exec(m_tabBar->mapToGlobal(pos));
    if (chosen == moveAction) {
        Q_EMIT moveToOtherSideRequested(index);
    } else if (chosen == closeAction) {
        removePinAt(index);
    }
}

void PinnedPagesPanel::slotViewCurrentPageChanged(int pageNumber)
{
    if (m_shownPin < 0 || m_shownPin >= m_pins.count()) {
        return;
    }
    m_pins[m_shownPin].pageNumber = pageNumber;
    updateTabLabel(m_shownPin);
    const QSignalBlocker blocker(m_pageSpinBox);
    m_pageSpinBox->setValue(pageNumber + 1);
}

void PinnedPagesPanel::storeCurrentViewState()
{
    if (m_shownPin < 0 || m_shownPin >= m_pins.count() || !m_view->isActive()) {
        return;
    }
    m_pins[m_shownPin] = m_view->pin();
}

void PinnedPagesPanel::showPin(int index)
{
    if (index < 0 || index >= m_pins.count()) {
        m_shownPin = -1;
        m_view->clear();
    } else {
        m_shownPin = index;
        m_view->setPin(m_pins.at(index));
    }
    updateControls();
}

void PinnedPagesPanel::removePinAt(int index)
{
    if (index < 0 || index >= m_pins.count()) {
        return;
    }

    // keep m_shownPin pointing to the right pin while the tab bar updates
    if (index == m_shownPin) {
        m_shownPin = -1;
    } else if (index < m_shownPin) {
        --m_shownPin;
    }
    m_pins.removeAt(index);
    m_tabBar->removeTab(index);

    if (m_pins.isEmpty()) {
        showPin(-1);
        Q_EMIT hasPinnedPagesChanged(false);
    }
}

void PinnedPagesPanel::clearPins()
{
    if (m_pins.isEmpty()) {
        return;
    }

    m_pins.clear();
    m_tabBar->blockSignals(true);
    while (m_tabBar->count() > 0) {
        m_tabBar->removeTab(0);
    }
    m_tabBar->blockSignals(false);
    showPin(-1);
    Q_EMIT hasPinnedPagesChanged(false);
}

void PinnedPagesPanel::updateTabLabel(int index)
{
    if (index < 0 || index >= m_pins.count()) {
        return;
    }
    const int pageNumber = m_pins.at(index).pageNumber;
    const Okular::Page *page = m_document->page(pageNumber);
    const QString label = (page && !page->label().isEmpty()) ? page->label() : QString::number(pageNumber + 1);
    m_tabBar->setTabText(index, i18nc("@title:tab short for page number", "p. %1", label));
    m_tabBar->setTabToolTip(index, i18nc("@info:tooltip", "Pinned view at page %1", pageNumber + 1));
}

void PinnedPagesPanel::updateControls()
{
    const bool active = m_view->isActive();
    const QList<QWidget *> controls = {m_pageSpinBox, m_rotateLeftButton, m_rotateRightButton, m_zoomOutButton, m_zoomInButton, m_fitWidthButton, m_fitPageButton};
    for (QWidget *control : controls) {
        control->setEnabled(active);
    }
    m_fitWidthButton->setChecked(active && m_view->zoomMode() == FitWidth);
    m_fitPageButton->setChecked(active && m_view->zoomMode() == FitPage);

    const QSignalBlocker blocker(m_pageSpinBox);
    const int pageCount = qMax(1, static_cast<int>(m_document->pages()));
    m_pageSpinBox->setRange(1, pageCount);
    m_pageSpinBox->setSuffix(i18nc("@label:spinbox suffix, total number of pages", " / %1", pageCount));
    if (active) {
        m_pageSpinBox->setValue(m_view->currentPage() + 1);
    }
}

/** PinnedPageView **/

PinnedPageView::PinnedPageView(PinnedPagesPanel *panel, Okular::Document *document)
    : QAbstractScrollArea(panel)
    , m_panel(panel)
    , m_document(document)
    , m_active(false)
    , m_zoomMode(PinnedPagesPanel::FitWidth)
    , m_zoomFactor(1.0)
    , m_rotation(0)
    , m_currentPage(0)
    , m_pendingPage(-1)
    , m_pendingOffset(-1.0)
    , m_inLayout(false)
    , m_dragging(false)
{
    setFrameStyle(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    viewport()->setCursor(Qt::OpenHandCursor);
    horizontalScrollBar()->setSingleStep(20);
    verticalScrollBar()->setSingleStep(20);
}

void PinnedPageView::setPin(const PinnedPagesPanel::Pin &pin)
{
    m_active = true;
    m_zoomMode = pin.zoomMode;
    m_zoomFactor = qBound(kMinZoom, pin.zoomFactor, kMaxZoom);
    m_rotation = ((pin.rotation % 4) + 4) % 4;
    m_items.clear();
    m_currentPage = pin.pageNumber;
    m_pendingPage = pin.pageNumber;
    m_pendingOffset = pin.pageOffset;
    relayout();
    Q_EMIT zoomChanged();
}

void PinnedPageView::clear()
{
    m_active = false;
    m_items.clear();
    m_visiblePages.clear();
    m_pendingPage = -1;
    m_contentsSize = QSize();
    doLayout();
    viewport()->update();
}

bool PinnedPageView::isActive() const
{
    return m_active;
}

PinnedPagesPanel::Pin PinnedPageView::pin() const
{
    PinnedPagesPanel::Pin pin;
    if (m_pendingPage >= 0) {
        pin.pageNumber = m_pendingPage;
        pin.pageOffset = m_pendingOffset;
    } else {
        currentPosition(&pin.pageNumber, &pin.pageOffset);
    }
    pin.zoomMode = m_zoomMode;
    pin.zoomFactor = m_zoomFactor;
    pin.rotation = m_rotation;
    return pin;
}

int PinnedPageView::currentPage() const
{
    return m_pendingPage >= 0 ? m_pendingPage : m_currentPage;
}

void PinnedPageView::scrollToPage(int pageNumber)
{
    setPosition(pageNumber, -1.0);
}

bool PinnedPageView::isPageVisible(int pageNumber) const
{
    return m_visiblePages.contains(pageNumber);
}

void PinnedPageView::setZoom(PinnedPagesPanel::ZoomMode zoomMode, double zoomFactor)
{
    m_zoomMode = zoomMode;
    m_zoomFactor = qBound(kMinZoom, zoomFactor, kMaxZoom);
    relayout();
    Q_EMIT zoomChanged();
}

PinnedPagesPanel::ZoomMode PinnedPageView::zoomMode() const
{
    return m_zoomMode;
}

double PinnedPageView::effectiveZoom() const
{
    if (m_zoomMode == PinnedPagesPanel::Custom || m_currentPage < 0 || m_currentPage >= m_items.count()) {
        return m_zoomFactor;
    }
    const Okular::Page *page = m_document->page(m_currentPage);
    const double pageWidth = (m_rotation % 2) ? page->height() : page->width();
    return pageWidth > 0 ? m_items.at(m_currentPage).geometry.width() / pageWidth : m_zoomFactor;
}

void PinnedPageView::zoomBy(double factor)
{
    if (!m_active) {
        return;
    }

    // keep the horizontal center where it is, the vertical position follows the current page
    const double relX = m_contentsSize.width() > 0 ? (horizontalScrollBar()->value() + viewport()->width() / 2.0) / m_contentsSize.width() : 0.5;
    setZoom(PinnedPagesPanel::Custom, effectiveZoom() * factor);
    horizontalScrollBar()->setValue(qRound(relX * m_contentsSize.width() - viewport()->width() / 2.0));
}

void PinnedPageView::rotateBy(int quarterTurns)
{
    if (!m_active) {
        return;
    }
    m_rotation = (((m_rotation + quarterTurns) % 4) + 4) % 4;
    relayout();
    Q_EMIT zoomChanged();
}

int PinnedPageView::rotation() const
{
    return m_rotation;
}

QRect PinnedPageView::pageRect(int pageNumber) const
{
    if (pageNumber < 0 || pageNumber >= m_items.count()) {
        return QRect();
    }
    return m_items.at(pageNumber).geometry.translated(contentsOffset());
}

void PinnedPageView::relayout()
{
    if (m_inLayout) {
        return;
    }
    m_inLayout = true;

    int pageNumber = m_currentPage;
    double offset = -1.0;
    bool restore = false;
    if (m_pendingPage >= 0) {
        pageNumber = m_pendingPage;
        offset = m_pendingOffset;
        restore = true;
    } else if (!m_items.isEmpty()) {
        currentPosition(&pageNumber, &offset);
        restore = true;
    }

    doLayout();

    if (m_active && restore && !m_items.isEmpty()) {
        setPosition(qBound(0, pageNumber, static_cast<int>(m_items.count()) - 1), offset);
    }
    m_inLayout = false;

    viewport()->update();
    requestPixmaps();
    updateCurrentPage();
}

void PinnedPageView::doLayout()
{
    const QSize maxViewport = maximumViewportSize();
    const int scrollBarExtent = verticalScrollBar()->sizeHint().width();

    if (m_active) {
        const int availableWidth = maxViewport.width() - 2 * kPageMargin;
        const int availableHeight = maxViewport.height() - 2 * kPageMargin;
        computeLayout(availableWidth, availableHeight);
        // leave room for the vertical scrollbar if the pages will need it
        if (m_zoomMode != PinnedPagesPanel::Custom && m_contentsSize.height() > maxViewport.height()) {
            computeLayout(availableWidth - scrollBarExtent, availableHeight);
        }
    }

    // compute the viewport size the scrollbars will leave us
    int viewWidth = maxViewport.width();
    int viewHeight = maxViewport.height();
    if (m_contentsSize.height() > viewHeight) {
        viewWidth -= scrollBarExtent;
    }
    if (m_contentsSize.width() > viewWidth) {
        viewHeight -= horizontalScrollBar()->sizeHint().height();
    }

    m_viewSize = QSize(viewWidth, viewHeight);
    horizontalScrollBar()->setRange(0, qMax(0, m_contentsSize.width() - viewWidth));
    horizontalScrollBar()->setPageStep(viewWidth);
    verticalScrollBar()->setRange(0, qMax(0, m_contentsSize.height() - viewHeight));
    verticalScrollBar()->setPageStep(viewHeight);
}

void PinnedPageView::computeLayout(int availableWidth, int availableHeight)
{
    m_items.clear();
    const int pageCount = m_document->pages();
    m_items.reserve(pageCount);

    const bool sideways = m_rotation % 2;
    int y = kPageMargin;
    int maxWidth = 0;
    for (int i = 0; i < pageCount; ++i) {
        const Okular::Page *page = m_document->page(i);
        double pageWidth = sideways ? page->height() : page->width();
        double pageHeight = sideways ? page->width() : page->height();
        if (pageWidth <= 0 || pageHeight <= 0) {
            pageWidth = pageHeight = 1;
        }
        const double ratio = pageHeight / pageWidth;

        double width;
        switch (m_zoomMode) {
        case PinnedPagesPanel::FitWidth:
            width = availableWidth;
            break;
        case PinnedPagesPanel::FitPage:
            width = qMin<double>(availableWidth, availableHeight / ratio);
            break;
        default:
            width = pageWidth * m_zoomFactor;
            break;
        }

        Item item;
        const int w = qMax(4, qRound(width));
        const int h = qMax(4, qRound(w * ratio));
        item.geometry = QRect(0, y, w, h);
        item.renderWidth = sideways ? h : w;
        item.renderHeight = sideways ? w : h;
        m_items.append(item);

        y += h + kPageMargin;
        maxWidth = qMax(maxWidth, w);
    }

    // center the pages horizontally
    for (Item &item : m_items) {
        item.geometry.moveLeft(kPageMargin + (maxWidth - item.geometry.width()) / 2);
    }
    m_contentsSize = pageCount > 0 ? QSize(maxWidth + 2 * kPageMargin, y) : QSize();
}

QPoint PinnedPageView::contentsOffset() const
{
    // use the size the layout was made for, the viewport may be in the middle of a resize
    const QSize view = m_viewSize;
    const int x = m_contentsSize.width() < view.width() ? (view.width() - m_contentsSize.width()) / 2 : -horizontalScrollBar()->value();
    const int y = m_contentsSize.height() < view.height() ? (view.height() - m_contentsSize.height()) / 2 : -verticalScrollBar()->value();
    return QPoint(x, y);
}

QTransform PinnedPageView::renderToItem(const Item &item) const
{
    // maps the (unrotated) pixmap coordinates to the displayed item, clockwise
    const int w = item.geometry.width();
    const int h = item.geometry.height();
    switch (m_rotation) {
    case 1:
        return QTransform().translate(w, 0).rotate(90);
    case 2:
        return QTransform().translate(w, h).rotate(180);
    case 3:
        return QTransform().translate(0, h).rotate(270);
    default:
        return QTransform();
    }
}

int PinnedPageView::pageAtContentsY(int y) const
{
    // the page whose bottom (including the following gap) is below y
    auto it = std::lower_bound(m_items.cbegin(), m_items.cend(), y, [](const Item &item, int value) { return item.geometry.bottom() + kPageMargin < value; });
    if (it == m_items.cend()) {
        return m_items.count() - 1;
    }
    return static_cast<int>(it - m_items.cbegin());
}

int PinnedPageView::pageAtViewportPos(const QPoint pos) const
{
    const QPoint contentsPos = pos - contentsOffset();
    const int candidate = pageAtContentsY(contentsPos.y());
    if (candidate >= 0 && m_items.at(candidate).geometry.contains(contentsPos)) {
        return candidate;
    }
    return -1;
}

void PinnedPageView::currentPosition(int *pageNumber, double *offset) const
{
    if (m_items.isEmpty()) {
        *pageNumber = m_currentPage;
        *offset = -1.0;
        return;
    }
    const int probeY = qRound(m_viewSize.height() * kProbeFraction) - contentsOffset().y();
    *pageNumber = pageAtContentsY(probeY);
    const QRect geometry = m_items.at(*pageNumber).geometry;
    *offset = qBound(0.0, (probeY - geometry.top()) / (double)geometry.height(), 1.0);
}

void PinnedPageView::setPosition(int pageNumber, double offset)
{
    if (m_items.isEmpty()) {
        return;
    }
    pageNumber = qBound(0, pageNumber, static_cast<int>(m_items.count()) - 1);

    if (!isVisible() || m_viewSize.width() < kMinViewSize || m_viewSize.height() < kMinViewSize) {
        // no real geometry yet (hidden, or just shown and not sized yet), apply it later
        m_pendingPage = pageNumber;
        m_pendingOffset = offset;
        m_currentPage = pageNumber;
        return;
    }
    m_pendingPage = -1;

    const QRect geometry = m_items.at(pageNumber).geometry;
    int value;
    if (offset < 0) {
        value = geometry.top() - kPageMargin;
    } else {
        value = qRound(geometry.top() + offset * geometry.height() - m_viewSize.height() * kProbeFraction);
    }
    verticalScrollBar()->setValue(value);
    updateCurrentPage();
}

void PinnedPageView::updateCurrentPage()
{
    if (!m_active || m_items.isEmpty() || m_pendingPage >= 0) {
        return;
    }
    int pageNumber;
    double offset;
    currentPosition(&pageNumber, &offset);
    if (pageNumber != m_currentPage) {
        m_currentPage = pageNumber;
        Q_EMIT currentPageChanged(pageNumber);
    }
}

void PinnedPageView::requestPixmaps()
{
    m_visiblePages.clear();
    if (!m_active || m_items.isEmpty() || !isVisible()) {
        return;
    }

    const QPoint offset = contentsOffset();
    const QRect viewRect = viewport()->rect();
    QList<Okular::PixmapRequest *> requests;
    int first = -1;
    int last = -1;

    for (int i = pageAtContentsY(viewRect.top() - offset.y()); i < m_items.count(); ++i) {
        const Item &item = m_items.at(i);
        const QRect itemRect = item.geometry.translated(offset);
        if (itemRect.top() > viewRect.bottom()) {
            break;
        }
        const QRect visible = itemRect.intersected(viewRect);
        if (visible.isEmpty()) {
            continue;
        }
        m_visiblePages.insert(i);
        if (first < 0) {
            first = i;
        }
        last = i;

        // visible area in the coordinates of the (unrotated) pixmap
        const QRectF renderVisible = renderToItem(item).inverted().mapRect(QRectF(visible.translated(-itemRect.topLeft())));
        const Okular::NormalizedRect visibleRect(qBound(0.0, renderVisible.left() / item.renderWidth, 1.0),
                                                 qBound(0.0, renderVisible.top() / item.renderHeight, 1.0),
                                                 qBound(0.0, renderVisible.right() / item.renderWidth, 1.0),
                                                 qBound(0.0, renderVisible.bottom() / item.renderHeight, 1.0));

        // same strategy as PageView: when tiled, also preload around the visible area
        const Okular::Page *page = m_document->page(i);
        Okular::NormalizedRect requestRect = visibleRect;
        const bool tiled = page->hasTilesManager(m_panel);
        if (tiled) {
            const double marginX = kPixelsToExpand / (double)item.renderWidth;
            const double marginY = kPixelsToExpand / (double)item.renderHeight;
            requestRect.left = qMax(0.0, visibleRect.left - marginX);
            requestRect.top = qMax(0.0, visibleRect.top - marginY);
            requestRect.right = qMin(1.0, visibleRect.right + marginX);
            requestRect.bottom = qMin(1.0, visibleRect.bottom + marginY);
        }

        if (!page->hasPixmap(m_panel, item.renderWidth, item.renderHeight, requestRect)) {
            Okular::PixmapRequest *request = new Okular::PixmapRequest(m_panel, i, item.renderWidth, item.renderHeight, devicePixelRatioF(), PAGEVIEW_PRIO, Okular::PixmapRequest::Asynchronous);
            request->setNormalizedRect(requestRect);
            request->setTile(tiled);
            requests.append(request);
        }
    }

    // preload the pages just before and after the visible ones
    if (first >= 0 && Okular::Settings::memoryLevel() != Okular::Settings::EnumMemoryLevel::Low) {
        for (const int i : {last + 1, first - 1}) {
            if (i < 0 || i >= m_items.count()) {
                continue;
            }
            const Item &item = m_items.at(i);
            const Okular::Page *page = m_document->page(i);
            if (!page->hasTilesManager(m_panel) && !page->hasPixmap(m_panel, item.renderWidth, item.renderHeight)) {
                requests.append(new Okular::PixmapRequest(m_panel,
                                                          i,
                                                          item.renderWidth,
                                                          item.renderHeight,
                                                          devicePixelRatioF(),
                                                          PAGEVIEW_PRELOAD_PRIO,
                                                          Okular::PixmapRequest::PixmapRequestFeatures(Okular::PixmapRequest::Asynchronous) | Okular::PixmapRequest::Preload));
            }
        }
    }

    if (!requests.isEmpty()) {
        m_document->requestPixmaps(requests);
    }
}

void PinnedPageView::paintEvent(QPaintEvent *e)
{
    QPainter p(viewport());

    const QColor backColor = Okular::Settings::useCustomBackgroundColor() ? Okular::Settings::backgroundColor() : viewport()->palette().color(QPalette::Dark);
    p.fillRect(e->rect(), backColor);

    if (!m_active || m_items.isEmpty()) {
        return;
    }

    const QPoint offset = contentsOffset();
    const QRect exposed = e->rect();
    const qreal dpr = devicePixelRatioF();
    QPen outlinePen(Qt::black);
    outlinePen.setWidth(0);

    for (int i = pageAtContentsY(exposed.top() - offset.y() - kPageMargin); i < m_items.count(); ++i) {
        const Item &item = m_items.at(i);
        const QRect itemRect = item.geometry.translated(offset);
        if (itemRect.top() > exposed.bottom() + kPageMargin) {
            break;
        }

        p.save();
        p.translate(itemRect.topLeft());

        const QRect clip = exposed.intersected(itemRect).translated(-itemRect.topLeft());
        if (!clip.isEmpty()) {
            const QTransform transform = renderToItem(item);
            const QRect renderClip = transform.inverted().mapRect(clip).intersected(QRect(0, 0, item.renderWidth, item.renderHeight));
            if (renderClip.isValid()) {
                p.save();
                p.setTransform(transform, true);
                PagePainter::paintPageOnPainter(&p, m_document->page(i), m_panel, pageflags, item.renderWidth, item.renderHeight, renderClip);
                p.restore();
            }
        }

        // page outline
        p.setPen(outlinePen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(-1.0 / dpr, -1.0 / dpr, itemRect.width() + 1.0 / dpr, itemRect.height() + 1.0 / dpr));
        p.restore();
    }
}

void PinnedPageView::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    relayout();
}

void PinnedPageView::showEvent(QShowEvent *e)
{
    QAbstractScrollArea::showEvent(e);
    relayout();
}

void PinnedPageView::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    viewport()->update();
    if (!m_inLayout) {
        requestPixmaps();
        updateCurrentPage();
    }
}

void PinnedPageView::keyPressEvent(QKeyEvent *e)
{
    if (m_active && e->modifiers() == Qt::NoModifier) {
        if (e->key() == Qt::Key_Home) {
            scrollToPage(0);
            e->accept();
            return;
        } else if (e->key() == Qt::Key_End) {
            scrollToPage(m_items.count() - 1);
            e->accept();
            return;
        }
    }
    QAbstractScrollArea::keyPressEvent(e);
}

void PinnedPageView::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const int delta = e->angleDelta().y();
        if (delta != 0) {
            zoomBy(delta > 0 ? kZoomStep : 1.0 / kZoomStep);
        }
        e->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(e);
}

void PinnedPageView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStart = e->globalPosition().toPoint();
        m_dragScrollStart = QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
        viewport()->setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QAbstractScrollArea::mousePressEvent(e);
}

void PinnedPageView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        const QPoint scroll = m_dragScrollStart - (e->globalPosition().toPoint() - m_dragStart);
        horizontalScrollBar()->setValue(scroll.x());
        verticalScrollBar()->setValue(scroll.y());
        e->accept();
        return;
    }
    QAbstractScrollArea::mouseMoveEvent(e);
}

void PinnedPageView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        viewport()->setCursor(Qt::OpenHandCursor);
        e->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(e);
}

void PinnedPageView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_active) {
        const int pageNumber = pageAtViewportPos(e->position().toPoint());
        if (pageNumber >= 0) {
            Q_EMIT activated(pageNumber);
            e->accept();
            return;
        }
    }
    QAbstractScrollArea::mouseDoubleClickEvent(e);
}

#include "moc_pinnedpagespanel.cpp"
