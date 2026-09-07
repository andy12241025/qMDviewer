#include "mainwindow.h"

#include "markdowneditor.h"
#include "markdownview.h"
#include "vimhandler.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMarginsF>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QFontMetricsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QPainter>
#include <QScopedPointer>
#include <QScrollBar>
#include <QSplitter>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QTextBlock>
#include <QTextTable>
#include <QTextCursor>
#include <QTextDocument>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#  include <QStyleHints>
#endif

#ifdef QMDV_HAVE_PRINTSUPPORT
#  include <QPrintPreviewDialog>
#  include <QPrinter>
#endif

namespace {

const int kMaxRecentFiles = 10;

QString fileFilter()
{
    return MainWindow::tr("Markdown files (*.md *.markdown *.mdown *.mkd *.mkdn *.mdwn);;"
                          "Text files (*.txt);;All files (*)");
}

bool paletteIsDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

QPalette darkPalette()
{
    const QColor window(0x25, 0x2a, 0x31);
    const QColor base(0x1c, 0x20, 0x27);
    const QColor text(0xd4, 0xda, 0xe0);
    const QColor dimmed(0x77, 0x7f, 0x8a);

    QPalette pal;
    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, QColor(0x2b, 0x31, 0x39));
    pal.setColor(QPalette::ToolTipBase, window);
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::PlaceholderText, dimmed);
    pal.setColor(QPalette::Button, window);
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
    pal.setColor(QPalette::Link, QColor(0x6c, 0xb6, 0xff));
    pal.setColor(QPalette::LinkVisited, QColor(0xa2, 0x8b, 0xff));
    pal.setColor(QPalette::Highlight, QColor(0x2f, 0x5d, 0x94));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    for (QPalette::ColorRole role : { QPalette::WindowText, QPalette::Text,
                                      QPalette::ButtonText, QPalette::Link })
        pal.setColor(QPalette::Disabled, role, dimmed);
    return pal;
}

QIcon themedIcon(const QString &name, QStyle::StandardPixmap fallback)
{
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull())
        icon = QApplication::style()->standardIcon(fallback);
    return icon;
}

QString welcomeDocument()
{
    return MainWindow::tr(
        "# qMDviewer\n\n"
        "A lightweight Markdown viewer.\n\n"
        "## Getting started\n\n"
        "- **Ctrl+O** - open a file\n"
        "- Drag a `.md` file onto this window\n"
        "- Pass a path on the command line: `qmdviewer notes.md`\n\n"
        "## Handy keys\n\n"
        "| Shortcut | Action |\n"
        "| --- | --- |\n"
        "| `Ctrl+F` | Find; Enter keeps the match, `F3` repeats |\n"
        "| `F5` | Reload |\n"
        "| `Ctrl+ +` / `Ctrl+ -` / `Ctrl+0` | Zoom in / out / reset |\n"
        "| `Alt+Left` / `Alt+Right` | Back / forward between linked files |\n"
        "| `Ctrl+Shift+O` | Toggle the outline panel |\n"
        "| `Ctrl+M` | Hide or show the menu bar |\n"
        "| `Ctrl+Shift+B` | Keep or re-flow single line breaks |\n"
        "| `Ctrl+E` | Show or hide the source editor |\n"
        "| `Ctrl+Shift+V` | Vim mode: modal editing, plus `j`/`k`, `gg`/`G`, "
        "`Ctrl+F`/`Ctrl+B`, `]]`/`[[` and `*`/`#` here in the preview |\n\n"
        "Fenced code is syntax coloured when the fence names a language "
        "(```` ```cpp ````, `sh`, `perl`, `python`, `sql`, `yaml`, `json`, `diff` and more); "
        "plain fences are left alone so log excerpts keep their own look.\n\n"
        "The file is watched on disk, so edits in your editor show up here "
        "immediately with your scroll position kept.\n\n"
        "Right-click the document for these commands when the menu bar is hidden.\n");
}

} // namespace

QIcon MainWindow::applicationIcon()
{
    QIcon icon;
    for (int size : { 16, 24, 32, 48, 64, 128, 256 })
        icon.addFile(QStringLiteral(":/icons/qmdviewer-%1.png").arg(size));
    return icon;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_systemPalette(QApplication::palette())
{
    m_systemStyleName = QApplication::style()->objectName();
    setWindowIcon(applicationIcon());

    setAcceptDrops(true);
    setMinimumSize(480, 360);
    resize(1000, 760);

    buildUi();
    buildActions();
    loadSettings();
    applyTheme();

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_themeMode == SystemTheme)
            applyTheme();
    });
#endif

    m_view->showMarkdown(welcomeDocument(), QString(), tr("Welcome"));
    setWindowTitle(tr("Welcome"));
    updateNavigationActions();
    updateStatus();
}

// --------------------------------------------------------------------------
// construction
// --------------------------------------------------------------------------

