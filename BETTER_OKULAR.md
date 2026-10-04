# BetterOkular: Okular with pinned page views

BetterOkular is a personal fork of [Okular](https://okular.kde.org), the KDE document viewer. It adds **pinned page views**: you can keep any page, such as a figure, table or equation, visible in a side panel while you scroll the main view somewhere else. You no longer have to scroll back and forth between the text and the figure it refers to.

- GitHub: <https://github.com/pgratz1/BetterOkular> (branch `pin-pages`, which is the default branch)
- Upstream: <https://invent.kde.org/graphics/okular> (git remote `origin`)
- Based on: Okular **v25.12.3**, the version Kubuntu 26.04 ships

---

## What it does

- **Pin a page:**
  - Right-click any page, in the main view or in the Thumbnails sidebar, and choose **Pin Page to Left** or **Pin Page to Right**.
  - Or use **View → Pin Current Page to Right** (`Ctrl+Alt+P`) or **to Left** (`Ctrl+Alt+Shift+P`).
- **Panels on both sides:** you can have a pinned panel on the left of the main view, on the right, or on both. Each panel hides when empty, and the panel widths are remembered.
- **Tabs:** each pin is a tab. A panel can hold several tabs, labelled with the page they show ("p. 12").
- **Each tab is its own mini-viewer of the whole document.** Its position, zoom and rotation are independent of the main view and of the other tabs:
  - Scroll through pages with the wheel, the scrollbar, PgUp/PgDn/Home/End, or the page box ("12 / 40").
  - Zoom with the buttons (zoom out/in, fit width, fit page) or `Ctrl+wheel`.
  - Pan by dragging with the left mouse button.
  - **Rotate** with the rotate buttons. This is independent of View → Orientation, so you can turn a landscape figure sideways without rotating the whole document.
  - **Double-click** a page to jump the main view there.
- **Move tabs between sides:** drag a tab down out of the tab bar and drop it past the middle of the main view. You can also right-click the tab and choose **Move to Left/Right Side**. A tab keeps its page, zoom and rotation when it moves.
- **Close** a tab with its × button or the tab's right-click menu.
- The main view is unaffected, so every view mode (Single, Facing, Overview, …) keeps working.
- Pins are cleared when you open a different document. They survive Okular's automatic reload when the file changes on disk.

---

## Installing on a machine

### Requirements

- **Kubuntu 26.04** or newer. The code needs Qt ≥ 6.6 and KDE Frameworks 6, which Kubuntu has shipped since 25.04 with Plasma 6. On older releases (24.04 / Plasma 5) this source tree will not build.
- About 1 GB of free disk space for the build, and a few minutes of compiling.
- `sudo` access, once, to install the build dependencies.

### 1. Get the source

Either:
- **Dropbox:** it's already at `~/Documents/MyCode/BetterOkular/okular` (`~/Documents` is a link into Dropbox). Or:
- **GitHub:** `git clone https://github.com/pgratz1/BetterOkular.git` (the default branch is `pin-pages`).

### 2. Enable source repositories (needed once per machine)

`apt build-dep` needs the `deb-src` lists:

```sh
sudo sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources
sudo apt update
```

Or open Discover → Settings → Software Sources and enable "Source code".

### 3. Build, install and add the launcher

```sh
cd ~/Documents/MyCode/BetterOkular/okular
./betterokular/install.sh
```

The script:
1. Installs any missing build dependencies (`sudo apt-get build-dep okular`). Pass `--no-deps` to skip this.
2. Configures and builds into **`~/okular-build`**. The build directory must be outside Dropbox; the script refuses a build directory inside Dropbox.
3. Installs into **`~/okular-install`**. It never touches `/usr`.
4. Installs the launcher **`~/.local/bin/okular-pin`** and a menu entry **"Okular (Pinned Pages)"**. Pass `--no-launcher` to skip this.

Other options: `--tests` also builds the autotests. You can override the directories with `BUILD_DIR=… PREFIX=… ./betterokular/install.sh`.

### 4. Use it

```sh
okular-pin paper.pdf
```

You can also use the "Okular (Pinned Pages)" entry in the application menu, or right-click a PDF → Open With.

To make it the default PDF viewer:
```sh
xdg-mime default okular-pin.desktop application/pdf
# undo:
xdg-mime default okularApplication_pdf.desktop application/pdf
```

---

## How it coexists with the system Okular

- The system `okular` package from apt stays installed and keeps receiving updates. BetterOkular lives only in `~/okular-install`.
- `okular-pin` sets `QT_PLUGIN_PATH` and related variables so that Okular loads **this** build's plugin (`okularpart.so`) and file-format plugins. Running `~/okular-install/bin/okular` directly without them would silently load the system plugin, and pinning wouldn't be there.
- Settings are shared: both versions use `~/.config/okularpartrc`, `~/.config/okularrc` and the per-document data. That's harmless; the stock Okular ignores the extra keys.
- Apps that embed Okular's viewer, such as Kate's document preview, keep using the system version.

### After system updates

- **Qt updates (important):** Okular uses some of Qt's private internals (`Qt6GuiPrivate`), which only work with the exact Qt version it was built against. After apt upgrades Qt, `okular-pin` may fail to start or crash. Rebuild:
  ```sh
  cmake --build ~/okular-build && cmake --install ~/okular-build
  ```
  Re-running `./betterokular/install.sh` does the same.
- **KDE Frameworks updates** are normally safe. Rebuild the same way if anything misbehaves.
- **A new Okular release in Kubuntu:** BetterOkular stays at 25.12.3 plus pinning until the `pin-pages` branch is rebased onto the new release tag. Fetch the tags (`git fetch origin --tags`), then `git rebase --onto vX.Y.Z v25.12.3 pin-pages`, then rebuild.

### Uninstall

```sh
rm -rf ~/okular-build ~/okular-install ~/.local/bin/okular-pin ~/.local/share/applications/okular-pin.desktop
update-desktop-database ~/.local/share/applications
```

---

## Using several machines through Dropbox

- Only the **source** syncs. Each machine needs its own one-time setup: build dependencies, `~/okular-build`, `~/okular-install`, and the launcher in `~/.local`. Run steps 2 and 3 above on each machine.
- The git repository (`okular/.git`) also syncs through Dropbox. That works if you **only work on one machine at a time** and let Dropbox finish syncing before switching. Committing on two machines at once can corrupt the repository.
- **GitHub is the safe copy of the code.** Commit and `git push github pin-pages` when you finish a change. If the Dropbox copy ever gets into a bad state, clone the repo fresh from GitHub.
- When code changes arrive from another machine, rebuild with `cmake --build ~/okular-build && cmake --install ~/okular-build`.
- Claude Code works on any of the machines. Start it in `~/Documents/MyCode/BetterOkular`; the `CLAUDE.md` there points to this file. Claude Code's memory of earlier sessions is stored per machine (in `~/.claude`), so this file is the shared record.

---

## For developers

### Source layout

| File | What it is |
|---|---|
| `part/pinnedpagespanel.h/.cpp` | `PinnedPagesPanel`: one side panel, made of a tab bar, a controls row and a view. It's a `DocumentObserver`, and holds a `Pin` (page, offset, zoom, rotation) per tab. `PinnedPageView`: a continuous single-column page view with its own zoom and rotation. |
| `part/part.cpp/.h` | Creates the splitter `[left panel \| PageView \| right panel]`, the pin actions, the context-menu entries, and moving pins between panels (`slotPinDraggedOut`, `moveToOtherSideRequested`). |
| `part/part.rc` | The View-menu entries `pin_current_page_left` and `pin_current_page` (rc version 57). |
| `conf/okular.kcfg` | `PinnedLeftPanelWidth` and `PinnedRightPanelWidth` in the "Main View" group. |
| `autotests/parttest.cpp` | `PartTest::testPinPages`. |
| `betterokular/` | `install.sh`, the `okular-pin` launcher, and the menu entry template. These aren't part of Okular itself. |

### Design notes

- A pinned panel is a **passive observer**. It never calls `Document::setViewport`, except on a double-click, so it can't fight the main `PageView` over the current page. It requests its own pixmaps (`PixmapRequest` with itself as the observer). It uses tiled rendering at high zoom, following `PageView::slotRequestVisiblePixmaps`, and paints with `PagePainter::paintPageOnPainter`.
- **Rotation of a pin** is a `QPainter` transform applied on top of the document's own orientation. Pixmaps are requested at the unrotated size, and the visible area is mapped back through the inverse transform to decide which part of the page to request.
- **Positions** are stored as "page + fraction down that page", measured at 1/3 of the viewport height. That keeps them valid across zoom, resize and rotation changes. A position set while the view is hidden or not yet sized is kept pending until the view has a real size.

### Build and test

```sh
cmake -S . -B ~/okular-build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_INSTALL_PREFIX=$HOME/okular-install -DBUILD_TESTING=ON
cmake --build ~/okular-build
QT_QPA_PLATFORM=offscreen ~/okular-build/bin/parttest testPinPages   # this feature's test
QT_QPA_PLATFORM=offscreen ~/okular-build/bin/parttest                # the whole part test suite
```

`mainshelltest` and `annotationtoolbartest` also fail on unmodified v25.12.3 when run headless (`offscreen`). That isn't caused by this feature.

### Contributing upstream

KDE doesn't accept GitHub pull requests; `github.com/KDE/okular` is a read-only mirror. To propose this feature to Okular:
1. Get a KDE Identity account and fork `graphics/okular` on invent.kde.org.
2. Rebase `pin-pages` onto `master`. This may need newer KDE Frameworks than Kubuntu ships; use `kde-builder` if so.
3. Push to the fork and open a merge request, with screenshots. It's best to discuss the feature first, in an issue or on the Okular Matrix channel.

### Not done yet

- Pins aren't saved per document. A possible approach is the docdata XML in `core/document.cpp`, next to bookmarks.
- Links and annotations inside a pinned view can't be clicked.
- Pinned views ignore *Trim Margins*.
- Okular may skip pre-rendering the pages just above and below a pinned view when they're far from where the main view is. Visible pages always render.
