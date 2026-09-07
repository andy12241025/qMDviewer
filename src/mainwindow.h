#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "pdfoutline.h"

#include <QDateTime>
#include <QIcon>
#include <QMainWindow>
#include <QPalette>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QAction;
class QSplitter;
class QCheckBox;
class QDockWidget;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QMenu;
class QPagedPaintDevice;
class QPoint;
class QTimer;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

class MarkdownEditor;
class MarkdownView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);
    void showText(const QString &markdown, const QString &title);

    // Renders the current document onto a paged device (PDF writer or
    // printer), prefixed with a generated table of contents. When bookmarks
    // is given it is filled with the heading destinations, for the PDF
    // outline that gets appended afterwards.
    bool renderToDevice(QPagedPaintDevice *device, int resolution,
                        QVector<PdfOutline::Bookmark> *bookmarks = nullptr);

    // The multi-resolution application icon from the compiled resources.
    static QIcon applicationIcon();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void chooseFile();
    void reload();
    void goBack();
    void goForward();
    void exportPdf();
    void print();
    void toggleFindBar();
    void setFindBarVisible(bool visible);
    void acceptFind();
    void findNext();
    void findPrevious();
    void applyZoom(int percent);
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void refreshOutline();
    void syncOutlineToViewport();
    void setMenuBarVisible(bool visible);
    void setToolBarVisible(bool visible);
    void setStatusBarVisible(bool visible);
    void setEditorVisible(bool visible);
    void setVimEnabled(bool enabled);
    void saveFile();
    void saveFileAs();
    void onEditorChanged();
    void showViewContextMenu(const QPoint &pos);
    void onFileChanged();
    void applyTheme();

private:
    enum ThemeMode { SystemTheme, LightTheme, DarkTheme };

    void buildUi();
    void buildActions();
    void buildFindBar();
    void loadSettings();
    void saveSettings();

    bool loadPath(const QString &path, bool recordHistory);
    bool confirmDiscardChanges();
    void updateWindowTitle();
    void openWithFeedback(const QString &path);
    bool systemIsDark() const;
    void watchCurrentFile();
    void updateNavigationActions();
    void updateRecentMenu();
    void rememberRecent(const QString &path);
    void find(bool backwards, bool fromStartOfSelection = false);
    void setFindFeedback(bool found);
    void updateStatus();

    MarkdownView *m_view = nullptr;
    MarkdownEditor *m_editor = nullptr;
    QSplitter *m_splitter = nullptr;
    QLabel *m_vimStatus = nullptr;
    QDockWidget *m_outlineDock = nullptr;
    QTreeWidget *m_outline = nullptr;
    QWidget *m_findBar = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QCheckBox *m_findCaseSensitive = nullptr;
    QLabel *m_pathLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QMenu *m_recentMenu = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QToolBar *m_toolBar = nullptr;
    QTimer *m_reloadTimer = nullptr;
    QTimer *m_previewTimer = nullptr;

    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;
    QAction *m_reloadAction = nullptr;
    QAction *m_autoReloadAction = nullptr;
    QAction *m_printAction = nullptr;
    QAction *m_menuBarAction = nullptr;
    QAction *m_toolBarAction = nullptr;
    QAction *m_statusBarAction = nullptr;
    QAction *m_hardBreaksAction = nullptr;
    QAction *m_editAction = nullptr;
    QAction *m_vimAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_escapeAction = nullptr;
    QAction *m_themeActions[3] = { nullptr, nullptr, nullptr };

    QStringList m_history;
    int m_historyIndex = -1;
    QStringList m_recentFiles;
    int m_zoomPercent = 100;
    ThemeMode m_themeMode = SystemTheme;
    QPalette m_systemPalette;
    QString m_systemStyleName;
    QDateTime m_lastModified;
    qint64 m_lastSize = -1;
    bool m_syncingOutline = false;
    bool m_applyingTheme = false;
    bool m_darkActive = false;
    bool m_editorDirty = false;
    bool m_syncingEditor = false;
    bool m_applyingZoom = false;
};

#endif // MAINWINDOW_H