void MainWindow::buildUi()
{
    m_view = new MarkdownView(this);
    connect(m_view, &MarkdownView::markdownLinkActivated,
            this, &MainWindow::openWithFeedback);
    connect(m_view, &MarkdownView::documentRendered, this, &MainWindow::refreshOutline);
    connect(m_view, &MarkdownView::zoomChanged, this, [this](int percent) { applyZoom(percent); });
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &MainWindow::syncOutlineToViewport);
    // The preview's vim handler leaves searching to the find bar, so the term
    // it repeats is the one on screen.
    VimHandler *viewVim = m_view->vim();
    connect(viewVim, &VimHandler::findRequested, this, &MainWindow::toggleFindBar);
    connect(viewVim, &VimHandler::findNextRequested, this, [this](bool backwards) {
        if (!m_findEdit->text().isEmpty())
            find(backwards);
    });
    connect(viewVim, &VimHandler::searchWordRequested, this,
            [this](const QString &word, bool backwards) {
                // Set the field without its live search, then jump ourselves.
                const bool blocked = m_findEdit->blockSignals(true);
                m_findEdit->setText(word);
                m_findEdit->blockSignals(blocked);
                find(backwards, true);
                statusBar()->showMessage(tr("Searching for \"%1\"").arg(word), 3000);
            });
    connect(viewVim, &VimHandler::message, this, [this](const QString &text) {
        statusBar()->showMessage(text, 4000);
    });
    connect(viewVim, &VimHandler::quitRequested, this, [this](bool force) {
        if (force)
            m_editorDirty = false;
        close();
    });
    // The preview has no status line of its own, so pending keys and the ":"
    // command line show up in the status bar.
    connect(viewVim, &VimHandler::statusChanged, this, [this](const QString &text) {
        if (text != QStringLiteral("-- NORMAL --") && !text.isEmpty())
            statusBar()->showMessage(text, 4000);
    });
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested,
            this, &MainWindow::showViewContextMenu);

    // Only the pane is hidden; hiding the editor itself would keep it hidden
    // when the pane is shown again.
    m_editor = new MarkdownEditor(this);
    // contentsChange rather than textChanged: the syntax highlighter emits the
    // latter for format-only changes, which would mark the file dirty on its
    // own and pop an "unsaved changes" prompt out of nowhere.
    connect(m_editor, &MarkdownEditor::zoomChanged, this,
            [this](int percent) { applyZoom(percent); });
    connect(m_editor->document(), &QTextDocument::contentsChange, this,
            [this](int, int charsRemoved, int charsAdded) {
                if (charsRemoved == 0 && charsAdded == 0)
                    return;
                onEditorChanged();
            });

    m_vimStatus = new QLabel(this);
    m_vimStatus->setContentsMargins(8, 3, 8, 3);
    m_vimStatus->setAutoFillBackground(true);
    QFont statusFont = m_vimStatus->font();
    statusFont.setBold(true);
    m_vimStatus->setFont(statusFont);
    m_vimStatus->hide();
    VimHandler *vim = m_editor->vim();
    connect(vim, &VimHandler::statusChanged, m_vimStatus, &QLabel::setText);
    connect(vim, &VimHandler::saveRequested, this, &MainWindow::saveFile);
    connect(vim, &VimHandler::reloadRequested, this, &MainWindow::reload);
    connect(vim, &VimHandler::quitRequested, this, [this](bool force) {
        if (force)
            m_editorDirty = false;
        close();
    });
    connect(vim, &VimHandler::lineNumbersRequested, m_editor,
            &MarkdownEditor::setLineNumbersVisible);
    connect(vim, &VimHandler::message, this, [this](const QString &text) {
        statusBar()->showMessage(text, 4000);
    });

    QWidget *editorPane = new QWidget(this);
    QVBoxLayout *editorLayout = new QVBoxLayout(editorPane);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);
    editorLayout->addWidget(m_editor, 1);
    editorLayout->addWidget(m_vimStatus);
    editorPane->hide();

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(editorPane);
    m_splitter->addWidget(m_view);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setChildrenCollapsible(false);

    buildFindBar();

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_splitter, 1);
    layout->addWidget(m_findBar);
    setCentralWidget(central);

    // Typing re-renders the preview, but not on every keystroke.
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(250);
    connect(m_previewTimer, &QTimer::timeout, this, [this] {
        m_view->setSourceText(m_editor->toPlainText());
    });

    m_outline = new QTreeWidget(this);
    m_outline->setHeaderHidden(true);
    m_outline->setIndentation(12);
    m_outline->setUniformRowHeights(true);
    m_outline->setFrameShape(QFrame::NoFrame);
    m_outline->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_outline, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        if (item)
            m_view->goToPosition(item->data(0, Qt::UserRole).toInt());
    });

    m_outlineDock = new QDockWidget(tr("Outline"), this);
    m_outlineDock->setObjectName(QStringLiteral("outlineDock"));
    m_outlineDock->setWidget(m_outline);
    m_outlineDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_outlineDock);
    resizeDocks({ m_outlineDock }, { 230 }, Qt::Horizontal);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_zoomLabel = new QLabel(this);
    statusBar()->addWidget(m_pathLabel, 1);
    statusBar()->addPermanentWidget(m_zoomLabel);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::onFileChanged);

    // Editors often truncate then rewrite, so coalesce bursts of events.
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(150);
    connect(m_reloadTimer, &QTimer::timeout, this, &MainWindow::reload);
}

