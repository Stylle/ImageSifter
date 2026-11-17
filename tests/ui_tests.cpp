#include "formats.h"
#include "window.h"
#include <QtTest>
class UiTests : public QObject {
    Q_OBJECT
  private slots:
    void manyRowsAndStaleThumbnails() {
        QTemporaryDir t;
        QImage image(800, 600, QImage::Format_RGB32);
        image.fill(QColor("#69c9b0"));
        auto real = t.path() + "/real.png";
        QVERIFY(image.save(real));
        PhotoModel model;
        QList<Photo> many;
        for (int i = 0; i < 10000; i++)
            many.append({QString::number(i), t.path() + QString("/offline%1.jpg").arg(i),
                         t.path() + QString("/offline%1.jpg").arg(i), "s", QString::number(i) + ".jpg"});
        model.setPhotos(many, t.path() + "/cache");
        QListView view;
        view.setModel(&model);
        view.setViewMode(QListView::IconMode);
        view.setItemDelegate(new PhotoDelegate(&view));
        view.setGridSize(QSize(160, 140));
        view.setUniformItemSizes(true);
        view.resize(900, 600);
        view.show();
        QTest::qWait(30);
        QCOMPARE(model.rowCount(), 10000);
        model.setPhotos({Photo{"new", real, real, "s", "real.png"}}, t.path() + "/new-cache");
        model.data(model.index(0), Qt::DecorationRole);
        QTRY_VERIFY_WITH_TIMEOUT(!model.data(model.index(0), Qt::DecorationRole).value<QPixmap>().isNull(),
                                 10000);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0)).toString(), QString("real.png"));
        QVERIFY(!model.data(model.index(0), PhotoModel::ErrorRole).toBool());
    }
    void rawPreviewAndDetection() {
        QString fixture = QStringLiteral(SIFTER_SOURCE_DIR "/tests/fixtures/synthetic.dng");
        QString error;
        auto image = decodePhoto(fixture, 512, &error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QTemporaryDir t;
        auto renamed = t.path() + "/capture.unusual";
        QVERIFY(QFile::copy(fixture, renamed));
        QVERIFY(Formats::isRaw(renamed));
        QVERIFY(!decodePhoto(renamed, 256, &error).isNull());
        for (auto ext : Formats::rawExtensions())
            QVERIFY(Formats::isRaw("CAPTURE." + ext.toUpper()));
        QVERIFY(!Formats::isRaw(t.path() + "/missing.jpg"));
    }
    void animationLifecycle() {
        Viewer v;
        v.show();
        v.load(QStringLiteral(SIFTER_SOURCE_DIR "/tests/fixtures/animated.gif"));
        QTRY_VERIFY_WITH_TIMEOUT(v.ready(), 10000);
        auto movie = v.findChild<QMovie *>();
        QVERIFY(movie);
        QTRY_COMPARE(movie->state(), QMovie::Running);
        v.toggleAnimation();
        QCOMPARE(movie->state(), QMovie::Paused);
        v.hide();
        v.show();
        QCOMPARE(movie->state(), QMovie::Paused);
        v.toggleAnimation();
        QCOMPARE(movie->state(), QMovie::Running);
        v.load({});
        QVERIFY(!v.findChild<QMovie *>());
    }
    void projectUi() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "城市漫步 · 测试拍摄");
        QDir().mkpath(t.path() + "/photos");
        for (int i = 0; i < 24; i++) {
            QImage img(720 + (i % 3) * 120, 480, QImage::Format_RGB32);
            QPainter p(&img);
            QLinearGradient gradient(0, 0, img.width(), img.height());
            gradient.setColorAt(0, QColor::fromHsv((i * 27) % 360, 95, 150));
            gradient.setColorAt(1, QColor::fromHsv((i * 27 + 60) % 360, 80, 55));
            p.fillRect(img.rect(), gradient);
            p.setPen(QColor(235, 235, 230));
            QFont font;
            font.setPointSize(44);
            p.setFont(font);
            p.drawText(img.rect(), Qt::AlignCenter, QString("PHOTO %1").arg(i + 1));
            p.end();
            QVERIFY(img.save(t.path() + QString("/photos/DSC_%1.jpg").arg(i + 1, 4, 10, QChar('0'))));
        }
        {
            Catalog c;
            c.open(file);
            auto id = c.addSource(t.path() + "/photos");
            auto r = Catalog::scan(t.path() + "/photos", c.root(), c.rejectedDirectory(), true, {});
            c.importPaths(id, r.paths);
        }
        Window w;
        QVERIFY(w.openProject(file));
        w.show();
        QTest::qWait(200);
        auto grid = w.findChild<QListView *>("photoGrid");
        QVERIFY(grid);
        QCOMPARE(grid->model()->rowCount(), 24);
        grid->setCurrentIndex(grid->model()->index(0, 0));
        QTest::mouseClick(w.findChild<QPushButton *>("rating4"), Qt::LeftButton);
        QCOMPARE(grid->model()->index(0, 0).data(PhotoModel::RatingRole).toInt(), 4);
        QTest::mouseClick(w.findChild<QPushButton *>("pick"), Qt::LeftButton);
        QCOMPARE(grid->model()->index(0, 0).data(PhotoModel::FlagRole).toInt(), 1);
        auto search = w.findChild<QLineEdit *>("search");
        search->setFocus();
        QTest::keyClicks(search, "px");
        QCOMPARE(search->text(), QString("px"));
        search->clear();
        QCOMPARE(grid->model()->index(0, 0).data(PhotoModel::FlagRole).toInt(), 1);
        w.findChild<QLineEdit *>("search")->setText("DSC_0001");
        QCOMPARE(grid->model()->rowCount(), 1);
        w.findChild<QLineEdit *>("search")->clear();
        QTest::mouseClick(w.findChild<QPushButton *>("mode1"), Qt::LeftButton);
        auto viewer = w.findChild<Viewer *>("viewer");
        QTRY_VERIFY_WITH_TIMEOUT(viewer->ready(), 10000);
        viewer->actualSize();
        viewer->fit();
        viewer->rotate(90);
        QVERIFY(viewer->ready());
        QTest::mouseClick(w.findChild<QPushButton *>("pin"), Qt::LeftButton);
        auto reference = w.findChild<Viewer *>("reference");
        QTRY_VERIFY_WITH_TIMEOUT(reference->ready(), 10000);
        QTest::mouseClick(w.findChild<QPushButton *>("mode0"), Qt::LeftButton);
        w.resize(1440, 940);
        QTest::qWait(1000);
        auto destination = qEnvironmentVariable("SIFTER_QA_OUTPUT");
        if (!destination.isEmpty()) {
            QDir().mkpath(destination);
            w.grab().save(destination + "/workspace.png");
            QTest::mouseClick(w.findChild<QPushButton *>("mode2"), Qt::LeftButton);
            QTest::qWait(300);
            w.grab().save(destination + "/compare.png");
            w.resize(960, 700);
            QTest::qWait(200);
            w.grab().save(destination + "/compact.png");
        }
        w.close();
    }
    void brokenImageCompletes() {
        QTemporaryDir t;
        QFile f(t.path() + "/broken.jpg");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not an image");
        f.close();
        PhotoModel model;
        model.setPhotos({Photo{"id", f.fileName(), f.fileName(), "s", "broken.jpg"}}, t.path() + "/cache");
        model.data(model.index(0), Qt::DecorationRole);
        QTRY_VERIFY_WITH_TIMEOUT(model.data(model.index(0), PhotoModel::ErrorRole).toBool(), 10000);
        QCOMPARE(model.rowCount(), 1);
        Viewer viewer;
        viewer.load(f.fileName());
        QSignalSpy info(&viewer, &Viewer::imageInfo);
        QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, 10000);
        QVERIFY(!viewer.ready());
        viewer.load({});
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("ImageSifterTests");
    app.setApplicationName("ImageSifterTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QFile f(":/theme.qss");
    if (f.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(f.readAll()));
    UiTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_tests.moc"
