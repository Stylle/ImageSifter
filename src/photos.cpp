#include "photos.h"
#include "exif.h"
#include "formats.h"
#include <libraw/libraw.h>

QImage decodePhoto(const QString &path, int edge, QString *error) {
    if (Formats::isRaw(path)) {
        auto rawStorage = std::make_unique<LibRaw>();
        auto &raw = *rawStorage;
        raw.imgdata.rawparams.max_raw_memory_mb = 512;
        int status = 0;
#ifdef Q_OS_WIN
        status = raw.open_file(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()));
#else
        status = raw.open_file(QFile::encodeName(path).constData());
#endif
        if (status != LIBRAW_SUCCESS) {
            if (error)
                *error = QString::fromLatin1(libraw_strerror(status));
            return {};
        }
        QImage image;
        if (raw.unpack_thumb() == LIBRAW_SUCCESS) {
            auto thumb = raw.dcraw_make_mem_thumb(&status);
            if (thumb) {
                if (thumb->type == LIBRAW_IMAGE_JPEG)
                    image = QImage::fromData(thumb->data, int(thumb->data_size), "JPEG");
                else if (thumb->type == LIBRAW_IMAGE_BITMAP && thumb->colors == 3 && thumb->bits == 8)
                    image = QImage(thumb->data, thumb->width, thumb->height, thumb->width * 3,
                                   QImage::Format_RGB888)
                                .copy();
                LibRaw::dcraw_clear_mem(thumb);
            }
        }
        if (image.isNull()) {
            raw.imgdata.params.output_bps = 8;
            raw.imgdata.params.use_camera_wb = 1;
            raw.imgdata.params.half_size = 1;
            if (raw.unpack() == LIBRAW_SUCCESS && raw.dcraw_process() == LIBRAW_SUCCESS) {
                auto bitmap = raw.dcraw_make_mem_image(&status);
                if (bitmap) {
                    if (bitmap->type == LIBRAW_IMAGE_BITMAP && bitmap->colors == 3 && bitmap->bits == 8)
                        image = QImage(bitmap->data, bitmap->width, bitmap->height, bitmap->width * 3,
                                       QImage::Format_RGB888)
                                    .copy();
                    LibRaw::dcraw_clear_mem(bitmap);
                }
            }
        } else {
            QTransform orientation;
            if (raw.imgdata.sizes.flip == 3)
                orientation.rotate(180);
            else if (raw.imgdata.sizes.flip == 5)
                orientation.rotate(-90);
            else if (raw.imgdata.sizes.flip == 6)
                orientation.rotate(90);
            image = image.transformed(orientation);
        }
        if (!image.isNull() && edge > 0)
            image = image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (image.isNull() && error)
            *error = QObject::tr("RAW 已识别，但当前编码无法预览");
        return image;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (size.isValid() && (size.width() > 100000 || size.height() > 100000)) {
        if (error)
            *error = QObject::tr("图片尺寸超过安全限制");
        return {};
    }
    if (edge > 0 && size.isValid())
        reader.setScaledSize(size.scaled(edge, edge, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull()) {
        // Some cameras store a JPEG preview in RAW containers; unsupported containers remain explicit
        // failures.
        if (error)
            *error = reader.errorString();
    } else if (image.colorSpace().isValid())
        image.convertToColorSpace(QColorSpace::SRgb);
    return image;
}
PhotoModel::PhotoModel(QObject *parent) : QAbstractListModel(parent) {}
PhotoModel::~PhotoModel() {
    for (auto w : watchers.keys()) {
        disconnect(w, nullptr, this, nullptr);
        w->waitForFinished();
    }
}
int PhotoModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(items.size());
}
QVariant PhotoModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= items.size())
        return {};
    const auto &p = items[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        return QFileInfo(p.original).fileName();
    case Qt::ToolTipRole:
        return p.path;
    case Qt::DecorationRole:
        if (auto pix = thumbnails.object(p.path))
            return *pix;
        if (!failed.contains(p.path))
            request(p.path);
        return {};
    case PathRole:
        return p.path;
    case RatingRole:
        return p.rating;
    case FlagRole:
        return p.flag;
    case LabelRole:
        return p.label;
    case RejectedRole:
        return p.rejected;
    case ErrorRole:
        return failed.contains(p.path);
    default:
        return {};
    }
}
void PhotoModel::setPhotos(QList<Photo> photos, const QString &cache) {
    beginResetModel();
    generation++;
    pending.clear();
    queued.clear();
    failed.clear();
    thumbnails.clear();
    items = std::move(photos);
    metadata.clear();
    for (const auto &p : items) {
        QFileInfo f(p.path);
        f.refresh();
        metadata.insert(p.path, f);
    }
    cacheDirectory = cache;
    endResetModel();
}
QStringList PhotoModel::ids() const {
    QStringList r;
    for (const auto &p : items)
        r.append(p.id);
    return r;
}
void PhotoModel::applyMarks(const QStringList &ids, const QString &field, int value) {
    for (int row = 0; row < items.size(); row++)
        if (ids.contains(items[row].id)) {
            if (field == "rating")
                items[row].rating = value;
            if (field == "flag")
                items[row].flag = value;
            if (field == "label")
                items[row].label = value;
            emit dataChanged(index(row), index(row));
        }
}
void PhotoModel::request(const QString &path) const {
    if (queued.contains(path) || pending.size() > 512)
        return;
    queued.insert(path);
    pending.prepend(path);
    pump();
}
void PhotoModel::pump() const {
    auto self = const_cast<PhotoModel *>(this);
    while (watchers.size() < 4 && !pending.isEmpty()) {
        QString path = pending.takeFirst(), cache = cacheDirectory;
        int requestGeneration = generation;
        auto watcher = new QFutureWatcher<QImage>(self);
        watchers.insert(watcher, path);
        connect(watcher, &QFutureWatcher<QImage>::finished, self, [self, watcher, path, requestGeneration] {
            QImage image = watcher->result();
            self->watchers.remove(watcher);
            watcher->deleteLater();
            if (requestGeneration == self->generation) {
                self->queued.remove(path);
                if (image.isNull())
                    self->failed.insert(path);
                else
                    self->thumbnails.insert(path, new QPixmap(QPixmap::fromImage(image)),
                                            qMax(1, int(image.sizeInBytes() / 1024)));
                for (int r = 0; r < self->items.size(); r++)
                    if (self->items[r].path == path) {
                        emit self->dataChanged(self->index(r), self->index(r),
                                               {Qt::DecorationRole, ErrorRole});
                        break;
                    }
            }
            emit self->decodeStatus(self->watchers.size());
            self->pump();
        });
        watcher->setFuture(QtConcurrent::run([path, cache] {
            QFileInfo info(path);
            if (!info.isFile())
                return QImage();
            // Only disposable hash-named previews are evicted, never originals or project records.
            static QMutex cacheMutex;
            static QHash<QString, int> pruneRequests;
            {
                QMutexLocker guard(&cacheMutex);
                if (!cache.isEmpty() && pruneRequests[cache]++ % 64 == 0) {
                    auto files = QDir(cache).entryInfoList({"*.png"}, QDir::Files | QDir::NoSymLinks,
                                                           QDir::Time | QDir::Reversed);
                    qint64 total = 0;
                    for (const auto &f : files)
                        total += f.size();
                    for (const auto &f : files) {
                        if (total <= 512LL * 1024 * 1024)
                            break;
                        if (QRegularExpression("^[a-f0-9]{64}\\.png$").match(f.fileName()).hasMatch() &&
                            QFile::remove(f.absoluteFilePath()))
                            total -= f.size();
                    }
                }
            }
            QByteArray key = (path + QString::number(info.size()) +
                              QString::number(info.lastModified().toMSecsSinceEpoch()))
                                 .toUtf8();
            QString cached =
                cache + "/" +
                QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex()) +
                ".png";
            QImage image;
            if (image.load(cached))
                return image;
            image = decodePhoto(path, 512);
            if (!image.isNull() && !cache.isEmpty()) {
                QDir().mkpath(cache);
                QSaveFile output(cached);
                if (output.open(QIODevice::WriteOnly) && image.save(&output, "PNG"))
                    output.commit();
            }
            return image;
        }));
    }
}
bool PhotoFilter::filterAcceptsRow(int row, const QModelIndex &parent) const {
    auto m = static_cast<PhotoModel *>(sourceModel());
    const auto &p = m->photo(row);
    if (format == 1 && !Formats::isJpeg(p.path))
        return false;
    if (format == 2 && !Formats::isRaw(p.path))
        return false;
    if (format == 3 && (Formats::isJpeg(p.path) || Formats::isRaw(p.path)))
        return false;
    Q_UNUSED(parent)
    if (category == Trash) {
        if (!p.rejected)
            return false;
    } else if (p.rejected)
        return false;
    if (category == Unreviewed && p.flag != 0)
        return false;
    if (category == Picked && p.flag != 1)
        return false;
    if (category == Rejected && p.flag != -1)
        return false;
    if (category == Offline && m->fileInfo(row).exists())
        return false;
    return p.rating >= stars && (color == 0 || p.label == color) &&
           (source.isEmpty() || p.source == source) &&
           (text.isEmpty() || QFileInfo(p.path).fileName().contains(text, Qt::CaseInsensitive));
}
bool PhotoFilter::lessThan(const QModelIndex &a, const QModelIndex &b) const {
    auto m = static_cast<PhotoModel *>(sourceModel());
    auto &pa = m->photo(a.row()), &pb = m->photo(b.row());
    QFileInfo fa = m->fileInfo(a.row()), fb = m->fileInfo(b.row());
    if (order == 1 && fa.lastModified() != fb.lastModified())
        return fa.lastModified() < fb.lastModified();
    if (order == 2 && pa.rating != pb.rating)
        return pa.rating > pb.rating;
    if (order == 3 && fa.size() != fb.size())
        return fa.size() > fb.size();
    auto compare = collator.compare(fa.fileName(), fb.fileName());
    return compare ? compare < 0 : pa.path < pb.path;
}
void PhotoDelegate::paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QRect card = o.rect.adjusted(5, 5, -5, -5);
    bool selected = o.state & QStyle::State_Selected, hover = o.state & QStyle::State_MouseOver;
    p->setPen(QPen(selected ? QColor("#69c9b0") : QColor(hover ? "#59616a" : "#363c43"), selected ? 2 : 1));
    p->setBrush(QColor(selected ? "#263c3a" : hover ? "#30373d" : "#242a30"));
    p->drawRoundedRect(card, 8, 8);
    QRect imageRect = card.adjusted(9, 9, -9, -47);
    auto pix = i.data(Qt::DecorationRole).value<QPixmap>();
    if (!pix.isNull()) {
        QSize size = pix.size();
        size.scale(imageRect.size(), Qt::KeepAspectRatio);
        QRect dst(
            QPoint(imageRect.center().x() - size.width() / 2, imageRect.center().y() - size.height() / 2),
            size);
        p->drawPixmap(dst, pix);
    } else {
        p->setPen(QColor("#88949e"));
        p->drawText(imageRect, Qt::AlignCenter,
                    i.data(PhotoModel::ErrorRole).toBool() ? QObject::tr("无法预览 / 离线")
                                                           : QObject::tr("正在载入…"));
    }
    p->setPen(QColor("#edf0f2"));
    QFont font = o.font;
    font.setPointSizeF(9);
    p->setFont(font);
    QRect name = card.adjusted(10, card.height() - 41, -10, -21);
    p->drawText(name, Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(font).elidedText(i.data().toString(), Qt::ElideMiddle, name.width() - 20));
    int rating = i.data(PhotoModel::RatingRole).toInt(), flag = i.data(PhotoModel::FlagRole).toInt();
    p->setPen(QColor("#e6be76"));
    p->drawText(card.adjusted(10, card.height() - 23, -10, -4), Qt::AlignLeft | Qt::AlignVCenter,
                QString(rating, QChar(0x2605)));
    QStringList colors{"#69717a", "#dc7373", "#e8b86b", "#69c9b0", "#73a8e9", "#b294d8"};
    int label = qBound(0, i.data(PhotoModel::LabelRole).toInt(), 5);
    p->setBrush(QColor(colors[label]));
    p->setPen(Qt::NoPen);
    p->drawEllipse(QPoint(card.right() - 14, card.bottom() - 14), 4, 4);
    if (flag) {
        p->setPen(QColor(flag > 0 ? "#69c9b0" : "#ef9696"));
        font.setBold(true);
        p->setFont(font);
        p->drawText(card.adjusted(card.width() - 35, 5, -8, -card.height() + 28), Qt::AlignCenter,
                    flag > 0 ? QString::fromUtf8("✓") : QString::fromUtf8("×"));
    }
    p->restore();
}