void MainWindow::buildFindBar()
{
    m_findBar = new QWidget(this);
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(tr("Find in document"));
    m_findEdit->setClearButtonEnabled(true);
    m_findCaseSensitive = new QCheckBox(tr("Match case"), m_findBar);

    QToolButton *previous = new QToolButton(m_findBar);
    previous->setText(tr("Previous"));
    previous->setToolTip(tr("Previous match (Shift+F3)"));
    previous->setAutoRaise(true);

    QToolButton *next = new QToolButton(m_findBar);
    next->setText(tr("Next"));
    next->setToolTip(tr("Next match (F3)"));
    next->setAutoRaise(true);

    QToolButton *close = new QToolButton(m_findBar);
    close->setIcon(themedIcon(QStringLiteral("window-close"), QStyle::SP_DialogCloseButton));
    close->setToolTip(tr("Close (Esc)"));
    close->setAutoRaise(true);

    QHBoxLayout *layout = new QHBoxLayout(m_findBar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(new QLabel(tr("Find:"), m_findBar));
    layout->addWidget(m_findEdit, 1);
    layout->addWidget(previous);
    layout->addWidget(next);
    layout->addWidget(m_findCaseSensitive);
    layout->addWidget(close);

    connect(m_findEdit, &QLineEdit::textChanged, this, [this] { find(false, true); });
    connect(m_findEdit, &QLineEdit::returnPressed, this, &MainWindow::acceptFind);
    connect(previous, &QToolButton::clicked, this, &MainWindow::findPrevious);
    connect(next, &QToolButton::clicked, this, &MainWindow::findNext);
    connect(m_findCaseSensitive, &QCheckBox::toggled, this, [this] { find(false, true); });
    connect(close, &QToolButton::clicked, this, [this] { setFindBarVisible(false); });

    m_findBar->hide();

    // Enabled only while the find bar is up: an always-live Esc shortcut would
    // swallow the key everywhere, including in the editor's vim mode.
    m_escapeAction = new QAction(this);
    m_escapeAction->setShortcut(QKeySequence(QStringLiteral("Esc")));
    m_escapeAction->setEnabled(false);
    connect(m_escapeAction, &QAction::triggered, this, [this] { setFindBarVisible(false); });
    addAction(m_escapeAction);
}

void MainWindow::buildActions()
{
    // Actions are created by hand because QMenu::addAction()'s convenience
    // overloads differ between Qt 5 and Qt 6.
    auto add = [this](QMenu *menu, const QString &text, const QKeySequence &shortcut,
                      auto receiver, auto slot, const QIcon &icon = QIcon()) {
        QAction *action = new QAction(icon, text, this);
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        connect(action, &QAction::triggered, receiver, slot);
        menu->addAction(action);
        // Also owned by the window so the shortcut keeps working when the menu
        // bar is hidden.
        addAction(action);
        return action;
    };

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *open = add(fileMenu, tr("&Open..."), QKeySequence::Open,
                        this, &MainWindow::chooseFile,
                        themedIcon(QStringLiteral("document-open"), QStyle::SP_DialogOpenButton));

    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));

    m_reloadAction = add(fileMenu, tr("&Reload"), QKeySequence(),
                         this, &MainWindow::reload,
                         themedIcon(QStringLiteral("view-refresh"), QStyle::SP_BrowserReload));
    m_reloadAction->setShortcuts({ QKeySequence::Refresh, QKeySequence(QStringLiteral("Ctrl+R")) });

    m_autoReloadAction = new QAction(tr("Reload on &Change"), this);
    m_autoReloadAction->setCheckable(true);
    m_autoReloadAction->setChecked(true);
    fileMenu->addAction(m_autoReloadAction);

    m_saveAction = add(fileMenu, tr("&Save"), QKeySequence::Save, this, &MainWindow::saveFile);
    add(fileMenu, tr("Save &As..."), QKeySequence::SaveAs, this, &MainWindow::saveFileAs);

    fileMenu->addSeparator();
    add(fileMenu, tr("Export as &PDF..."), QKeySequence(), this, &MainWindow::exportPdf);
    m_printAction = add(fileMenu, tr("&Print..."), QKeySequence::Print, this, &MainWindow::print);

    fileMenu->addSeparator();
    QAction *quit = add(fileMenu, tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    quit->setMenuRole(QAction::QuitRole);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    add(editMenu, tr("&Copy"), QKeySequence::Copy, m_view, &QTextEdit::copy);
    add(editMenu, tr("Select &All"), QKeySequence::SelectAll, m_view, &QTextEdit::selectAll);
    editMenu->addSeparator();
    add(editMenu, tr("&Find..."), QKeySequence::Find, this, &MainWindow::toggleFindBar);
    add(editMenu, tr("Find &Next"), QKeySequence::FindNext, this, &MainWindow::findNext);
    add(editMenu, tr("Find &Previous"), QKeySequence::FindPrevious, this, &MainWindow::findPrevious);

    QMenu *editMenu2 = menuBar()->addMenu(tr("&Edit Source"));
    m_editAction = new QAction(tr("&Edit Mode"), this);
    m_editAction->setCheckable(true);
    m_editAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(m_editAction, &QAction::toggled, this, &MainWindow::setEditorVisible);
    editMenu2->addAction(m_editAction);
    addAction(m_editAction);

    m_vimAction = new QAction(tr("&Vim Mode"), this);
    m_vimAction->setCheckable(true);
    m_vimAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    m_vimAction->setStatusTip(tr("Modal editing in the editor, and vim navigation keys "
                                 "in the preview"));
    connect(m_vimAction, &QAction::toggled, this, &MainWindow::setVimEnabled);
    editMenu2->addAction(m_vimAction);
    addAction(m_vimAction);

    QAction *lineNumbers = new QAction(tr("Line &Numbers"), this);
    lineNumbers->setCheckable(true);
    lineNumbers->setChecked(true);
    connect(lineNumbers, &QAction::toggled, this, [this](bool on) {
        m_editor->setLineNumbersVisible(on);
    });
    editMenu2->addAction(lineNumbers);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *zoomInAction = add(viewMenu, tr("Zoom &In"), QKeySequence(), this, &MainWindow::zoomIn);
    zoomInAction->setShortcuts({ QKeySequence::ZoomIn, QKeySequence(QStringLiteral("Ctrl+=")) });
    add(viewMenu, tr("Zoom &Out"), QKeySequence::ZoomOut, this, &MainWindow::zoomOut);
    add(viewMenu, tr("&Actual Size"), QKeySequence(QStringLiteral("Ctrl+0")),
        this, &MainWindow::zoomReset);

    viewMenu->addSeparator();
    QAction *outlineToggle = m_outlineDock->toggleViewAction();
    outlineToggle->setText(tr("&Outline"));
    outlineToggle->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    viewMenu->addAction(outlineToggle);
    addAction(outlineToggle);

    m_toolBarAction = new QAction(tr("&Toolbar"), this);
    m_toolBarAction->setCheckable(true);
    m_toolBarAction->setChecked(true);
    connect(m_toolBarAction, &QAction::toggled, this, &MainWindow::setToolBarVisible);
    viewMenu->addAction(m_toolBarAction);
    addAction(m_toolBarAction);

    m_statusBarAction = new QAction(tr("&Status Bar"), this);
    m_statusBarAction->setCheckable(true);
    m_statusBarAction->setChecked(true);
    connect(m_statusBarAction, &QAction::toggled, this, &MainWindow::setStatusBarVisible);
    viewMenu->addAction(m_statusBarAction);
    addAction(m_statusBarAction);

    m_menuBarAction = new QAction(tr("&Menu Bar"), this);
    m_menuBarAction->setCheckable(true);
    m_menuBarAction->setChecked(true);
    m_menuBarAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    connect(m_menuBarAction, &QAction::toggled, this, &MainWindow::setMenuBarVisible);
    viewMenu->addAction(m_menuBarAction);
    addAction(m_menuBarAction);

    viewMenu->addSeparator();
    m_hardBreaksAction = new QAction(tr("&Keep Single Line Breaks"), this);
    m_hardBreaksAction->setCheckable(true);
    m_hardBreaksAction->setChecked(true);
    m_hardBreaksAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")));
    m_hardBreaksAction->setStatusTip(tr("Render every newline in the source as a line break, "
                                        "instead of re-flowing the paragraph"));
    connect(m_hardBreaksAction, &QAction::toggled, this, [this](bool on) {
        m_view->setHardLineBreaks(on);
    });
    viewMenu->addAction(m_hardBreaksAction);
    addAction(m_hardBreaksAction);

    viewMenu->addSeparator();
    QMenu *themeMenu = viewMenu->addMenu(tr("&Theme"));
    QActionGroup *themeGroup = new QActionGroup(this);
    const QStringList themeNames = { tr("Follow &System"), tr("&Light"), tr("&Dark") };
    for (int i = 0; i < 3; ++i) {
        QAction *action = themeMenu->addAction(themeNames.at(i));
        action->setCheckable(true);
        themeGroup->addAction(action);
        m_themeActions[i] = action;
        connect(action, &QAction::triggered, this, [this, i] {
            m_themeMode = static_cast<ThemeMode>(i);
            applyTheme();
        });
    }
    m_themeActions[0]->setChecked(true);

    QMenu *goMenu = menuBar()->addMenu(tr("&Go"));
    m_backAction = add(goMenu, tr("&Back"), QKeySequence::Back, this, &MainWindow::goBack,
                       themedIcon(QStringLiteral("go-previous"), QStyle::SP_ArrowBack));
    m_forwardAction = add(goMenu, tr("&Forward"), QKeySequence::Forward,
                          this, &MainWindow::goForward,
                          themedIcon(QStringLiteral("go-next"), QStyle::SP_ArrowForward));

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    add(helpMenu, tr("&About"), QKeySequence(), this, [this] {
        QMessageBox::about(this, tr("About qMDviewer"),
            tr("<h3>qMDviewer %1</h3>"
               "<p>A lightweight Markdown viewer built on Qt's own Markdown "
               "renderer - no third-party dependencies.</p>"
               "<p>Running on Qt %2.</p>")
                .arg(QStringLiteral(QMDV_VERSION), QString::fromLatin1(qVersion())));
    });

    QToolBar *toolBar = addToolBar(tr("Main"));
    m_toolBar = toolBar;
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolBar->addAction(open);
    toolBar->addAction(m_reloadAction);
    toolBar->addSeparator();
    toolBar->addAction(m_backAction);
    toolBar->addAction(m_forwardAction);

    updateRecentMenu();
}

