#pragma once
#include <QtConcurrent>
#include <QtWidgets>

class Viewer : public QGraphicsView {
    Q_OBJECT
  public:
    explicit Viewer(QWidget *parent = nullptr);
    ~Viewer() override;
    void load(const QString &path);
    QString path() const {
        return currentPath;
    }
    void fit();
    void actualSize();
    void rotate(int degrees);
    void toggleAnimation();
    void syncFrom(double relativeScale, const QPointF &center);
    bool ready() const {
        return item != nullptr;
    }
  signals:
    void activated();
    void imageInfo(const QString &path, const QSize &size, const QString &details);
    void transformChanged(double relativeScale, const QPointF &center);

  protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void drawForeground(QPainter *p, const QRectF &rect) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

  private:
    QGraphicsScene scene;
    QGraphicsPixmapItem *item = nullptr;
    QMovie *movie = nullptr;
    QString currentPath, message;
    int generation = 0, rotation = 0;
    double fittedScale = 1;
    bool fitting = true, synchronizing = false, playback = true;
    QList<QFutureWatcher<QPair<QImage, QString>> *> tasks;
    void sendTransform();
};
