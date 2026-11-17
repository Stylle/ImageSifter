#include "catalog.h"
#include "formats.h"
#include <QtTest>

class CatalogTests : public QObject {
    Q_OBJECT
    static void write(const QString &p, const QByteArray &bytes = "fixture") {
        QDir().mkpath(QFileInfo(p).path());
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(bytes), bytes.size());
    }
  private slots:
    void missingCatalogNotReinitialized() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "Missing database");
        QVERIFY(QFile::rename(t.path() + "/project/catalog.sqlite", t.path() + "/project/catalog.saved"));
        Catalog c;
        QVERIFY_EXCEPTION_THROWN(c.open(file), std::runtime_error);
        QVERIFY(!QFileInfo::exists(t.path() + "/project/catalog.sqlite"));
        QVERIFY(QFileInfo::exists(t.path() + "/project/catalog.saved"));
    }
    void verifiedCopyAndUndoOrganization() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "Copy transport");
        auto original = t.path() + "/source/a.jpg";
        write(original, QByteArray(2 * 1024 * 1024, 'x'));
        Catalog c;
        c.open(file);
        c.setSetting("verifiedCopy", 1);
        auto source = c.addSource(t.path() + "/source");
        c.importPaths(source, {original});
        auto id = c.photos().first().id;
        auto moved = c.organize({id}, t.path() + "/sorted");
        QVERIFY2(moved.succeeded == 1, qPrintable(moved.errors.join("\n")));
        QVERIFY(!QFileInfo::exists(original));
        QCOMPARE(QFileInfo(c.photos().first().path).size(), qint64(2 * 1024 * 1024));
        QCOMPARE(c.undoOrganization().succeeded, 1);
        QVERIFY(QFileInfo::exists(original));
        QCOMPARE(c.undoOrganization().succeeded, 0);
        QVERIFY(c.unresolvedOperations().isEmpty());
    }
    void movedProjectRetainsTrash() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "Moved project");
        write(t.path() + "/source/a.jpg");
        Catalog c;
        c.open(file);
        auto source = c.addSource(t.path() + "/source");
        c.importPaths(source, {t.path() + "/source/a.jpg"});
        auto id = c.photos().first().id;
        QCOMPARE(c.organize({id}, c.root()).succeeded, 1);
        QCOMPARE(c.relocate({id}, false, false).succeeded, 1);
        c.close();
        QVERIFY(QDir().rename(t.path() + "/project", t.path() + "/renamed"));
        c.open(t.path() + "/renamed/Project.imagesifter");
        QVERIFY(QFileInfo::exists(c.photos().first().path));
        QVERIFY(c.photos().first().path.contains("/renamed/Rejected/"));
        QCOMPARE(c.relocate({id}, true, false).succeeded, 1);
    }
    void ambiguousPairNotGuessed() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "Ambiguous");
        QStringList paths;
        for (auto ext : QStringList{"jpg", "nef", "dng"}) {
            auto p = t.path() + "/source/a." + ext;
            write(p);
            paths << p;
        }
        Catalog c;
        c.open(file);
        auto source = c.addSource(t.path() + "/source");
        c.importPaths(source, paths);
        QCOMPARE(c.expandedSelection({c.photos().first().id}, true).size(), 1);
    }
    void schemaUpgradePreservesMarks() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "Migration");
        write(t.path() + "/source/a.jpg");
        {
            Catalog c;
            c.open(file);
            auto source = c.addSource(t.path() + "/source");
            c.importPaths(source, {t.path() + "/source/a.jpg"});
            c.setMark({c.photos().first().id}, "rating", 5);
        }
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "migration-fixture");
            db.setDatabaseName(t.path() + "/project/catalog.sqlite");
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("ALTER TABLE photos DROP COLUMN pairkey"));
            QVERIFY(q.exec("ALTER TABLE operations DROP COLUMN kind"));
            QVERIFY(q.exec("PRAGMA user_version=1"));
            db.close();
        }
        QSqlDatabase::removeDatabase("migration-fixture");
        Catalog c;
        c.open(file);
        QCOMPARE(c.photos().first().rating, 5);
        QVERIFY(!c.photos().first().pair.isEmpty());
        QVERIFY(!QDir(t.path() + "/project/backups").entryList({"*.sqlite"}, QDir::Files).isEmpty());
    }
    void organizeAndPairedCleanup() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "Organize");
        auto jpg = t.path() + "/source/day/a.JPG", raw = t.path() + "/source/day/a.CR3";
        write(jpg, "jpeg");
        write(raw, "raw");
        Catalog c;
        c.open(file);
        auto source = c.addSource(t.path() + "/source");
        c.importPaths(source, {jpg, raw});
        auto id = c.photos().first().id;
        QCOMPARE(c.expandedSelection({id}, true).size(), 2);
        auto organized = c.organize({c.photos()[0].id, c.photos()[1].id}, t.path() + "/sorted");
        QCOMPARE(organized.succeeded, 2);
        QVERIFY(!QFileInfo::exists(jpg));
        QVERIFY(!QFileInfo::exists(raw));
        auto jpegPath = c.photos().first().path;
        QVERIFY(jpegPath.contains("/JPG/"));
        QVERIFY(c.photos().last().path.contains("/RAW/"));
        c.close();
        c.open(file);
        for (int i = 0; i < 2; i++) {
            auto removed = c.relocate({id}, false, true);
            QCOMPARE(removed.succeeded, 2);
            for (auto p : c.photos())
                QVERIFY(p.rejected);
            QVERIFY(c.photos().first().path.contains("/Rejected/JPG/"));
            QVERIFY(c.photos().last().path.contains("/Rejected/RAW/"));
            auto restored = c.relocate({id}, true, true);
            QCOMPARE(restored.succeeded, 2);
            QVERIFY(QFileInfo::exists(jpegPath));
        }
        auto jpegOnly = c.relocate({id}, false, false);
        QCOMPARE(jpegOnly.succeeded, 1);
        QVERIFY(c.photos().first().rejected);
        QVERIFY(!c.photos().last().rejected);
        QVERIFY(QFileInfo::exists(c.photos().last().path));
        QCOMPARE(c.relocate({id}, true, true).succeeded, 1);
    }
    void alreadySeparatedPairing() {
        QCOMPARE(Formats::pairKey("JPG/day/a.jpg"), Formats::pairKey("RAW/day/a.NEF"));
        QCOMPARE(Formats::pairKey("day/JPG/a.jpg"), Formats::pairKey("day/RAW/a.ARW"));
    }
    void persistenceAndIsolation() {
        QTemporaryDir t;
        QVERIFY(t.isValid());
        auto a = Catalog::create(t.path() + "/one", "One"), b = Catalog::create(t.path() + "/two", "Two");
        write(t.path() + "/photos/a.JPG");
        Catalog c;
        c.open(a);
        auto source = c.addSource(t.path() + "/photos");
        QCOMPARE(c.importPaths(source, {t.path() + "/photos/a.JPG"}), 1);
        auto id = c.photos().first().id;
        c.setMark({id}, "rating", 5);
        c.setMark({id}, "flag", 1);
        c.setMark({id}, "label", 3);
        c.setSetting("mode", 2);
        c.close();
        c.open(a);
        QCOMPARE(c.photos().first().rating, 5);
        QCOMPARE(c.photos().first().flag, 1);
        QCOMPARE(c.photos().first().label, 3);
        QCOMPARE(c.setting("mode").toInt(), 2);
        c.close();
        c.open(b);
        QVERIFY(c.photos().isEmpty());
    }
    void projectLockAndInvalidInput() {
        QTemporaryDir t;
        auto f = Catalog::create(t.path() + "/project", "Test");
        Catalog c;
        c.open(f);
        Catalog other;
        QVERIFY_EXCEPTION_THROWN(other.open(f), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(c.setMark({}, "path", 1), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(c.setMark({}, "rating", 7), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(Catalog::create(t.path() + "/project", "Overwrite"), std::runtime_error);
    }
    void scanExcludesProjectAndTrash() {
        QTemporaryDir t;
        write(t.path() + "/source/image.BMP");
        write(t.path() + "/source/sub/b.jpg");
        write(t.path() + "/source/project/cache.png");
        write(t.path() + "/source/Rejected/deleted.jpg");
        auto token = std::make_shared<std::atomic_bool>(false);
        auto r = Catalog::scan(t.path() + "/source", t.path() + "/source/project",
                               t.path() + "/source/Rejected", true, token);
        QCOMPARE(r.paths.size(), 2);
        token->store(true);
        QVERIFY(Catalog::scan(t.path() + "/source", t.path() + "/p", t.path() + "/r", true, token).cancelled);
    }
    void moveAndRestoreWithRaw() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "Move");
        QString jpg = t.path() + "/photos/a.JPG", raw = t.path() + "/photos/a.NEF";
        write(jpg, "jpeg-fixture");
        write(raw, "raw-fixture");
        Catalog c;
        c.open(file);
        auto s = c.addSource(t.path() + "/photos");
        c.importPaths(s, {jpg, raw});
        auto id = c.photos().first().id;
        auto r = c.relocate({id}, false, true);
        QCOMPARE(r.succeeded, 2);
        QVERIFY(!QFileInfo::exists(jpg));
        QVERIFY(!QFileInfo::exists(raw));
        QVERIFY(c.photos().first().rejected);
        auto target = c.photos().first().path;
        QVERIFY(QFileInfo::exists(target));
        r = c.relocate({id}, true, true);
        QCOMPARE(r.succeeded, 2);
        QVERIFY(QFileInfo::exists(jpg));
        QVERIFY(QFileInfo::exists(raw));
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(!c.photos().first().rejected);
    }
    void restoreDoesNotOverwrite() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "Conflict");
        QString image = t.path() + "/photos/a.jpg";
        write(image, "original");
        Catalog c;
        c.open(file);
        auto s = c.addSource(t.path() + "/photos");
        c.importPaths(s, {image});
        auto id = c.photos().first().id;
        c.relocate({id}, false, false);
        write(image, "new");
        auto r = c.relocate({id}, true, false);
        QCOMPARE(r.failed, 1);
        QVERIFY(c.photos().first().rejected);
        QFile f(image);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("new"));
    }
    void missingFileNeverVanishes() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "Missing");
        QString p = t.path() + "/photos/a.jpg";
        write(p);
        Catalog c;
        c.open(file);
        auto s = c.addSource(t.path() + "/photos");
        c.importPaths(s, {p});
        auto id = c.photos().first().id;
        QFile::remove(p);
        auto result = c.relocate({id}, false, false);
        QCOMPARE(result.failed, 1);
        QCOMPARE(c.photos().size(), 1);
        QVERIFY(!c.photos().first().rejected);
    }
    void backupAndRelink() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "Backup");
        write(t.path() + "/photos/a.jpg");
        write(t.path() + "/replacement/a.jpg");
        Catalog c;
        c.open(file);
        auto s = c.addSource(t.path() + "/photos");
        c.importPaths(s, {t.path() + "/photos/a.jpg"});
        auto id = c.photos().first().id;
        c.setMark({id}, "rating", 4);
        QVERIFY(QFileInfo::exists(c.backup()));
        c.relinkSource(s, t.path() + "/replacement");
        QVERIFY(c.photos().first().path.endsWith("replacement/a.jpg"));
        QCOMPARE(c.photos().first().rating, 4);
    }
    void interruptedRenameRecovered() {
        QTemporaryDir t;
        auto file = Catalog::create(t.path() + "/project", "Recovery");
        QString original = t.path() + "/photos/a.jpg";
        write(original, "intact");
        QString id, target;
        {
            Catalog c;
            c.open(file);
            auto s = c.addSource(t.path() + "/photos");
            c.importPaths(s, {original});
            id = c.photos().first().id;
            target = t.path() + "/project/Rejected/a.jpg";
            QDir().mkpath(QFileInfo(target).path());
            QVERIFY(QFile::rename(original, target));
        }
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "test-inject");
            db.setDatabaseName(t.path() + "/project/catalog.sqlite");
            QVERIFY(db.open());
            QSqlQuery q(db);
            q.prepare("INSERT INTO operations VALUES('pending',?,?,?,?,'prepared',0,1,'','trash')");
            q.addBindValue(id);
            q.addBindValue(original);
            q.addBindValue(target);
            q.addBindValue(QCryptographicHash::hash("intact", QCryptographicHash::Sha256));
            QVERIFY(q.exec());
            db.close();
        }
        QSqlDatabase::removeDatabase("test-inject");
        Catalog c;
        c.open(file);
        QVERIFY(c.photos().first().rejected);
        QCOMPARE(c.photos().first().path, target);
        QVERIFY(c.unresolvedOperations().isEmpty());
    }
};
QTEST_GUILESS_MAIN(CatalogTests)
#include "catalog_tests.moc"