// --------------------------------------------------------------------------
// loading
// --------------------------------------------------------------------------

bool MainWindow::openFile(const QString &path)
{
    return loadPath(path, true);
}

void MainWindow::showText(const QString &markdown, const QString &title)
{
    m_view->showMarkdown(markdown, QDir::currentPath(), title);
    setWindowTitle(title);
    watchCurrentFile();
    updateStatus();
}

bool MainWindow::loadPath(const QString &path, bool recordHistory)
{
    if (!confirmDiscardChanges())
        return true; // cancelled, but not a failure to report
    const QFileInfo info(path);
    const QString absolute = info.absoluteFilePath();
    if (!m_view->loadFile(absolute))
        return false;

    if (recordHistory) {
        const bool alreadyCurrent = m_historyIndex >= 0
                && m_historyIndex < m_history.size()
                && m_history.at(m_historyIndex) == absolute;
        if (!alreadyCurrent) {
            m_history = m_history.mid(0, m_historyIndex + 1);
            m_history.append(absolute);
            m_historyIndex = m_history.size() - 1;
        }
    }

    m_editorDirty = false;
    if (m_editAction && m_editAction->isChecked()) {
        m_syncingEditor = true;
        m_editor->setPlainText(m_view->sourceText());
        m_syncingEditor = false;
    }
    updateWindowTitle();
    m_lastModified = info.lastModified();
    m_lastSize = info.size();
    rememberRecent(absolute);
    watchCurrentFile();
    updateNavigationActions();
    updateStatus();
    m_view->setFocus();
    return true;
}

void MainWindow::openWithFeedback(const QString &path)
{
    if (!loadPath(path, true)) {
        QMessageBox::warning(this, tr("qMDviewer"),
                             tr("Cannot read %1").arg(QDir::toNativeSeparators(path)));
    }
}

void MainWindow::chooseFile()
{
    QString startDir = QDir::currentPath();
    if (!m_view->filePath().isEmpty())
        startDir = QFileInfo(m_view->filePath()).absolutePath();
    else if (!m_recentFiles.isEmpty())
        startDir = QFileInfo(m_recentFiles.first()).absolutePath();

    const QString path = QFileDialog::getOpenFileName(this, tr("Open Markdown File"),
                                                      startDir, fileFilter());
    if (!path.isEmpty())
        openWithFeedback(path);
}

void MainWindow::reload()
{
    const QString path = m_view->filePath();
    if (path.isEmpty())
        return;

    if (!m_view->loadFile(path, true)) {
        statusBar()->showMessage(tr("Could not reload %1")
                                     .arg(QDir::toNativeSeparators(path)), 4000);
        return;
    }

    const QFileInfo info(path);
    m_lastModified = info.lastModified();
    m_lastSize = info.size();
    watchCurrentFile(); // a save may have replaced the file and dropped the watch
    if (m_editAction && m_editAction->isChecked()) {
        m_syncingEditor = true;
        m_editor->setPlainText(m_view->sourceText());
        m_syncingEditor = false;
        m_editorDirty = false;
        updateWindowTitle();
    }
    updateStatus();
    statusBar()->showMessage(tr("Reloaded at %1")
                                 .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))),
                             2500);
}

void MainWindow::onFileChanged()
{
    const QString path = m_view->filePath();
    if (path.isEmpty())
        return;

    if (!m_autoReloadAction->isChecked()) {
        watchCurrentFile();
        return;
    }

    const QFileInfo info(path);
    if (!info.exists() || (info.lastModified() == m_lastModified && info.size() == m_lastSize)) {
        watchCurrentFile();
        return;
    }
    if (m_editorDirty) {
        statusBar()->showMessage(tr("%1 changed on disk; your edits are kept. Press F5 to "
                                    "discard them and reload.")
                                     .arg(QFileInfo(path).fileName()), 8000);
        watchCurrentFile();
        return;
    }
    m_reloadTimer->start();
}

void MainWindow::watchCurrentFile()
{
    const QStringList watched = m_watcher->files() + m_watcher->directories();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);

    const QString path = m_view->filePath();
    if (path.isEmpty())
        return;

    m_watcher->addPath(path);
    // Watching the directory too catches editors that save by replacing.
    m_watcher->addPath(QFileInfo(path).absolutePath());
}

// --------------------------------------------------------------------------
// navigation
// --------------------------------------------------------------------------

void MainWindow::goBack()
{
    if (m_historyIndex <= 0)
        return;
    --m_historyIndex;
    if (!loadPath(m_history.at(m_historyIndex), false))
        m_history.removeAt(m_historyIndex);
    updateNavigationActions();
}

void MainWindow::goForward()
{
    if (m_historyIndex + 1 >= m_history.size())
        return;
    ++m_historyIndex;
    if (!loadPath(m_history.at(m_historyIndex), false))
        m_history.removeAt(m_historyIndex);
    updateNavigationActions();
}

void MainWindow::updateNavigationActions()
{
    m_backAction->setEnabled(m_historyIndex > 0);
    m_forwardAction->setEnabled(m_historyIndex + 1 < m_history.size());
}

void MainWindow::rememberRecent(const QString &path)
{
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > kMaxRecentFiles)
        m_recentFiles.removeLast();
    updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();
    m_recentMenu->setEnabled(!m_recentFiles.isEmpty());
    if (m_recentFiles.isEmpty())
        return;

    for (const QString &path : m_recentFiles) {
        QAction *action = new QAction(QDir::toNativeSeparators(path), this);
        connect(action, &QAction::triggered, this, [this, path] { openWithFeedback(path); });
        m_recentMenu->addAction(action);
    }

    m_recentMenu->addSeparator();
    QAction *clear = new QAction(tr("Clear List"), this);
    connect(clear, &QAction::triggered, this, [this] {
        m_recentFiles.clear();
        updateRecentMenu();
    });
    m_recentMenu->addAction(clear);
}

// --------------------------------------------------------------------------
// outline
// --------------------------------------------------------------------------

