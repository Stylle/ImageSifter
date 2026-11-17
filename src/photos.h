#pragma once
#include "catalog.h"
#include <QtConcurrent>
#include <QtWidgets>

class PhotoModel : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Role {
        PhotoRole = Qt::UserRole + 1,
        PathRole,
        RatingRole,
        FlagRole,
        LabelRole,
        RejectedRole,
        ErrorRole
    };
    explicit PhotoModel(QObject *parent = nullptr);
    ~PhotoModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    void setPhotos(QList<Photo> photos, const QString &cacheDirectory);
    const Photo &photo(int row) const {
        return items.at(row);
    }
    QFileInfo fileInfo(int row) const {
        return metadata.value(items.at(row).path);
    }
    void applyMarks(const QStringList &ids, const QString &field, int value);
    QStringList ids() const;
  signals:
    void decodeStatus(int running);

  private:
    QList<Photo> items;
    QHash<QString, QFileInfo> metadata;
    QString cacheDirectory;
    mutable QCache<QString, QPixmap> thumbnails{96 * 1024};
    mutable QSet<QString> failed, queued;
    mutable QStringList pending;
    mutable QHash<QFutureWatcher<QImage> *, QString> watchers;
    mutable int generation = 0;
    void request(const QString &path) const;
    void pump() const;
};

class PhotoFilter : public QSortFilterProxyModel {
  public:
    enum Category { All, Unreviewed, Picked, Rejected, Trash, Offline };
    explicit PhotoFilter(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {
        setDynamicSortFilter(true);
        collator.setNumericMode(true);
    }
    Category category = All;
    int stars = 0, color = 0, order = 0, format = 0;
    QString text, source;
    void refresh() {
        invalidateFilter();
        sort(0);
    }

  protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &a, const QModelIndex &b) const override;

  private:
    QCollator collator;
};

class PhotoDelegate : public QStyledItemDelegate {
  public:
    explicit PhotoDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

QImage decodePhoto(const QString &path, int edge, QString *error = nullptr);

class PhotoGrid : public QListView {
  public:
    explicit PhotoGrid(QWidget *parent = nullptr) : QListView(parent) {}
    void setThumbnailWidth(int value) {
        preferred = value;
        updateCells();
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QListView::resizeEvent(event);
        updateCells();
    }

  private:
    int preferred = 230;
    void updateCells() {
        int available = qMax(120, viewport()->width());
        int columns = qMax(1, qRound(available / double(preferred)));
        int width = qMax(100, available / columns - 1);
        QSize cell(width, width * 3 / 4 + 47);
        if (gridSize() != cell)
            setGridSize(cell);
    }
};