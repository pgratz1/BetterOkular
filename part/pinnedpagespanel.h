/*
    SPDX-FileCopyrightText: 2026 pgratz <pgratz@gratz1.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef _OKULAR_PINNEDPAGESPANEL_H_
#define _OKULAR_PINNEDPAGESPANEL_H_

#include <QAbstractScrollArea>
#include <QList>
#include <QPoint>
#include <QSet>
#include <QTransform>
#include <QWidget>

#include "core/observer.h"
#include "okularpart_export.h"

class QSpinBox;
class QTabBar;
class QToolButton;
class PinnedPageView;

namespace Okular
{
class Document;
class Page;
}

/**
 * @short A side panel with "pinned" views of the document, independent of the main view.
 *
 * Each tab is a small continuous viewer of the whole document with its own
 * position, zoom and rotation, so the main PageView can scroll freely (in any
 * view mode) while e.g. a figure stays in sight. There can be one panel on
 * each side of the main view; tabs can be dragged from one to the other.
 *
 * The panel is a passive observer: it never changes the document viewport,
 * except when the user double clicks a page to jump to it.
 */
class OKULARPART_EXPORT PinnedPagesPanel : public QWidget, public Okular::DocumentObserver
{
    Q_OBJECT

public:
    enum Side { LeftSide, RightSide };
    enum ZoomMode { FitWidth, FitPage, Custom };

    // the state of one tab, it moves along when a tab is moved to the other panel
    struct Pin {
        int pageNumber = 0;
        double pageOffset = -1.0; // position inside pageNumber (0..1), negative means "top of the page"
        ZoomMode zoomMode = FitWidth;
        double zoomFactor = 1.0; // only used for Custom
        int rotation = 0;        // extra quarter turns, clockwise
    };

    PinnedPagesPanel(QWidget *parent, Okular::Document *document, Side side);
    ~PinnedPagesPanel() override;

    Side side() const;

    // add a tab showing pageNumber (or select the tab already showing it)
    void pinPage(int pageNumber);
    // remove the tab at index and return its state
    Pin takePin(int index);
    // add a tab with the given state and select it
    void insertPin(const Pin &pin);

    int pinCount() const;
    bool isEmpty() const;
    bool showsPage(int pageNumber) const;

    // inherited from DocumentObserver
    void notifySetup(const QList<Okular::Page *> &pages, int setupFlags) override;
    void notifyPageChanged(int pageNumber, int changedFlags) override;
    void notifyContentsCleared(int changedFlags) override;
    bool canUnloadPixmap(int pageNumber) const override;

Q_SIGNALS:
    // emitted when the panel goes from empty to non empty and vice versa
    void hasPinnedPagesChanged(bool hasPinnedPages);
    // a tab was dragged out of the tab bar and dropped at globalPos
    void pinDraggedOut(int index, const QPoint globalPos);
    void moveToOtherSideRequested(int index);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void slotCurrentTabChanged(int index);
    void slotTabCloseRequested(int index);
    void slotTabMoved(int from, int to);
    void slotTabContextMenu(const QPoint pos);
    void slotViewCurrentPageChanged(int pageNumber);

private:
    void storeCurrentViewState();
    void showPin(int index);
    void removePinAt(int index);
    void clearPins();
    void updateTabLabel(int index);
    void updateControls();

    Okular::Document *m_document;
    Side m_side;
    QTabBar *m_tabBar;
    QSpinBox *m_pageSpinBox;
    QToolButton *m_rotateLeftButton;
    QToolButton *m_rotateRightButton;
    QToolButton *m_zoomOutButton;
    QToolButton *m_zoomInButton;
    QToolButton *m_fitWidthButton;
    QToolButton *m_fitPageButton;
    PinnedPageView *m_view;
    QList<Pin> m_pins;
    int m_shownPin;

    // dragging a tab out of the tab bar
    int m_pressedTab;
    bool m_draggedOut;

    friend class PinnedPageView;
};

/**
 * @short Scroll area showing all the pages of the document in a single
 * continuous column, at its own zoom and rotation.
 */
class OKULARPART_EXPORT PinnedPageView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    PinnedPageView(PinnedPagesPanel *panel, Okular::Document *document);

    // show the given state
    void setPin(const PinnedPagesPanel::Pin &pin);
    void clear();
    bool isActive() const;
    // the current state, for storing it in the panel
    PinnedPagesPanel::Pin pin() const;

    int currentPage() const;
    void scrollToPage(int pageNumber);
    bool isPageVisible(int pageNumber) const;

    void setZoom(PinnedPagesPanel::ZoomMode zoomMode, double zoomFactor);
    PinnedPagesPanel::ZoomMode zoomMode() const;
    void zoomBy(double factor);
    void rotateBy(int quarterTurns);
    int rotation() const;

    // recompute the layout keeping the current position, and ask for the visible pixmaps
    void relayout();
    void requestPixmaps();

    // geometry of a page in viewport coordinates
    QRect pageRect(int pageNumber) const;

Q_SIGNALS:
    void currentPageChanged(int pageNumber);
    void zoomChanged();
    void activated(int pageNumber);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    struct Item {
        QRect geometry; // displayed (rotated) geometry, in contents coordinates
        int renderWidth;
        int renderHeight; // size of the pixmap, in the document orientation
    };

    void doLayout();
    void computeLayout(int availableWidth, int availableHeight);
    double effectiveZoom() const;
    QPoint contentsOffset() const;
    QTransform renderToItem(const Item &item) const;
    int pageAtContentsY(int y) const;
    int pageAtViewportPos(const QPoint pos) const;
    void currentPosition(int *pageNumber, double *offset) const;
    void setPosition(int pageNumber, double offset);
    void updateCurrentPage();

    PinnedPagesPanel *m_panel;
    Okular::Document *m_document;
    bool m_active;
    PinnedPagesPanel::ZoomMode m_zoomMode;
    double m_zoomFactor;
    int m_rotation;
    QList<Item> m_items;
    QSize m_contentsSize;
    // viewport size the layout was computed for
    QSize m_viewSize;
    int m_currentPage;
    // position to restore once the view has a real size (it may be hidden)
    int m_pendingPage;
    double m_pendingOffset;
    bool m_inLayout;
    QSet<int> m_visiblePages;
    QPoint m_dragStart;
    QPoint m_dragScrollStart;
    bool m_dragging;
};

#endif