void MainWindow::refreshOutline()
{
    m_outline->clear();

    const QVector<MarkdownView::Heading> headings = m_view->headings();
    QVector<QPair<int, QTreeWidgetItem *>> stack;

    for (const MarkdownView::Heading &heading : headings) {
        while (!stack.isEmpty() && stack.last().first >= heading.level)
            stack.removeLast();

        QTreeWidgetItem *item = stack.isEmpty() ? new QTreeWidgetItem(m_outline)
                                                : new QTreeWidgetItem(stack.last().second);
        item->setText(0, heading.text);
        item->setToolTip(0, heading.text);
        item->setData(0, Qt::UserRole, heading.position);
        stack.append(qMakePair(heading.level, item));
    }

    m_outline->expandAll();
    syncOutlineToViewport();
}

void MainWindow::syncOutlineToViewport()
{
    if (m_syncingOutline || !m_outlineDock->isVisible() || m_outline->topLevelItemCount() == 0)
        return;

    const int position = m_view->headingAtViewportTop();
    if (position < 0)
        return;

    m_syncingOutline = true;
    for (QTreeWidgetItemIterator it(m_outline); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toInt() == position) {
            m_outline->setCurrentItem(*it);
            m_outline->scrollToItem(*it);
            break;
        }
    }
    m_syncingOutline = false;
}

// --------------------------------------------------------------------------
// find
// --------------------------------------------------------------------------

void MainWindow::setFindBarVisible(bool visible)
{
    m_findBar->setVisible(visible);
    m_escapeAction->setEnabled(visible);
    if (visible) {
        m_findEdit->setFocus();
        m_findEdit->selectAll();
    } else {
        m_view->setFocus();
    }
}

void MainWindow::toggleFindBar()
{
    setFindBarVisible(true);
}

// Enter means "this is the match I wanted": keep it selected, close the bar and
// hand the keyboard back to the document. Searching again here would skip past
// the match the search-as-you-type already found.
void MainWindow::acceptFind()
{
    if (m_findEdit->text().isEmpty()) {
        setFindBarVisible(false);
        return;
    }
    if (!m_view->textCursor().hasSelection())
        find(false, true);

    if (!m_view->textCursor().hasSelection()) {
        setFindFeedback(false);
        return; // nothing to jump to, so stay put
    }
    m_view->ensureCursorVisible();
    setFindBarVisible(false);
}

// F3 and Shift+F3 keep cycling matches after Enter has closed the bar; they
// only open it when there is nothing to repeat.
void MainWindow::findNext()
{
    if (m_findEdit->text().isEmpty()) {
        toggleFindBar();
        return;
    }
    find(false);
}

void MainWindow::findPrevious()
{
    if (m_findEdit->text().isEmpty()) {
        toggleFindBar();
        return;
    }
    find(true);
}

void MainWindow::find(bool backwards, bool fromStartOfSelection)
{
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        QTextCursor cursor = m_view->textCursor();
        cursor.clearSelection();
        m_view->setTextCursor(cursor);
        setFindFeedback(true);
        return;
    }

    QTextDocument::FindFlags flags;
    if (backwards)
        flags |= QTextDocument::FindBackward;
    if (m_findCaseSensitive->isChecked())
        flags |= QTextDocument::FindCaseSensitively;

    if (fromStartOfSelection) {
        // Searching as you type should not skip the current match.
        QTextCursor cursor = m_view->textCursor();
        if (cursor.hasSelection()) {
            cursor.setPosition(cursor.selectionStart());
            m_view->setTextCursor(cursor);
        }
    }

    bool found = m_view->find(needle, flags);
    if (!found) {
        QTextCursor cursor = m_view->textCursor();
        cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        m_view->setTextCursor(cursor);
        found = m_view->find(needle, flags);
        if (found)
            statusBar()->showMessage(tr("Search wrapped around"), 1500);
    }
    setFindFeedback(found);
}

void MainWindow::setFindFeedback(bool found)
{
    m_findEdit->setStyleSheet(found ? QString()
                                    : QStringLiteral("QLineEdit { color: #d04437; }"));
}

// --------------------------------------------------------------------------
// view options
// --------------------------------------------------------------------------

// Both panes share one zoom level, so Ctrl+/Ctrl- and Ctrl+wheel do the same
// thing wherever the focus happens to be. Changing the editor's font is
// immediate; the preview coalesces its own re-layout internally.
void MainWindow::applyZoom(int percent)
{
    if (m_applyingZoom)
        return;
    const int clamped = qBound(50, percent, 400);
    if (clamped == m_zoomPercent)
        return;

    m_applyingZoom = true;
    m_zoomPercent = clamped;
    m_view->setZoomPercent(clamped);
    m_editor->setZoomPercent(clamped);
    m_applyingZoom = false;
    updateStatus();
}

void MainWindow::zoomIn()
{
    applyZoom(m_zoomPercent + 10);
}

void MainWindow::zoomOut()
{
    applyZoom(m_zoomPercent - 10);
}

void MainWindow::zoomReset()
{
    applyZoom(100);
}

bool MainWindow::systemIsDark() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
#endif
    // Not QApplication::palette(): the dark theme overwrites it, so asking the
    // live palette would just report back whichever theme is already active.
    return paletteIsDark(m_systemPalette);
}

void MainWindow::setMenuBarVisible(bool visible)
{
    menuBar()->setVisible(visible);
    if (!visible) {
        statusBar()->showMessage(tr("Menu bar hidden - press %1 to bring it back, "
                                    "or right-click the document")
                                     .arg(m_menuBarAction->shortcut().toString(
                                         QKeySequence::NativeText)),
                                 6000);
    }
}

void MainWindow::setToolBarVisible(bool visible)
{
    if (m_toolBar)
        m_toolBar->setVisible(visible);
}

void MainWindow::setStatusBarVisible(bool visible)
{
    statusBar()->setVisible(visible);
}

