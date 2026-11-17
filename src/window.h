#pragma once
#include "photos.h"
#include "viewer.h"

class Window : public QMainWindow {
    Q_OBJECT
  public:
    explicit Window(QWidget *parent = nullptr);
    ~Window() override;
    bool openProject(const QString &path);
    bool idle() const {
        return !busy;
    }

  protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    std::unique_ptr<Catalog> catalog;
    PhotoModel *model;
    PhotoFilter *filter;
    QListView *grid, *film;
    QListWidget *categories, *folders;
    QWidget *left = nullptr, *right = nullptr, *welcome;
    QAction *leftToggle = nullptr, *rightToggle = nullptr;
    QSplitter *workspace, *compare;
    QStackedWidget *pages;
    Viewer *viewer, *reference;
    QLabel *projectTitle, *heading, *subtitle, *infoName, *infoDetails, *stats, *taskLabel, *selectionLabel;
    QLineEdit *search;
    QComboBox *stars, *colors, *sort;
    QProgressBar *progress;
    QSlider *thumbnailSize;
    QCheckBox *autoAdvance, *syncViews;
    QPushButton *cancelButton, *trashButton, *pinButton;
    QButtonGroup *modes;
    QList<QPushButton *> projectActions;
    QFutureWatcher<ScanResult> *scanTask;
    QFutureWatcher<OperationResult> *fileTask;
    std::shared_ptr<std::atomic_bool> cancel;
    QString scanningSource;
    int mode = 0;
    bool busy = false, closing = false, selectionScheduled = false;
    QList<QPair<QString, int>> undoMarks;
    QString undoField;
    QString lastId;
    void buildUi();
    void newProject();
    void chooseProject();
    void addFolder(const QString &path = {});
    void refreshCatalog(bool rescan = false);
    void scanSource(const Source &source, bool recursive = true);
    void reload();
    void updateCurrent();
    void updateCounts();
    void mark(const QString &field, int value);
    void undoMark();
    void setMode(int index);
    void navigate(int offset);
    void relocate(bool restore);
    void organizeFiles();
    void removeFormat(bool jpeg);
    void copySelection();
    void projectSettings();
    void operationRecords();
    void updateFolders();
    QStringList selectedIds() const;
    QList<Photo> selectedPhotos() const;
    const Photo *currentPhoto() const;
    void setBusy(bool value, const QString &message = {});
    void error(const QString &message);
    void saveWorkspace();
    void remember(const QString &path);
};
