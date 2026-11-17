#include "window.h"
#include "formats.h"
#include <stdexcept>

namespace {
QLabel *label(const QString &text, const char *role, QWidget *parent = nullptr) {
    auto l = new QLabel(text, parent);
    l->setTextFormat(Qt::PlainText);
    l->setProperty("role", role);
    return l;
}
QPushButton *button(const QString &text, const QString &name = {}, bool primary = false) {
    auto b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    if (!name.isEmpty())
        b->setObjectName(name);
    if (primary)
        b->setProperty("primary", true);
    return b;
}
QString exceptionText(const std::exception &e) {
    return QString::fromUtf8(e.what());
}
} // namespace
Window::Window(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(tr("ImageSifter · 选片工作台"));
    setWindowIcon(QIcon(":/logo.svg"));
    resize(1480, 940);
    setMinimumSize(850, 600);
    setAcceptDrops(true);
    model = new PhotoModel(this);
    filter = new PhotoFilter(this);
    filter->setSourceModel(model);
    scanTask = new QFutureWatcher<ScanResult>(this);
    fileTask = new QFutureWatcher<OperationResult>(this);
    cancel = std::make_shared<std::atomic_bool>(false);
    buildUi();
    QApplication::instance()->installEventFilter(this);
    connect(scanTask, &QFutureWatcher<ScanResult>::finished, this, [this] {
        auto result = scanTask->result();
        try {
            if (catalog && !result.cancelled) {
                int count = catalog->importPaths(scanningSource, result.paths);
                reload();
                setBusy(false, tr("扫描完成，新增 %1 张；未重新设置已有标记").arg(count));
                if (!result.error.isEmpty())
                    error(result.error);
            } else
                setBusy(false, tr("已取消扫描，已有照片记录保留"));
        } catch (const std::exception &e) {
            setBusy(false);
            error(exceptionText(e));
        }
        if (closing)
            close();
    });
    connect(fileTask, &QFutureWatcher<OperationResult>::finished, this, [this] {
        auto result = fileTask->result();
        reload();
        setBusy(false, tr("整理完成：成功 %1，失败 %2").arg(result.succeeded).arg(result.failed));
        if (result.failed)
            error(result.errors.mid(0, 10).join("\n\n"));
        if (closing)
            close();
    });
    connect(model, &PhotoModel::decodeStatus, this, [this](int running) {
        if (!busy && running)
            taskLabel->setText(tr("正在生成缩略图 · %1 个任务").arg(running));
        else if (!busy)
            taskLabel->setText(tr("所有标记自动保存"));
    });
    QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());
}
Window::~Window() {
    cancel->store(true);
    scanTask->waitForFinished();
    fileTask->waitForFinished();
}
void Window::error(const QString &message) {
    QMessageBox::warning(this, tr("ImageSifter"), message);
}
void Window::buildUi() {
    auto shell = new QWidget;
    auto outer = new QVBoxLayout(shell);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    setCentralWidget(shell);
    auto header = new QFrame;
    header->setObjectName("header");
    auto h = new QHBoxLayout(header);
    h->setContentsMargins(22, 12, 22, 12);
    h->setSpacing(12);
    auto logo = new QLabel;
    logo->setPixmap(QIcon(":/logo.svg").pixmap(38, 38));
    h->addWidget(logo);
    auto brand = new QVBoxLayout;
    brand->setSpacing(1);
    brand->addWidget(label("ImageSifter", "brand"));
    brand->addWidget(label(tr("让好照片留下来"), "muted"));
    h->addLayout(brand);
    h->addSpacing(30);
    projectTitle = label(tr("尚未打开工程"), "title");
    projectTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    projectTitle->setMinimumWidth(80);
    projectTitle->setMaximumWidth(330);
    h->addWidget(projectTitle, 1);
    h->addStretch();
    auto create = button(tr("新建工程"), "newProject", true), open = button(tr("打开工程"), "openProject"),
         more = button(tr("工程 ▾"), "projectMenu");
    h->addWidget(create);
    h->addWidget(open);
    h->addWidget(more);
    connect(create, &QPushButton::clicked, this, &Window::newProject);
    connect(open, &QPushButton::clicked, this, &Window::chooseProject);
    auto menu = new QMenu(more);
    more->setMenu(menu);
    auto recent = menu->addMenu(tr("最近工程"));
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        for (const auto &p : QSettings().value("recent").toStringList()) {
            auto a = recent->addAction(QFileInfo(p).dir().dirName());
            a->setToolTip(p);
            connect(a, &QAction::triggered, this, [this, p] { openProject(p); });
        }
        if (recent->isEmpty())
            recent->addAction(tr("暂无最近工程"))->setEnabled(false);
    });
    menu->addAction(tr("工程设置 / 弃片目录"), this, &Window::projectSettings);
    menu->addAction(tr("备份工程记录"), this, [this] {
        if (!catalog || busy)
            return;
        try {
            auto p = catalog->backup();
            QMessageBox::information(
                this, tr("备份完成"),
                tr("工程记录已备份到：\n%1\n\n备份不包含原照片；请同时保留原照片和工程文件。").arg(p));
        } catch (const std::exception &e) {
            error(exceptionText(e));
        }
    });
    menu->addAction(tr("打开工程文件夹"), this, [this] {
        if (catalog)
            QDesktopServices::openUrl(QUrl::fromLocalFile(catalog->root()));
    });
    menu->addAction(tr("文件操作记录"), this, &Window::operationRecords);
    auto organizeMenu = menu->addMenu(tr("文件整理"));
    organizeMenu->addAction(tr("分离 JPG 与 RAW…"), this, &Window::organizeFiles);
    organizeMenu->addAction(tr("当前范围：移除全部 JPG…"), this, [this] { removeFormat(true); });
    organizeMenu->addAction(tr("当前范围：移除全部 RAW…"), this, [this] { removeFormat(false); });
    organizeMenu->addAction(tr("恢复整理前的位置…"), this, [this] {
        if (!catalog || busy)
            return;
        if (QMessageBox::question(
                this, tr("恢复整理位置"),
                tr("将所有未弃片照片恢复到各自最近一次整理之前的位置。重名文件不会覆盖，弃片不受影响。"),
                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok)
            return;
        auto path = catalog->file();
        cancel = std::make_shared<std::atomic_bool>(false);
        auto token = cancel;
        setBusy(true, tr("正在恢复整理前的位置…"));
        fileTask->setFuture(QtConcurrent::run([path, token] {
            try {
                Catalog worker;
                worker.open(path, false);
                return worker.undoOrganization(token);
            } catch (const std::exception &e) {
                return OperationResult{0, 1, {exceptionText(e)}};
            }
        }));
    });
    menu->addAction(tr("关闭当前工程"), this, [this] {
        if (!catalog || busy)
            return;
        saveWorkspace();
        catalog.reset();
        model->setPhotos({}, {});
        viewer->load({});
        reference->load({});
        folders->clear();
        undoMarks.clear();
        lastId.clear();
        projectTitle->setText(tr("尚未打开工程"));
        setWindowTitle(tr("ImageSifter · 选片工作台"));
        pages->setCurrentWidget(welcome);
        setBusy(false);
        updateCounts();
    });
    menu->addAction(tr("播放 / 暂停动图"), this, [this] { viewer->toggleAnimation(); });
    menu->addSeparator();
    menu->addAction(tr("快捷键与关于"), this, [this] {
        QMessageBox::about(
            this, tr("关于 ImageSifter"),
            tr("ImageSifter 2.0.0\n本地照片筛选管理工具\n\nG 网格 · E 放大 · C 比较\nP 保留 · X 淘汰 · U "
               "未标记\n0–5 评分 · ← → 切图 · Delete 移入弃片\nCtrl+Z 撤销最近标记 · F 全屏\n\nMIT License · "
               "Qt（LGPL）· easyexif（BSD）\n弃片移动可在弃片分类恢复。"));
    });
    outer->addWidget(header);
    workspace = new QSplitter;
    workspace->setObjectName("workspace");
    outer->addWidget(workspace, 1);
    left = new QWidget;
    left->setObjectName("sidebar");
    left->setMinimumWidth(185);
    left->setMaximumWidth(320);
    auto l = new QVBoxLayout(left);
    l->setContentsMargins(16, 23, 16, 14);
    l->setSpacing(14);
    l->addWidget(label("LIBRARY", "eyebrow"));
    l->addWidget(label(tr("拍摄图库"), "section"));
    categories = new QListWidget;
    categories->setObjectName("categories");
    categories->setFocusPolicy(Qt::NoFocus);
    categories->setSpacing(4);
    categories->setFixedHeight(310);
    QStringList names{tr("全部照片"),   tr("尚未筛选"),   tr("已保留"),
                      tr("已标记淘汰"), tr("弃片文件夹"), tr("离线照片")};
    for (int i = 0; i < names.size(); i++) {
        auto item = new QListWidgetItem(names[i]);
        item->setData(Qt::UserRole, i);
        item->setSizeHint(QSize(150, 37));
        categories->addItem(item);
    }
    categories->setCurrentRow(0);
    l->addWidget(categories);
    auto directoryRow = new QHBoxLayout;
    directoryRow->addWidget(label(tr("照片来源"), "eyebrow"));
    directoryRow->addStretch();
    auto add = button("+", "addFolder");
    add->setFixedWidth(30);
    directoryRow->addWidget(add);
    l->addLayout(directoryRow);
    folders = new QListWidget;
    folders->setObjectName("folders");
    folders->setSpacing(3);
    folders->setContextMenuPolicy(Qt::CustomContextMenu);
    l->addWidget(folders, 1);
    auto refresh = button(tr("刷新当前来源"), "rescan");
    l->addWidget(refresh);
    projectActions << add << refresh;
    auto tip = label(tr("评分与取舍自动保存\n原图内容不会被修改"), "muted");
    tip->setWordWrap(true);
    l->addWidget(tip);
    workspace->addWidget(left);
    auto center = new QWidget;
    center->setMinimumWidth(420);
    auto c = new QVBoxLayout(center);
    c->setContentsMargins(22, 20, 22, 12);
    c->setSpacing(12);
    auto titleRow = new QHBoxLayout;
    auto titles = new QVBoxLayout;
    titles->setSpacing(3);
    heading = label(tr("全部照片"), "hero");
    subtitle = label(tr("打开工程，开始一次新的选片"), "muted");
    titles->addWidget(heading);
    titles->addWidget(subtitle);
    titleRow->addLayout(titles);
    titleRow->addStretch();
    modes = new QButtonGroup(this);
    for (int i = 0; i < 3; i++) {
        auto b = button(QStringList{tr("网格"), tr("放大"), tr("比较")}.at(i), QString("mode%1").arg(i));
        b->setCheckable(true);
        modes->addButton(b, i);
        titleRow->addWidget(b);
    }
    modes->button(0)->setChecked(true);
    c->addLayout(titleRow);
    auto filterRow = new QHBoxLayout;
    filterRow->setSpacing(8);
    search = new QLineEdit;
    search->setObjectName("search");
    search->setPlaceholderText(tr("搜索照片名称"));
    search->setClearButtonEnabled(true);
    filterRow->addWidget(search, 1);
    auto format = new QComboBox;
    format->setObjectName("formatFilter");
    format->addItems({tr("全部格式"), "JPG / JPEG", "RAW", tr("其他图片")});
    filterRow->addWidget(format);
    connect(format, &QComboBox::currentIndexChanged, this, [this](int i) {
        filter->format = i;
        filter->refresh();
        updateCounts();
    });
    stars = new QComboBox;
    stars->setObjectName("starsFilter");
    stars->addItems({tr("全部评分"), tr("≥ 1 星"), tr("≥ 2 星"), tr("≥ 3 星"), tr("≥ 4 星"), tr("5 星")});
    filterRow->addWidget(stars);
    colors = new QComboBox;
    colors->addItems({tr("全部颜色"), tr("红色"), tr("琥珀"), tr("青绿"), tr("蓝色"), tr("紫色")});
    filterRow->addWidget(colors);
    sort = new QComboBox;
    sort->addItems({tr("文件名"), tr("文件时间"), tr("评分优先"), tr("文件大小")});
    filterRow->addWidget(sort);
    c->addLayout(filterRow);
    pages = new QStackedWidget;
    c->addWidget(pages, 1);
    grid = new PhotoGrid;
    grid->setObjectName("photoGrid");
    grid->setModel(filter);
    grid->setItemDelegate(new PhotoDelegate(grid));
    grid->setViewMode(QListView::IconMode);
    grid->setResizeMode(QListView::Adjust);
    grid->setMovement(QListView::Static);
    grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    grid->setUniformItemSizes(true);
    grid->setMouseTracking(true);
    grid->setSpacing(0);
    grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    grid->installEventFilter(this);
    pages->addWidget(grid);
    compare = new QSplitter;
    viewer = new Viewer;
    viewer->setObjectName("viewer");
    reference = new Viewer;
    reference->setObjectName("reference");
    compare->addWidget(reference);
    compare->addWidget(viewer);
    pages->addWidget(compare);
    welcome = new QWidget;
    auto w = new QVBoxLayout(welcome);
    w->setAlignment(Qt::AlignCenter);
    auto largeLogo = new QLabel;
    largeLogo->setPixmap(QIcon(":/logo.svg").pixmap(88, 88));
    largeLogo->setAlignment(Qt::AlignCenter);
    w->addWidget(largeLogo);
    auto welcomeTitle = label(tr("每次拍摄，都值得认真挑选"), "hero");
    welcomeTitle->setAlignment(Qt::AlignCenter);
    w->addWidget(welcomeTitle);
    auto intro = label(tr("为一次拍摄建立工程，标记、比较，再把好照片留下。\n支持拖入工程文件；打开工程后可拖"
                          "入照片文件夹。"),
                       "muted");
    intro->setAlignment(Qt::AlignCenter);
    intro->setWordWrap(true);
    w->addWidget(intro);
    auto start = button(tr("创建第一个工程"), "welcomeCreate", true);
    start->setMaximumWidth(220);
    w->addWidget(start, 0, Qt::AlignCenter);
    connect(start, &QPushButton::clicked, this, &Window::newProject);
    pages->addWidget(welcome);
    pages->setCurrentWidget(welcome);
    auto marks = new QHBoxLayout;
    auto pick = button(tr("✓ 保留"), "pick"), reject = button(tr("× 淘汰"), "reject"),
         clear = button(tr("未标记"), "unflag"), one = button("1:1", "actualSize"),
         fit = button(tr("适应"), "fit"), rotate = button(tr("旋转"), "rotate");
    marks->addWidget(pick);
    marks->addWidget(reject);
    marks->addWidget(clear);
    marks->addSpacing(6);
    marks->addWidget(one);
    marks->addWidget(fit);
    marks->addWidget(rotate);
    marks->addStretch();
    pinButton = button(tr("固定为参考"), "pin");
    marks->addWidget(pinButton);
    autoAdvance = new QCheckBox(tr("标记后下一张"));
    autoAdvance->setObjectName("autoAdvance");
    marks->addWidget(autoAdvance);
    c->addLayout(marks);
    projectActions << pick << reject << clear << pinButton;
    syncViews = new QCheckBox(tr("同步缩放 / 平移"));
    syncViews->setChecked(true);
    syncViews->hide();
    c->addWidget(syncViews);
    film = new QListView;
    film->setObjectName("filmstrip");
    film->setModel(filter);
    film->setSelectionModel(grid->selectionModel());
    film->setItemDelegate(new PhotoDelegate(film));
    film->setViewMode(QListView::IconMode);
    film->setFlow(QListView::LeftToRight);
    film->setWrapping(false);
    film->setMovement(QListView::Static);
    film->setGridSize(QSize(140, 132));
    film->setUniformItemSizes(true);
    film->setFixedHeight(150);
    film->setSelectionMode(QAbstractItemView::ExtendedSelection);
    film->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    film->installEventFilter(this);
    film->hide();
    c->addWidget(film);
    auto bottom = new QHBoxLayout;
    selectionLabel = label(tr("未选择照片"), "muted");
    bottom->addWidget(selectionLabel);
    bottom->addStretch();
    bottom->addWidget(label(tr("缩略图"), "muted"));
    thumbnailSize = new QSlider(Qt::Horizontal);
    thumbnailSize->setObjectName("thumbnailSize");
    thumbnailSize->setRange(140, 360);
    thumbnailSize->setValue(230);
    thumbnailSize->setFixedWidth(110);
    bottom->addWidget(thumbnailSize);
    c->addLayout(bottom);
    workspace->addWidget(center);
    right = new QWidget;
    right->setObjectName("inspector");
    right->setMinimumWidth(220);
    right->setMaximumWidth(350);
    auto r = new QVBoxLayout(right);
    r->setContentsMargins(20, 25, 20, 14);
    r->setSpacing(14);
    r->addWidget(label("DETAILS", "eyebrow"));
    r->addWidget(label(tr("照片信息"), "section"));
    infoName = label(tr("选择一张照片"), "title");
    infoName->setWordWrap(true);
    infoName->setTextInteractionFlags(Qt::TextSelectableByMouse);
    r->addWidget(infoName);
    auto ratings = new QHBoxLayout;
    for (int i = 0; i <= 5; i++) {
        auto b = button(i ? QString::number(i) : "—", QString("rating%1").arg(i));
        b->setFixedWidth(30);
        b->setToolTip(tr("设置 %1 星").arg(i));
        ratings->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, i] { mark("rating", i); });
        projectActions << b;
    }
    r->addLayout(ratings);
    auto labels = new QHBoxLayout;
    QStringList palette{"#69717a", "#dc7373", "#e8b86b", "#69c9b0", "#73a8e9", "#b294d8"};
    for (int i = 0; i < 6; i++) {
        auto b = button("", QString("label%1").arg(i));
        b->setFixedSize(25, 20);
        b->setToolTip(i ? tr("设置颜色标签 %1").arg(i) : tr("清除颜色标签"));
        b->setStyleSheet(
            QString("background:%1;border:2px solid #343c44;border-radius:5px;").arg(palette[i]));
        labels->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, i] { mark("label", i); });
        projectActions << b;
    }
    labels->addStretch();
    r->addLayout(labels);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    infoDetails = label(tr("文件、尺寸和拍摄参数会显示在这里。"), "muted");
    infoDetails->setWordWrap(true);
    infoDetails->setTextFormat(Qt::PlainText);
    infoDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoDetails->setAlignment(Qt::AlignTop);
    scroll->setWidget(infoDetails);
    r->addWidget(scroll, 1);
    auto copy = button(tr("复制选中照片到…"), "copySelection");
    auto organizeAction = button(tr("文件整理 ▾"), "organizeFiles");
    organizeAction->setMenu(organizeMenu);
    r->addWidget(organizeAction);
    projectActions << organizeAction;
    trashButton = button(tr("移入弃片文件夹"), "moveToRejected");
    auto folder = button(tr("在文件夹中查看"), "revealFile");
    r->addWidget(copy);
    r->addWidget(trashButton);
    r->addWidget(folder);
    projectActions << copy << trashButton << folder;
    workspace->addWidget(right);
    workspace->setSizes({215, 990, 260});
    workspace->setStretchFactor(1, 1);
    auto status = new QFrame;
    status->setObjectName("status");
    auto s = new QHBoxLayout(status);
    s->setContentsMargins(18, 7, 18, 7);
    stats = label(tr("ImageSifter 2.0"), "muted");
    taskLabel = label(tr("离线工作 · 无需账户"), "muted");
    progress = new QProgressBar;
    progress->setFixedWidth(130);
    progress->setFixedHeight(5);
    progress->setTextVisible(false);
    progress->hide();
    cancelButton = button(tr("取消"), "cancelTask");
    cancelButton->hide();
    s->addWidget(stats);
    s->addStretch();
    s->addWidget(taskLabel);
    s->addWidget(progress);
    s->addWidget(cancelButton);
    outer->addWidget(status);
    connect(categories, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        filter->category = PhotoFilter::Category(row);
        filter->source.clear();
        folders->clearSelection();
        filter->refresh();
        heading->setText(categories->item(row)->text().section("  ", 0, 0));
        trashButton->setText(row == PhotoFilter::Trash ? tr("恢复到原位置") : tr("移入弃片文件夹"));
        updateCounts();
        if (filter->rowCount())
            grid->setCurrentIndex(filter->index(0, 0));
        else
            updateCurrent();
    });
    connect(folders, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        filter->source = item->data(Qt::UserRole).toString();
        filter->refresh();
        heading->setText(item->text());
        updateCounts();
    });
    connect(folders, &QListWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        auto item = folders->itemAt(point);
        if (!item || !catalog || busy)
            return;
        QMenu menu;
        auto relink = menu.addAction(tr("重新定位来源目录…"));
        auto reveal = menu.addAction(tr("打开来源目录"));
        auto chosen = menu.exec(folders->viewport()->mapToGlobal(point));
        if (chosen == relink) {
            QString next = QFileDialog::getExistingDirectory(this, tr("重新定位照片目录"));
            if (!next.isEmpty())
                try {
                    catalog->relinkSource(item->data(Qt::UserRole).toString(), next);
                    reload();
                } catch (const std::exception &e) {
                    error(exceptionText(e));
                }
        } else if (chosen == reveal)
            QDesktopServices::openUrl(QUrl::fromLocalFile(item->toolTip()));
    });
    connect(add, &QPushButton::clicked, this, [this] { addFolder(); });
    connect(refresh, &QPushButton::clicked, this, [this] { refreshCatalog(true); });
    connect(search, &QLineEdit::textChanged, this, [this](const QString &t) {
        filter->text = t;
        filter->refresh();
        updateCounts();
    });
    connect(stars, &QComboBox::currentIndexChanged, this, [this](int i) {
        filter->stars = i;
        filter->refresh();
        updateCounts();
    });
    connect(colors, &QComboBox::currentIndexChanged, this, [this](int i) {
        filter->color = i;
        filter->refresh();
        updateCounts();
    });
    connect(sort, &QComboBox::currentIndexChanged, this, [this](int i) {
        filter->order = i;
        filter->refresh();
    });
    connect(modes, &QButtonGroup::idClicked, this, &Window::setMode);
    connect(thumbnailSize, &QSlider::valueChanged, this,
            [this](int size) { static_cast<PhotoGrid *>(grid)->setThumbnailWidth(size); });
    static_cast<PhotoGrid *>(grid)->setThumbnailWidth(230);
    connect(grid->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { updateCurrent(); });
    connect(grid->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { updateCounts(); });
    connect(grid, &QListView::doubleClicked, this, [this] { setMode(1); });
    connect(viewer, &Viewer::imageInfo, this,
            [this](const QString &path, const QSize &size, const QString &details) {
                auto p = currentPhoto();
                if (!p || p->path != path)
                    return;
                QFileInfo f(path);
                infoDetails->setText(tr("%1 × %2 像素\n%3 MB · %4\n\n%5\n\n%6")
                                         .arg(size.width())
                                         .arg(size.height())
                                         .arg(f.size() / 1048576.0, 0, 'f', 2)
                                         .arg(f.suffix().toUpper(), details, path));
            });
    connect(viewer, &Viewer::transformChanged, this, [this](double scale, const QPointF &point) {
        if (mode == 2 && syncViews->isChecked())
            reference->syncFrom(scale, point);
    });
    connect(reference, &Viewer::transformChanged, this, [this](double scale, const QPointF &point) {
        if (mode == 2 && syncViews->isChecked())
            viewer->syncFrom(scale, point);
    });
    connect(pick, &QPushButton::clicked, this, [this] { mark("flag", 1); });
    connect(reject, &QPushButton::clicked, this, [this] { mark("flag", -1); });
    connect(clear, &QPushButton::clicked, this, [this] { mark("flag", 0); });
    connect(one, &QPushButton::clicked, viewer, &Viewer::actualSize);
    connect(fit, &QPushButton::clicked, viewer, &Viewer::fit);
    connect(rotate, &QPushButton::clicked, this, [this] { viewer->rotate(90); });
    connect(pinButton, &QPushButton::clicked, this, [this] {
        if (auto p = currentPhoto()) {
            reference->load(p->path);
            setMode(2);
        }
    });
    connect(trashButton, &QPushButton::clicked, this,
            [this] { relocate(filter->category == PhotoFilter::Trash); });
    connect(copy, &QPushButton::clicked, this, &Window::copySelection);
    connect(folder, &QPushButton::clicked, this, [this] {
        if (auto p = currentPhoto())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p->path).absolutePath()));
    });
    connect(cancelButton, &QPushButton::clicked, this, [this] {
        cancel->store(true);
        taskLabel->setText(tr("正在安全停止，请稍候…"));
        cancelButton->setEnabled(false);
    });
    auto viewMenu = menuBar()->addMenu(tr("视图"));
    leftToggle = viewMenu->addAction(tr("目录侧栏"));
    leftToggle->setCheckable(true);
    leftToggle->setChecked(true);
    connect(leftToggle, &QAction::toggled, left, &QWidget::setVisible);
    rightToggle = viewMenu->addAction(tr("信息侧栏"));
    rightToggle->setCheckable(true);
    rightToggle->setChecked(true);
    connect(rightToggle, &QAction::toggled, right, &QWidget::setVisible);
    menuBar()->hide();
    menu->addSeparator();
    menu->addAction(leftToggle);
    menu->addAction(rightToggle);
    viewer->installEventFilter(this);
    reference->installEventFilter(this);
    setBusy(false);
}
void Window::remember(const QString &path) {
    QSettings s;
    auto list = s.value("recent").toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 12)
        list.removeLast();
    s.setValue("recent", list);
}
bool Window::openProject(const QString &path) {
    if (busy) {
        error(tr("请先等待或取消当前任务"));
        return false;
    }
    try {
        if (catalog && QFileInfo(path).absoluteFilePath() == catalog->file())
            return true;
        auto next = std::make_unique<Catalog>();
        next->open(path);
        saveWorkspace();
        catalog = std::move(next);
        undoMarks.clear();
        lastId.clear();
        viewer->load({});
        reference->load({});
        projectTitle->setText(catalog->name());
        setWindowTitle(catalog->name() + " · ImageSifter");
        remember(catalog->file());
        filter->source.clear();
        filter->text.clear();
        search->clear();
        categories->setCurrentRow(0);
        filter->category = PhotoFilter::All;
        stars->setCurrentIndex(catalog->setting("filterStars", 0).toInt());
        colors->setCurrentIndex(catalog->setting("filterColor", 0).toInt());
        sort->setCurrentIndex(catalog->setting("sort", 0).toInt());
        thumbnailSize->setValue(catalog->setting("thumbnailSize", 230).toInt());
        autoAdvance->setChecked(catalog->setting("autoAdvance", 0).toInt());
        reload();
        setMode(catalog->setting("mode", 0).toInt());
        auto saved = catalog->setting("currentId").toString();
        for (int i = 0; i < filter->rowCount(); i++)
            if (model->photo(filter->mapToSource(filter->index(i, 0)).row()).id == saved) {
                grid->setCurrentIndex(filter->index(i, 0));
                break;
            }
        auto split = QByteArray::fromBase64(catalog->setting("splitter").toByteArray());
        if (!split.isEmpty())
            workspace->restoreState(split);
        setBusy(false);
        if (!catalog->unresolvedOperations().isEmpty())
            QTimer::singleShot(0, this, &Window::operationRecords);
        return true;
    } catch (const std::exception &e) {
        error(exceptionText(e));
        return false;
    }
}
void Window::newProject() {
    if (busy)
        return;
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("新建拍摄工程"), tr("工程名称，例如：2026-11 城市漫步"),
                                         QLineEdit::Normal, {}, &ok)
                       .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (name.contains(QRegularExpression("[<>:\"/\\\\|?*]")) || name == "." || name == ".." ||
        name.endsWith('.') || name.endsWith(' ')) {
        error(tr("工程名称包含不支持的路径字符"));
        return;
    }
    QString parent =
        QFileDialog::getExistingDirectory(this, tr("选择存放工程的位置（将在其中新建同名文件夹）"));
    if (parent.isEmpty())
        return;
    try {
        QString path = Catalog::create(QDir(parent).filePath(name), name);
        if (openProject(path))
            addFolder();
    } catch (const std::exception &e) {
        error(exceptionText(e));
    }
}
void Window::chooseProject() {
    if (busy)
        return;
    QString file =
        QFileDialog::getOpenFileName(this, tr("打开工程"), {}, tr("ImageSifter 工程 (*.imagesifter)"));
    if (!file.isEmpty())
        openProject(file);
}
void Window::addFolder(const QString &path) {
    if (!catalog) {
        error(tr("请先创建或打开一个工程"));
        return;
    }
    if (busy)
        return;
    auto directory = path.isEmpty() ? QFileDialog::getExistingDirectory(this, tr("添加照片来源目录")) : path;
    if (directory.isEmpty())
        return;
    try {
        auto id = catalog->addSource(directory);
        updateFolders();
        scanSource({id, directory}, catalog->setting("recursive", 1).toInt());
    } catch (const std::exception &e) {
        error(exceptionText(e));
    }
}
void Window::scanSource(const Source &source, bool recursive) {
    if (busy)
        return;
    cancel = std::make_shared<std::atomic_bool>(false);
    auto token = cancel;
    auto project = catalog->root(), rejected = catalog->rejectedDirectory();
    scanningSource = source.id;
    setBusy(true, tr("正在扫描目录，可随时取消…"));
    scanTask->setFuture(QtConcurrent::run([source, project, rejected, recursive, token] {
        return Catalog::scan(source.path, project, rejected, recursive, token);
    }));
}
void Window::refreshCatalog(bool rescan) {
    if (!catalog || busy)
        return;
    if (!rescan) {
        reload();
        return;
    }
    auto sources = catalog->sources();
    if (sources.isEmpty()) {
        addFolder();
        return;
    }
    QString id = filter->source;
    if (id.isEmpty() && sources.size() > 1) {
        QStringList paths;
        for (auto s : sources)
            paths << s.path;
        bool ok = false;
        auto chosen = QInputDialog::getItem(this, tr("刷新照片来源"),
                                            tr("选择需要重新扫描的来源；已有评分保留"), paths, 0, false, &ok);
        if (!ok)
            return;
        for (auto s : sources)
            if (s.path == chosen) {
                id = s.id;
                break;
            }
    }
    for (auto s : sources)
        if (id.isEmpty() || s.id == id) {
            scanSource(s);
            return;
        }
}
void Window::updateFolders() {
    folders->clear();
    if (!catalog)
        return;
    for (auto s : catalog->sources()) {
        auto i = new QListWidgetItem(QFileInfo(s.path).fileName().isEmpty() ? s.path
                                                                            : QFileInfo(s.path).fileName());
        i->setData(Qt::UserRole, s.id);
        i->setToolTip(s.path);
        i->setSizeHint(QSize(160, 35));
        if (!QFileInfo(s.path).isDir())
            i->setText(i->text() + tr(" · 离线"));
        folders->addItem(i);
    }
}
void Window::reload() {
    if (!catalog)
        return;
    QString id = lastId;
    model->setPhotos(catalog->photos(), catalog->root() + "/previews");
    filter->refresh();
    updateFolders();
    int chosen = 0;
    for (int i = 0; i < filter->rowCount(); i++)
        if (model->photo(filter->mapToSource(filter->index(i, 0)).row()).id == id) {
            chosen = i;
            break;
        }
    if (filter->rowCount())
        grid->setCurrentIndex(filter->index(chosen, 0));
    else
        updateCurrent();
    updateCounts();
}
const Photo *Window::currentPhoto() const {
    auto index = filter->mapToSource(grid->currentIndex());
    return index.isValid() ? &model->photo(index.row()) : nullptr;
}
QList<Photo> Window::selectedPhotos() const {
    QList<Photo> result;
    for (auto index : grid->selectionModel()->selectedIndexes())
        result << model->photo(filter->mapToSource(index).row());
    if (result.isEmpty())
        if (auto p = currentPhoto())
            result << *p;
    return result;
}
QStringList Window::selectedIds() const {
    QStringList ids;
    for (auto p : selectedPhotos())
        ids << p.id;
    return ids;
}
void Window::updateCurrent() {
    const auto p = currentPhoto();
    if (!p) {
        infoName->setText(tr("没有匹配的照片"));
        infoDetails->setText(tr("调整筛选条件，或添加照片来源。"));
        viewer->load({});
        updateCounts();
        return;
    }
    lastId = p->id;
    infoName->setText(QFileInfo(p->original).fileName());
    QFileInfo f(p->path);
    infoDetails->setText(tr("%1\n评分 %2 星 · %3\n%4 MB\n\n%5")
                             .arg(p->rejected     ? tr("已移入弃片")
                                  : p->flag == 1  ? tr("已保留")
                                  : p->flag == -1 ? tr("已标记淘汰")
                                                  : tr("尚未标记"))
                             .arg(p->rating)
                             .arg(f.suffix().toUpper())
                             .arg(f.size() / 1048576.0, 0, 'f', 2)
                             .arg(p->path));
    if (mode && viewer->path() != p->path)
        viewer->load(p->path);
    film->scrollTo(grid->currentIndex());
    updateCounts();
}
void Window::updateCounts() {
    if (filter->rowCount() > 0 && !grid->currentIndex().isValid() && !selectionScheduled) {
        selectionScheduled = true;
        QTimer::singleShot(0, this, [this] {
            selectionScheduled = false;
            if (grid->currentIndex().isValid() || filter->rowCount() == 0)
                return;
            int chosen = 0;
            for (int row = 0; row < filter->rowCount(); row++)
                if (model->photo(filter->mapToSource(filter->index(row, 0)).row()).id == lastId) {
                    chosen = row;
                    break;
                }
            grid->setCurrentIndex(filter->index(chosen, 0));
        });
    }
    int count = filter->rowCount(), selected = grid->selectionModel()->selectedIndexes().size();
    stats->setText(tr("%1 张照片 · 当前显示 %2 张").arg(model->rowCount()).arg(count));
    selectionLabel->setText(selected ? tr("已选择 %1 张").arg(selected) : tr("未选择照片"));
    subtitle->setText(catalog ? tr("%1 · %2 张匹配照片").arg(catalog->name()).arg(count)
                              : tr("打开工程，开始一次新的选片"));
}
void Window::mark(const QString &field, int value) {
    if (!catalog || busy)
        return;
    auto selected = selectedPhotos();
    if (selected.isEmpty())
        return;
    QStringList ids;
    undoMarks.clear();
    undoField = field;
    int next = grid->currentIndex().row();
    for (auto p : selected) {
        ids << p.id;
        undoMarks.append({p.id, field == "rating" ? p.rating : field == "flag" ? p.flag : p.label});
    }
    try {
        catalog->setMark(ids, field, value);
        model->applyMarks(ids, field, value);
        updateCounts();
        if (autoAdvance->isChecked() && selected.size() == 1 && filter->rowCount()) {
            bool stillVisible = false;
            for (int i = 0; i < filter->rowCount(); i++)
                if (model->photo(filter->mapToSource(filter->index(i, 0)).row()).id == selected.first().id) {
                    stillVisible = true;
                    next = i;
                    break;
                }
            grid->selectionModel()->setCurrentIndex(
                filter->index(qMin(next + (stillVisible ? 1 : 0), filter->rowCount() - 1), 0),
                QItemSelectionModel::ClearAndSelect);
        } else
            updateCurrent();
    } catch (const std::exception &e) {
        undoMarks.clear();
        error(exceptionText(e));
    }
}
void Window::undoMark() {
    if (!catalog || busy || undoMarks.isEmpty())
        return;
    try {
        for (auto pair : undoMarks) {
            catalog->setMark({pair.first}, undoField, pair.second);
            model->applyMarks({pair.first}, undoField, pair.second);
        }
        undoMarks.clear();
        updateCurrent();
    } catch (const std::exception &e) {
        error(exceptionText(e));
    }
}
void Window::setMode(int index) {
    mode = qBound(0, index, 2);
    modes->button(mode)->setChecked(true);
    if (!catalog) {
        pages->setCurrentWidget(welcome);
        return;
    }
    pages->setCurrentWidget(mode ? static_cast<QWidget *>(compare) : grid);
    reference->setVisible(mode == 2);
    if (mode == 2)
        compare->setSizes({compare->width() / 2, compare->width() / 2});
    film->setVisible(mode != 0);
    syncViews->setVisible(mode == 2);
    pinButton->setVisible(mode != 0);
    if (mode == 2 && reference->path().isEmpty())
        if (auto p = currentPhoto())
            reference->load(p->path);
    updateCurrent();
}
void Window::navigate(int offset) {
    int count = filter->rowCount();
    if (!count)
        return;
    int row = qBound(0, grid->currentIndex().row() + offset, count - 1);
    auto index = filter->index(row, 0);
    grid->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    grid->scrollTo(index);
}
void Window::setBusy(bool value, const QString &message) {
    busy = value;
    for (auto b : projectActions)
        b->setEnabled(catalog && !value);
    findChild<QPushButton *>("newProject")->setEnabled(!value);
    findChild<QPushButton *>("openProject")->setEnabled(!value);
    progress->setVisible(value);
    progress->setRange(0, 0);
    cancelButton->setVisible(value);
    cancelButton->setEnabled(value);
    if (!message.isEmpty())
        taskLabel->setText(message);
    else if (!value)
        taskLabel->setText(tr("所有标记自动保存"));
}
void Window::relocate(bool restore) {
    if (!catalog || busy)
        return;
    auto photos = catalog->expandedSelection(selectedIds(), true);
    photos.erase(std::remove_if(photos.begin(), photos.end(),
                                [restore](const Photo &p) { return p.rejected != restore; }),
                 photos.end());
    if (photos.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(restore ? tr("恢复弃片") : tr("移入弃片文件夹"));
    auto layout = new QVBoxLayout(&dialog);
    auto description = new QLabel(
        restore ? tr("将所选 %1 张照片恢复到原位置。同名文件不会覆盖。").arg(photos.size())
                : tr("将所选 %1 张照片移入：\n%2\n\n文件将离开原目录；其他引用这些照片的工程也会受到影响。")
                      .arg(photos.size())
                      .arg(catalog->rejectedDirectory()));
    description->setWordWrap(true);
    description->setMaximumWidth(560);
    layout->addWidget(description);
    auto pairing = new QLabel(tr("包含已配对的 JPG / RAW；分离整理后仍按工程中的配对关系处理。"));
    pairing->setWordWrap(true);
    layout->addWidget(pairing);
    auto list = new QPlainTextEdit;
    list->setReadOnly(true);
    QStringList names;
    for (auto p : photos)
        names << p.path;
    list->setPlainText(names.join("\n"));
    list->setMaximumHeight(150);
    layout->addWidget(list);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(restore ? tr("恢复") : tr("移入弃片"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto ids = selectedIds();
    auto path = catalog->file();
    bool include = true;
    cancel = std::make_shared<std::atomic_bool>(false);
    auto token = cancel;
    setBusy(true, tr("正在整理文件，请勿拔出磁盘…"));
    fileTask->setFuture(QtConcurrent::run([path, ids, restore, include, token] {
        try {
            Catalog worker;
            worker.open(path, false);
            return worker.relocate(ids, restore, include, token);
        } catch (const std::exception &e) {
            return OperationResult{0, 1, {exceptionText(e)}};
        }
    }));
}
void Window::organizeFiles() {
    if (!catalog || busy)
        return;
    QStringList ids;
    int jpeg = 0, raw = 0;
    for (int row = 0; row < filter->rowCount(); row++) {
        auto p = model->photo(filter->mapToSource(filter->index(row, 0)).row());
        if (p.rejected)
            continue;
        if (Formats::isJpeg(p.path)) {
            jpeg++;
            ids << p.id;
        } else if (Formats::isRaw(p.path)) {
            raw++;
            ids << p.id;
        }
    }
    if (ids.isEmpty()) {
        error(tr("当前筛选范围没有可整理的 JPG 或 RAW"));
        return;
    }
    auto dest = QFileDialog::getExistingDirectory(this, tr("选择整理输出位置（创建 JPG 和 RAW 子文件夹）"));
    if (dest.isEmpty())
        return;
    QMessageBox confirm(this);
    confirm.setWindowTitle(tr("分离 JPG 与 RAW"));
    confirm.setText(tr("将当前筛选结果中的 %1 张 JPG 和 %2 张 RAW "
                       "移动到：\n%3/JPG\n%3/"
                       "RAW\n\n保留来源及相对目录层级，配对、评分和标记保留。其他工程可能需要重新定位。此操作"
                       "不会移除任一格式。")
                        .arg(jpeg)
                        .arg(raw)
                        .arg(dest));
    confirm.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    QStringList plan;
    for (const auto &p : catalog->photos())
        if (ids.contains(p.id))
            plan << p.path + " → " + dest + "/" + Formats::category(p.path) + "/" + p.source.left(8) + "/" +
                        p.relative;
    confirm.setDetailedText(plan.join("\n"));
    if (confirm.exec() != QMessageBox::Ok)
        return;
    auto path = catalog->file();
    cancel = std::make_shared<std::atomic_bool>(false);
    auto token = cancel;
    setBusy(true, tr("正在分离 JPG 与 RAW…"));
    fileTask->setFuture(QtConcurrent::run([path, ids, dest, token] {
        try {
            Catalog worker;
            worker.open(path, false);
            return worker.organize(ids, dest, token);
        } catch (const std::exception &e) {
            return OperationResult{0, 1, {exceptionText(e)}};
        }
    }));
}
void Window::removeFormat(bool jpeg) {
    if (!catalog || busy)
        return;
    QStringList ids, paths;
    for (int row = 0; row < filter->rowCount(); row++) {
        auto p = model->photo(filter->mapToSource(filter->index(row, 0)).row());
        if (!p.rejected && (jpeg ? Formats::isJpeg(p.path) : Formats::isRaw(p.path))) {
            ids << p.id;
            paths << p.path;
        }
    }
    if (ids.isEmpty()) {
        error(tr("当前筛选范围没有该格式的照片"));
        return;
    }
    QMessageBox confirm(this);
    confirm.setWindowTitle(jpeg ? tr("移除当前范围的 JPG") : tr("移除当前范围的 RAW"));
    confirm.setText(
        tr("将当前筛选结果中的 %1 个 %2 "
           "文件移入专用弃片目录。\n\n本操作只移除这一种格式，配对的另一种格式保留。可在弃片分类查看并恢复。")
            .arg(ids.size())
            .arg(jpeg ? "JPG/JPEG" : "RAW"));
    confirm.setDetailedText(paths.join("\n"));
    confirm.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    if (confirm.exec() != QMessageBox::Ok)
        return;
    auto path = catalog->file();
    cancel = std::make_shared<std::atomic_bool>(false);
    auto token = cancel;
    setBusy(true, tr("正在按格式移入弃片…"));
    fileTask->setFuture(QtConcurrent::run([path, ids, token] {
        try {
            Catalog worker;
            worker.open(path, false);
            return worker.relocate(ids, false, false, token);
        } catch (const std::exception &e) {
            return OperationResult{0, 1, {exceptionText(e)}};
        }
    }));
}
void Window::copySelection() {
    if (!catalog || busy)
        return;
    auto photos = selectedPhotos();
    if (photos.isEmpty())
        return;
    QString dest = QFileDialog::getExistingDirectory(this, tr("复制选中照片到目录（重名时自动改名）"));
    if (dest.isEmpty())
        return;
    cancel = std::make_shared<std::atomic_bool>(false);
    auto token = cancel;
    setBusy(true, tr("正在复制照片，原文件保留…"));
    fileTask->setFuture(QtConcurrent::run([photos, dest, token] {
        OperationResult r;
        for (auto p : photos) {
            if (token->load())
                break;
            QFileInfo f(p.path);
            QString target = QDir(dest).filePath(f.fileName());
            if (QFileInfo::exists(target))
                target = QDir(dest).filePath(f.completeBaseName() + "-" +
                                             QUuid::createUuid().toString(QUuid::Id128).left(8) + "." +
                                             f.suffix());
            if (QFile::copy(p.path, target))
                r.succeeded++;
            else {
                r.failed++;
                r.errors << QObject::tr("无法复制：%1").arg(p.path);
            }
        }
        return r;
    }));
}
void Window::operationRecords() {
    if (!catalog)
        return;
    auto records = catalog->unresolvedOperations();
    QMessageBox box(this);
    box.setWindowTitle(tr("文件操作记录"));
    box.setText(records.isEmpty() ? tr("没有需要处理的中断操作。展开详细信息可查看最近 200 项文件操作。")
                                  : tr("发现 %1 "
                                       "项需要核对的移动记录。程序保留现有文件，不会自动删除重复副本。\n请核"
                                       "对下方路径，处理后重新打开工程以复查。")
                                        .arg(records.size()));
    box.setDetailedText(catalog->operationHistory().join("\n\n"));
    box.exec();
}
void Window::projectSettings() {
    if (!catalog || busy)
        return;
    QDialog d(this);
    d.setWindowTitle(tr("工程设置"));
    auto layout = new QFormLayout(&d);
    auto path = new QLineEdit(catalog->rejectedDirectory());
    path->setMinimumWidth(360);
    layout->addRow(tr("弃片目录"), path);
    auto choose = button(tr("选择目录…"));
    layout->addRow("", choose);
    connect(choose, &QPushButton::clicked, &d, [this, path] {
        auto p = QFileDialog::getExistingDirectory(this, tr("选择专用弃片目录"));
        if (!p.isEmpty())
            path->setText(p);
    });
    auto recursive = new QCheckBox(tr("添加来源时包含子目录"));
    recursive->setChecked(catalog->setting("recursive", 1).toInt());
    layout->addRow(recursive);
    auto copyMode = new QCheckBox(tr("始终使用校验复制完成移动（网络盘兼容）"));
    copyMode->setChecked(catalog->setting("verifiedCopy", 0).toInt());
    layout->addRow(copyMode);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() == QDialog::Accepted)
        try {
            if (path->text() != catalog->rejectedDirectory())
                catalog->setRejectedDirectory(path->text());
            catalog->setSetting("recursive", recursive->isChecked() ? 1 : 0);
            catalog->setSetting("verifiedCopy", copyMode->isChecked() ? 1 : 0);
        } catch (const std::exception &e) {
            error(exceptionText(e));
        }
}
void Window::saveWorkspace() {
    if (!catalog || busy)
        return;
    try {
        catalog->setSetting("mode", mode);
        catalog->setSetting("thumbnailSize", thumbnailSize->value());
        catalog->setSetting("currentId", lastId);
        catalog->setSetting("filterStars", stars->currentIndex());
        catalog->setSetting("filterColor", colors->currentIndex());
        catalog->setSetting("sort", sort->currentIndex());
        catalog->setSetting("autoAdvance", autoAdvance->isChecked() ? 1 : 0);
        catalog->setSetting("splitter", QString::fromLatin1(workspace->saveState().toBase64()));
    } catch (const std::exception &e) {
        taskLabel->setText(tr("保存布局失败：%1").arg(exceptionText(e)));
    }
}
void Window::closeEvent(QCloseEvent *event) {
    if (busy) {
        closing = true;
        cancel->store(true);
        taskLabel->setText(tr("正在安全结束任务，完成后关闭…"));
        event->ignore();
        return;
    }
    saveWorkspace();
    QSettings().setValue("geometry", saveGeometry());
    event->accept();
}
void Window::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (leftToggle && rightToggle) {
        leftToggle->setChecked(event->size().width() >= 1100);
        rightToggle->setChecked(event->size().width() >= 1300);
    }
}
void Window::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls() && !busy)
        e->acceptProposedAction();
}
void Window::dropEvent(QDropEvent *e) {
    if (busy)
        return;
    auto urls = e->mimeData()->urls();
    if (urls.size() != 1) {
        error(tr("请一次拖入一个工程文件或照片文件夹"));
        return;
    }
    auto p = urls.first().toLocalFile();
    if (p.endsWith(".imagesifter", Qt::CaseInsensitive))
        openProject(p);
    else if (QFileInfo(p).isDir())
        addFolder(p);
    else
        error(tr("请拖入工程文件或照片文件夹"));
}
bool Window::eventFilter(QObject *object, QEvent *event) {
    if (event->type() != QEvent::KeyPress)
        return false;
    if (QApplication::activeModalWidget())
        return false;
    auto widget = qobject_cast<QWidget *>(object);
    if (!widget || (!isAncestorOf(widget) && widget != this))
        return false;
    if (qobject_cast<QLineEdit *>(widget) || qobject_cast<QPlainTextEdit *>(widget) ||
        qobject_cast<QTextEdit *>(widget) || qobject_cast<QComboBox *>(widget))
        return false;
    if (event->type() != QEvent::KeyPress)
        return false;
    auto key = static_cast<QKeyEvent *>(event);
    if (key->modifiers() & Qt::ControlModifier) {
        if (key->key() == Qt::Key_Z) {
            undoMark();
            return true;
        }
        return false;
    }
    switch (key->key()) {
    case Qt::Key_Left:
        navigate(-1);
        return true;
    case Qt::Key_Right:
        navigate(1);
        return true;
    case Qt::Key_P:
        mark("flag", 1);
        return true;
    case Qt::Key_X:
        mark("flag", -1);
        return true;
    case Qt::Key_U:
        mark("flag", 0);
        return true;
    case Qt::Key_G:
        setMode(0);
        return true;
    case Qt::Key_E:
        setMode(1);
        return true;
    case Qt::Key_C:
        setMode(2);
        return true;
    case Qt::Key_F:
        isFullScreen() ? showNormal() : showFullScreen();
        return true;
    case Qt::Key_Delete:
        relocate(filter->category == PhotoFilter::Trash);
        return true;
    default:
        if (key->key() >= Qt::Key_0 && key->key() <= Qt::Key_5) {
            mark("rating", key->key() - Qt::Key_0);
            return true;
        }
    }
    return false;
}