// The document's own context menu doubles as the way back when the menu bar
// and toolbar are hidden.
void MainWindow::showViewContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    QAction *copy = menu.addAction(tr("&Copy"));
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(m_view->textCursor().hasSelection());
    connect(copy, &QAction::triggered, m_view, &QTextEdit::copy);

    QAction *selectAll = menu.addAction(tr("Select &All"));
    selectAll->setShortcut(QKeySequence::SelectAll);
    connect(selectAll, &QAction::triggered, m_view, &QTextEdit::selectAll);

    menu.addSeparator();
    QAction *find = menu.addAction(tr("&Find..."));
    find->setShortcut(QKeySequence::Find);
    connect(find, &QAction::triggered, this, &MainWindow::toggleFindBar);

    QAction *reload = menu.addAction(tr("&Reload"));
    reload->setEnabled(!m_view->filePath().isEmpty());
    connect(reload, &QAction::triggered, this, &MainWindow::reload);

    menu.addSeparator();
    menu.addAction(m_editAction);
    menu.addSeparator();
    menu.addAction(m_menuBarAction);
    menu.addAction(m_toolBarAction);
    menu.addAction(m_statusBarAction);
    menu.addAction(m_outlineDock->toggleViewAction());

    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void MainWindow::setEditorVisible(bool visible)
{
    QWidget *pane = m_editor->parentWidget();
    if (visible) {
        m_syncingEditor = true;
        m_editor->setPlainText(m_view->sourceText());
        m_syncingEditor = false;
        m_editorDirty = false;
        pane->show();
        if (m_splitter->sizes().value(0) == 0)
            m_splitter->setSizes({ width() / 2, width() / 2 });
        m_editor->setFocus();
    } else {
        pane->hide();
        m_view->setFocus();
    }
    m_vimStatus->setVisible(visible && m_editor->isVimEnabled());
    m_saveAction->setEnabled(visible);
    updateWindowTitle();
}

void MainWindow::setVimEnabled(bool enabled)
{
    m_editor->setVimEnabled(enabled);
    // The preview is read-only, so it gets the navigation keys only.
    m_view->setVimNavigation(enabled);
    m_vimStatus->setVisible(enabled && m_editAction->isChecked());
    if (enabled) {
        statusBar()->showMessage(tr("Vim mode on: j/k, Ctrl+F/B, gg/G and ]]/[[ navigate; "
                                    "\"/\" searches (Ctrl+F pages instead); "
                                    "in the editor i inserts, :w saves, :q quits"),
                                 8000);
    }
}

void MainWindow::onEditorChanged()
{
    // Nobody can type into a hidden editor, so a change there is ours.
    if (m_syncingEditor || !m_editAction || !m_editAction->isChecked())
        return;
    m_editorDirty = true;
    updateWindowTitle();
    m_previewTimer->start();
}

void MainWindow::saveFile()
{
    if (m_view->filePath().isEmpty()) {
        saveFileAs();
        return;
    }
    // Whatever the editor holds is the document, even if the preview has not
    // caught up with the last keystroke yet.
    const QString text = m_editAction->isChecked() ? m_editor->toPlainText()
                                                   : m_view->sourceText();
    QFile file(m_view->filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("qMDviewer"),
                             tr("Cannot write %1")
                                 .arg(QDir::toNativeSeparators(m_view->filePath())));
        return;
    }
    file.write(text.toUtf8());
    file.close();

    m_editorDirty = false;
    // Our own write must not trigger the watcher's reload.
    const QFileInfo info(m_view->filePath());
    m_lastModified = info.lastModified();
    m_lastSize = info.size();
    m_previewTimer->stop();
    m_view->setSourceText(text);
    updateWindowTitle();
    updateStatus();
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(m_view->filePath())),
                             3000);
}

void MainWindow::saveFileAs()
{
    const QString suggestion = m_view->filePath().isEmpty()
            ? QDir::currentPath() + QStringLiteral("/untitled.md")
            : m_view->filePath();
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Markdown File"), suggestion,
                                                      fileFilter());
    if (path.isEmpty())
        return;

    const QString text = m_editAction->isChecked() ? m_editor->toPlainText()
                                                   : m_view->sourceText();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("qMDviewer"),
                             tr("Cannot write %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    file.write(text.toUtf8());
    file.close();
    m_editorDirty = false;
    loadPath(path, true);
}

// Returns true when the caller may go ahead and throw the edits away.
bool MainWindow::confirmDiscardChanges()
{
    if (!m_editorDirty)
        return true;

    const QString name = m_view->displayName().isEmpty() ? tr("The document")
                                                         : m_view->displayName();
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Unsaved changes"));
    box.setText(tr("%1 has unsaved changes.").arg(name));
    box.setInformativeText(tr("Save them before continuing?"));
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);
    box.setEscapeButton(box.button(QMessageBox::Cancel));

    switch (box.exec()) {
    case QMessageBox::Save:
        saveFile();
        // A failed write must not silently lose the changes.
        return !m_editorDirty;
    case QMessageBox::Discard:
        // Dropping the changes is the whole point, so stop reporting them.
        m_editorDirty = false;
        updateWindowTitle();
        return true;
    default:
        return false; // Cancel
    }
}

void MainWindow::updateWindowTitle()
{
    QString name = m_view->displayName();
    if (name.isEmpty())
        name = tr("Untitled");
    setWindowTitle(m_editorDirty ? name + QLatin1String(" *") : name);
}

void MainWindow::applyTheme()
{
    const bool dark = m_themeMode == DarkTheme
            || (m_themeMode == SystemTheme && systemIsDark());

    m_applyingTheme = true;
    if (dark) {
        // Fusion honours custom palettes on every platform; native styles do not.
        if (QApplication::style()->objectName().compare(QLatin1String("fusion"),
                                                        Qt::CaseInsensitive) != 0) {
            if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
                QApplication::setStyle(fusion);
        }
        QApplication::setPalette(darkPalette());
    } else {
        if (QApplication::style()->objectName() != m_systemStyleName) {
            if (QStyle *original = QStyleFactory::create(m_systemStyleName))
                QApplication::setStyle(original);
        }
        QApplication::setPalette(m_systemPalette);
    }
    m_applyingTheme = false;
    m_darkActive = dark;

    m_view->setDarkMode(dark);
    m_editor->setDarkMode(dark);
}

