#include "viewer.h"
#include "exif.h"
#include "formats.h"
#include "photos.h"
#include <libraw/libraw.h>

Viewer::Viewer(QWidget *parent) : QGraphicsView(parent), scene(this) {
    setScene(&scene);
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(QColor("#161b20"));
    setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumSize(150, 140);
    setFocusPolicy(Qt::StrongFocus);
    message = tr("选择照片开始浏览");
}
Viewer::~Viewer() {
    for (auto t : tasks) {
        disconnect(t, nullptr, this, nullptr);
        t->waitForFinished();
    }
}
void Viewer::load(const QString &path) {
    generation++;
    int request = generation;
    currentPath = path;
    rotation = 0;
    fitting = true;
    if (movie) {
        delete movie;
        movie = nullptr;
    }
    scene.clear();
    item = nullptr;
    resetTransform();
    message = path.isEmpty() ? tr("选择照片开始浏览") : tr("正在解码照片…");
    viewport()->update();
    if (path.isEmpty() || !tasks.isEmpty())
        return;
    auto task = new QFutureWatcher<QPair<QImage, QString>>(this);
    tasks.append(task);
    connect(task, &QFutureWatcher<QPair<QImage, QString>>::finished, this, [this, task, path, request] {
        auto result = task->result();
        tasks.removeOne(task);
        task->deleteLater();
        if (request != generation) {
            if (!currentPath.isEmpty())
                load(currentPath);
            return;
        }
        if (result.first.isNull()) {
            message = tr("无法预览此文件\n可能离线、损坏或缺少格式插件\n原文件不会被修改");
            viewport()->update();
            emit imageInfo(path, {}, result.second);
            return;
        }
        message.clear();
        item = scene.addPixmap(QPixmap::fromImage(result.first));
        if (!QSettings().value("reduceMotion", false).toBool()) {
            item->setOpacity(0);
            auto fade = new QVariantAnimation(this);
            fade->setDuration(140);
            fade->setStartValue(0.0);
            fade->setEndValue(1.0);
            connect(fade, &QVariantAnimation::valueChanged, this, [this, request](const QVariant &v) {
                if (request == generation && item)
                    item->setOpacity(v.toDouble());
            });
            connect(fade, &QVariantAnimation::finished, fade, &QObject::deleteLater);
            fade->start();
        }
        scene.setSceneRect(item->boundingRect());
        fit();
        emit imageInfo(path, result.first.size(), result.second);
        QImageReader reader(path);
        if (reader.supportsAnimation() && reader.imageCount() != 1) {
            movie = new QMovie(path, QByteArray(), this);
            movie->setCacheMode(QMovie::CacheNone);
            playback = true;
            connect(movie, &QMovie::frameChanged, this, [this](int) {
                if (item && movie) {
                    item->setPixmap(movie->currentPixmap());
                }
            });
            if (isVisible())
                movie->start();
        }
    });
    task->setFuture(QtConcurrent::run([path] {
        QString error;
        QImage image = decodePhoto(path, 0, &error);
        QString details;
        if (!image.isNull() && Formats::isRaw(path)) {
            auto metadataStorage = std::make_unique<LibRaw>();
            auto &metadata = *metadataStorage;
#ifdef Q_OS_WIN
            int status =
                metadata.open_file(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()));
#else
            int status = metadata.open_file(QFile::encodeName(path).constData());
#endif
            if (status == LIBRAW_SUCCESS)
                details = QObject::tr("RAW 相机  %1 %2\n光圈 f/%3 · ISO %4\n快门 %5 秒 · 焦距 %6 mm\nRAW "
                                      "使用内嵌预览；无预览时半尺寸解码。")
                              .arg(QString::fromUtf8(metadata.imgdata.idata.make),
                                   QString::fromUtf8(metadata.imgdata.idata.model))
                              .arg(metadata.imgdata.other.aperture, 0, 'f', 1)
                              .arg(metadata.imgdata.other.iso_speed, 0, 'f', 0)
                              .arg(metadata.imgdata.other.shutter, 0, 'g', 3)
                              .arg(metadata.imgdata.other.focal_len, 0, 'f', 0);
        }
        if (!image.isNull() && QStringList{"jpg", "jpeg"}.contains(QFileInfo(path).suffix().toLower())) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray bytes = f.read(16 * 1024 * 1024);
                easyexif::EXIFInfo exif;
                if (exif.parseFrom(reinterpret_cast<const unsigned char *>(bytes.constData()),
                                   unsigned(bytes.size())) == 0) {
                    details =
                        QObject::tr("相机  %1 %2\n镜头  %3\n光圈  f/%4    ISO %5\n快门  %6 秒    焦距 %7 "
                                    "mm\n拍摄  %8")
                            .arg(QString::fromStdString(exif.Make), QString::fromStdString(exif.Model),
                                 QString::fromStdString(exif.LensInfo.Model),
                                 QString::number(exif.FNumber, 'f', 1), QString::number(exif.ISOSpeedRatings),
                                 QString::number(exif.ExposureTime, 'g', 3),
                                 QString::number(exif.FocalLength, 'f', 0),
                                 QString::fromStdString(exif.DateTimeOriginal));
                }
            }
        }
        return qMakePair(image, image.isNull() ? error : details);
    }));
}
void Viewer::fit() {
    if (!item)
        return;
    fitting = true;
    resetTransform();
    rotate(0);
    fitInView(item, Qt::KeepAspectRatio);
    fittedScale = transform().m11();
    if (qFuzzyIsNull(fittedScale))
        fittedScale = 1;
}
void Viewer::actualSize() {
    if (!item)
        return;
    fitting = false;
    resetTransform();
    centerOn(item);
    sendTransform();
}
void Viewer::rotate(int degrees) {
    if (!item)
        return;
    rotation = (rotation + degrees) % 360;
    item->setTransformOriginPoint(item->boundingRect().center());
    item->setRotation(rotation);
    scene.setSceneRect(item->sceneBoundingRect());
    if (degrees)
        fit();
}
void Viewer::wheelEvent(QWheelEvent *event) {
    if (!item) {
        event->accept();
        return;
    }
    fitting = false;
    double factor = std::pow(1.0015, event->angleDelta().y());
    double current = std::hypot(transform().m11(), transform().m12());
    if (current * factor >= 0.01 && current * factor <= 16)
        scale(factor, factor);
    event->accept();
    sendTransform();
}
void Viewer::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    if (fitting)
        fit();
}
void Viewer::mousePressEvent(QMouseEvent *event) {
    emit activated();
    QGraphicsView::mousePressEvent(event);
}
void Viewer::mouseReleaseEvent(QMouseEvent *event) {
    QGraphicsView::mouseReleaseEvent(event);
    if (item)
        sendTransform();
}
void Viewer::mouseDoubleClickEvent(QMouseEvent *event) {
    Q_UNUSED(event)
    if (!item)
        return;
    if (fitting)
        actualSize();
    else
        fit();
}
void Viewer::sendTransform() {
    if (!item || synchronizing)
        return;
    auto point = mapToScene(viewport()->rect().center());
    auto bounds = item->sceneBoundingRect();
    emit transformChanged(
        std::hypot(transform().m11(), transform().m12()) / fittedScale,
        QPointF((point.x() - bounds.left()) / bounds.width(), (point.y() - bounds.top()) / bounds.height()));
}
void Viewer::syncFrom(double relative, const QPointF &center) {
    if (!item)
        return;
    synchronizing = true;
    fit();
    fitting = false;
    scale(relative, relative);
    auto b = item->sceneBoundingRect();
    centerOn(b.left() + center.x() * b.width(), b.top() + center.y() * b.height());
    synchronizing = false;
}
void Viewer::drawForeground(QPainter *p, const QRectF &rect) {
    Q_UNUSED(rect)
    if (message.isEmpty())
        return;
    p->save();
    p->resetTransform();
    p->setPen(QColor("#9ba7b2"));
    p->drawText(viewport()->rect().adjusted(15, 15, -15, -15), Qt::AlignCenter | Qt::TextWordWrap, message);
    p->restore();
}
void Viewer::toggleAnimation() {
    if (!movie)
        return;
    playback = !playback;
    movie->setPaused(!playback);
}
void Viewer::hideEvent(QHideEvent *event) {
    if (movie)
        movie->setPaused(true);
    QGraphicsView::hideEvent(event);
}
void Viewer::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    if (movie && playback) {
        if (movie->state() == QMovie::NotRunning)
            movie->start();
        else
            movie->setPaused(false);
    }
}