void MainWindow::changeEvent(QEvent *event)
{
    // Keep track of the desktop's own palette so "Follow System" still works
    // after the dark theme has replaced the application palette. Ignored while
    // we are the ones changing it, and while dark is active (the live palette
    // is ours, not the system's).
    if (event->type() == QEvent::ApplicationPaletteChange && !m_applyingTheme && !m_darkActive) {
        m_systemPalette = QApplication::palette();
        if (m_themeMode == SystemTheme)
            applyTheme();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::updateStatus()
{
    const QString path = m_view->filePath();
    if (path.isEmpty()) {
        m_pathLabel->setText(m_view->displayName());
    } else {
        const QFileInfo info(path);
        m_pathLabel->setText(tr("%1   %2   modified %3")
                                 .arg(QDir::toNativeSeparators(path),
                                      locale().formattedDataSize(info.size()),
                                      info.lastModified().toString(
                                          QStringLiteral("yyyy-MM-dd HH:mm"))));
    }
    m_zoomLabel->setText(QStringLiteral("%1%").arg(m_zoomPercent));
    m_reloadAction->setEnabled(!path.isEmpty());
}

// --------------------------------------------------------------------------
// export
// --------------------------------------------------------------------------

namespace {

struct TocEntry
{
    int level = 1;
    QString title;
    int page = 1;
    qreal topInPage = 0; // device pixels from the top of its page
};

int pageOfBlock(const QTextDocument *doc, const QTextBlock &block, qreal pageHeight)
{
    if (pageHeight <= 0)
        return 1;
    const qreal top = doc->documentLayout()->blockBoundingRect(block).top();
    return int(top / pageHeight) + 1;
}

QVector<TocEntry> collectHeadings(const QTextDocument *doc, qreal pageHeight)
{
    QVector<TocEntry> entries;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const int level = block.blockFormat().headingLevel();
        const QString title = block.text().trimmed();
        if (level == 0 || title.isEmpty())
            continue;
        const int page = pageOfBlock(doc, block, pageHeight);
        const qreal top = doc->documentLayout()->blockBoundingRect(block).top()
                - (page - 1) * pageHeight;
        entries.append(TocEntry{ qBound(1, level, 6), title, page, top });
    }
    return entries;
}

// Lays the contents list out in its own document. Keeping it separate from the
// body means the body's pagination never shifts, so the page numbers stay
// correct; the two documents are simply printed one after the other.
void buildTableOfContents(QTextDocument *toc, const QVector<TocEntry> &entries,
                          int pageOffset, qreal body, const QFont &baseFont,
                          const QSizeF &pageSize, qreal unit)
{
    toc->clear();
    toc->setDefaultFont(baseFont);
    toc->setDocumentMargin(0);
    // clear() drops the page size, so it has to be restored here rather than
    // set once by the caller.
    toc->setPageSize(pageSize);

    QTextCursor cursor(toc);
    cursor.beginEditBlock();

    QTextBlockFormat titleBlock;
    titleBlock.setBottomMargin(unit * 1.4);
    QTextCharFormat titleChar;
    titleChar.setFontPointSize(body * 1.7);
    titleChar.setFontWeight(QFont::Bold);
    cursor.setBlockFormat(titleBlock);
    cursor.setCharFormat(titleChar);
    cursor.insertText(MainWindow::tr("Contents"));

    // A borderless two-column table rather than a right-aligned tab stop:
    // Qt's RightTab wraps the line instead of pulling the text to the stop.
    QTextTableFormat tableFormat;
    tableFormat.setBorder(0);
    tableFormat.setBorderStyle(QTextFrameFormat::BorderStyle_None);
    tableFormat.setCellPadding(0);
    tableFormat.setCellSpacing(0);
    // Full width, so the fixed number column sits against the right margin.
    tableFormat.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    tableFormat.setColumnWidthConstraints({
        QTextLength(QTextLength::VariableLength, 0),
        QTextLength(QTextLength::FixedLength, unit * 3.0),
    });
    QTextTable *table = cursor.insertTable(entries.size(), 2, tableFormat);

    for (int row = 0; row < entries.size(); ++row) {
        const TocEntry &entry = entries.at(row);

        QTextBlockFormat titleFormat;
        titleFormat.setLeftMargin((entry.level - 1) * unit * 1.5);
        titleFormat.setTopMargin(entry.level == 1 ? unit * 0.55 : unit * 0.12);

        QTextCharFormat charFormat;
        charFormat.setFontPointSize(body);
        charFormat.setFontWeight(entry.level == 1 ? QFont::Bold : QFont::Normal);

        QTextCursor title = table->cellAt(row, 0).firstCursorPosition();
        title.setBlockFormat(titleFormat);
        title.setCharFormat(charFormat);
        title.insertText(entry.title);

        QTextBlockFormat pageFormat = titleFormat;
        pageFormat.setLeftMargin(0);
        pageFormat.setAlignment(Qt::AlignRight);
        QTextCursor number = table->cellAt(row, 1).firstCursorPosition();
        number.setBlockFormat(pageFormat);
        number.setCharFormat(charFormat);
        number.insertText(QString::number(entry.page + pageOffset));
    }
    cursor.endEditBlock();
}

void paintPages(QPainter *painter, QPagedPaintDevice *device, QTextDocument *doc,
                const QRectF &content, int *pageNumber, int totalPages, const QFont &footerFont)
{
    const int pages = doc->pageCount();
    for (int page = 0; page < pages; ++page) {
        if (*pageNumber > 1)
            device->newPage();

        painter->save();
        painter->translate(0, -page * content.height());
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, Qt::black);
        context.clip = QRectF(0, page * content.height(), content.width(), content.height());
        doc->documentLayout()->draw(painter, context);
        painter->restore();

        painter->save();
        painter->setFont(footerFont);
        painter->setPen(QColor(0x77, 0x7f, 0x8a));
        const QFontMetricsF metrics(footerFont, painter->device());
        painter->drawText(QRectF(0, content.height(), content.width(), metrics.height() * 2.0),
                          Qt::AlignHCenter | Qt::AlignBottom,
                          MainWindow::tr("%1 / %2").arg(*pageNumber).arg(totalPages));
        painter->restore();

        ++(*pageNumber);
    }
}

} // namespace

// Renders the document to a paged device with a generated table of contents
// and page-number footers.
bool MainWindow::renderToDevice(QPagedPaintDevice *device, int resolution,
                                QVector<PdfOutline::Bookmark> *bookmarks)
{
    QPainter painter(device);
    if (!painter.isActive())
        return false;

    const QRectF paintRect = device->pageLayout().paintRectPixels(resolution);
    const qreal body = QApplication::font().pointSizeF() > 0
            ? QApplication::font().pointSizeF()
            : 10.0;
    // Block geometry is in device pixels, so distances expressed in "body text
    // sizes" have to be scaled from the screen's resolution to the device's.
    const qreal unit = body * qreal(device->logicalDpiY()) / qMax(1, logicalDpiY());

    QFont footerFont = QApplication::font();
    footerFont.setPointSizeF(body * 0.8);
    const qreal footerHeight = QFontMetricsF(footerFont, device).height() * 2.0;
    const QRectF content(0, 0, paintRect.width(), paintRect.height() - footerHeight);

    QScopedPointer<QTextDocument> doc(m_view->createPrintDocument(device, content.width()));
    doc->setPageSize(content.size());

    QScopedPointer<QTextDocument> toc;
    const QVector<TocEntry> entries = collectHeadings(doc.data(), content.height());
    if (entries.size() >= 2) {
        toc.reset(new QTextDocument);
        toc->documentLayout()->setPaintDevice(device);
        toc->setPageSize(content.size());

        QFont baseFont = QApplication::font();
        baseFont.setPointSizeF(body);
        // The offset depends on how many pages the contents itself needs, and
        // that can grow once the numbers are filled in, so settle it first.
        int offset = 0;
        for (int pass = 0; pass < 4; ++pass) {
            buildTableOfContents(toc.data(), entries, offset, body, baseFont,
                                 content.size(), unit);
            const int pages = toc->pageCount();
            if (pages == offset)
                break;
            offset = pages;
        }
    }

    const int tocPages = toc ? toc->pageCount() : 0;
    const int totalPages = doc->pageCount() + tocPages;

    if (bookmarks) {
        // PDF destinations are measured in points from the bottom of the page.
        const QRectF pagePoints = device->pageLayout().fullRectPoints();
        const QRectF contentPoints = device->pageLayout().paintRectPoints();
        const qreal pointsPerPixel = 72.0 / qMax(1, resolution);
        for (const TocEntry &entry : entries) {
            PdfOutline::Bookmark bookmark;
            bookmark.level = entry.level;
            bookmark.title = entry.title;
            bookmark.pageIndex = tocPages + entry.page - 1;
            bookmark.topPoints = pagePoints.height() - contentPoints.top()
                    - entry.topInPage * pointsPerPixel;
            bookmarks->append(bookmark);
        }
    }

    int pageNumber = 1;
    if (toc)
        paintPages(&painter, device, toc.data(), content, &pageNumber, totalPages, footerFont);
    paintPages(&painter, device, doc.data(), content, &pageNumber, totalPages, footerFont);
    return true;
}

void MainWindow::exportPdf()
{
    if (m_view->isEmpty())
        return;

    QString suggestion = QStringLiteral("document.pdf");
    if (!m_view->filePath().isEmpty()) {
        const QFileInfo info(m_view->filePath());
        suggestion = info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
                + QStringLiteral(".pdf");
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export as PDF"), suggestion,
                                                      tr("PDF documents (*.pdf)"));
    if (path.isEmpty())
        return;

    QVector<PdfOutline::Bookmark> bookmarks;
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
        writer.setResolution(300);
        writer.setTitle(m_view->displayName());
        writer.setCreator(QStringLiteral("qMDviewer"));

        if (!renderToDevice(&writer, writer.resolution(), &bookmarks)) {
            QMessageBox::warning(this, tr("qMDviewer"),
                                 tr("Could not write %1").arg(QDir::toNativeSeparators(path)));
            return;
        }
    } // the writer has to be closed before the outline is appended

    const bool outlined = PdfOutline::addTo(path, bookmarks);
    statusBar()->showMessage(outlined
                                 ? tr("Exported %1 with %n outline entries", nullptr,
                                      bookmarks.size())
                                       .arg(QDir::toNativeSeparators(path))
                                 : tr("Exported %1").arg(QDir::toNativeSeparators(path)),
                             4000);
}

#ifdef QMDV_HAVE_PRINTSUPPORT
void MainWindow::print()
{
    if (m_view->isEmpty())
        return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(m_view->displayName());

    QPrintPreviewDialog preview(&printer, this);
    preview.setWindowTitle(tr("Print Preview"));
    connect(&preview, &QPrintPreviewDialog::paintRequested, this, [this](QPrinter *target) {
        renderToDevice(target, target->resolution());
    });
    preview.exec();
}
#else
void MainWindow::print()
{
    QMessageBox::information(this, tr("Printing unavailable"),
                             tr("This build was made without Qt PrintSupport. "
                                "Use File > Export as PDF instead."));
}
#endif

// --------------------------------------------------------------------------
// settings and events
// --------------------------------------------------------------------------

void MainWindow::loadSettings()
{
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("windowState")).toByteArray());

    m_recentFiles = settings.value(QStringLiteral("recentFiles")).toStringList();
    while (m_recentFiles.size() > kMaxRecentFiles)
        m_recentFiles.removeLast();
    updateRecentMenu();

    m_autoReloadAction->setChecked(settings.value(QStringLiteral("autoReload"), true).toBool());

    const bool hardBreaks = settings.value(QStringLiteral("hardLineBreaks"), true).toBool();
    m_hardBreaksAction->setChecked(hardBreaks);
    m_view->setHardLineBreaks(hardBreaks);

    // Restored without the status hint that setMenuBarVisible() would show.
    const bool menuBarVisible = settings.value(QStringLiteral("menuBarVisible"), true).toBool();
    m_menuBarAction->setChecked(menuBarVisible);
    menuBar()->setVisible(menuBarVisible);
    m_toolBarAction->setChecked(settings.value(QStringLiteral("toolBarVisible"), true).toBool());
    m_statusBarAction->setChecked(
        settings.value(QStringLiteral("statusBarVisible"), true).toBool());
    m_vimAction->setChecked(settings.value(QStringLiteral("vimMode"), false).toBool());

    const qreal uiPoints = QApplication::font().pointSizeF() > 0
            ? QApplication::font().pointSizeF()
            : 10.0;
    m_editor->setBasePointSize(
        settings.value(QStringLiteral("editorPointSize"), uiPoints + 2.0).toDouble());

    // Applied directly rather than through applyZoom(), so the first render
    // already uses the stored zoom instead of re-rendering 180 ms later.
    m_zoomPercent = qBound(50, settings.value(QStringLiteral("zoom"), 100).toInt(), 400);
    m_view->setZoomPercent(m_zoomPercent);
    m_editor->setZoomPercent(m_zoomPercent);

    const int mode = settings.value(QStringLiteral("themeMode"), int(SystemTheme)).toInt();
    m_themeMode = static_cast<ThemeMode>(qBound(0, mode, 2));
    m_themeActions[m_themeMode]->setChecked(true);
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("windowState"), saveState());
    settings.setValue(QStringLiteral("recentFiles"), m_recentFiles);
    settings.setValue(QStringLiteral("autoReload"), m_autoReloadAction->isChecked());
    settings.setValue(QStringLiteral("zoom"), m_zoomPercent);
    settings.setValue(QStringLiteral("themeMode"), int(m_themeMode));
    settings.setValue(QStringLiteral("hardLineBreaks"), m_hardBreaksAction->isChecked());
    settings.setValue(QStringLiteral("menuBarVisible"), m_menuBarAction->isChecked());
    settings.setValue(QStringLiteral("toolBarVisible"), m_toolBarAction->isChecked());
    settings.setValue(QStringLiteral("statusBarVisible"), m_statusBarAction->isChecked());
    settings.setValue(QStringLiteral("vimMode"), m_vimAction->isChecked());
    settings.setValue(QStringLiteral("editorPointSize"), m_editor->basePointSize());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmDiscardChanges()) {
        event->ignore();
        return;
    }
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime->hasUrls())
        return;
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        event->acceptProposedAction();
        openWithFeedback(url.toLocalFile());
        return;
    }
}
