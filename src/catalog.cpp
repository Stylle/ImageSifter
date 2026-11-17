#include "catalog.h"
#include "formats.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <stdexcept>

namespace {
[[noreturn]] void fail(const QString &s) {
    throw std::runtime_error(s.toUtf8().constData());
}
QString normalized(const QString &p) {
    return QDir::cleanPath(QFileInfo(p).absoluteFilePath());
}
QString uniquePath(QString path) {
    if (!QFileInfo::exists(path))
        return path;
    QFileInfo f(path);
    return f.path() + "/" + f.completeBaseName() + "-" + QUuid::createUuid().toString(QUuid::Id128) + "." +
           f.suffix();
}
QByteArray digest(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        fail(QObject::tr("无法读取文件：%1").arg(path));
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f))
        fail(QObject::tr("文件校验失败：%1").arg(path));
    return h.result();
}

} // namespace
Catalog::Catalog() : connection(QUuid::createUuid().toString(QUuid::Id128)) {}
Catalog::~Catalog() {
    close();
}
void Catalog::close() {
    if (db.isValid()) {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);
    }
    lock.reset();
    projectFile.clear();
    projectName.clear();
}
void Catalog::execute(const QString &sql, const QVariantList &params) const {
    QSqlQuery q(db);
    q.prepare(sql);
    for (const auto &p : params)
        q.addBindValue(p);
    if (!q.exec())
        fail(QObject::tr("工程数据库操作失败：%1").arg(q.lastError().text()));
}
QString Catalog::create(const QString &directory, const QString &name) {
    if (name.trimmed().isEmpty())
        fail(QObject::tr("请输入工程名称"));
    QDir dir(directory);
    if (!dir.exists() && !QDir().mkpath(directory))
        fail(QObject::tr("无法创建工程目录"));
    if (!dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty())
        fail(QObject::tr("请选择空文件夹，避免覆盖已有工程"));
    QSaveFile f(dir.filePath("Project.imagesifter"));
    if (!f.open(QIODevice::WriteOnly))
        fail(QObject::tr("无法写入工程文件"));
    auto bytes =
        QJsonDocument(QJsonObject{{"format", "ImageSifter"}, {"version", 1}, {"name", name.trimmed()}})
            .toJson();
    if (f.write(bytes) != bytes.size() || !f.commit())
        fail(QObject::tr("保存工程失败"));
    Catalog c;
    c.open(f.fileName(), true, true);
    return f.fileName();
}
void Catalog::open(const QString &file, bool acquireLock, bool initialize) {
    close();
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        fail(QObject::tr("无法打开工程文件"));
    QJsonParseError pe;
    auto obj = QJsonDocument::fromJson(f.read(65536), &pe).object();
    if (pe.error != QJsonParseError::NoError || obj.value("format") != "ImageSifter" ||
        obj.value("version").toInt() != 1)
        fail(QObject::tr("工程格式不支持，请使用兼容版本"));
    projectFile = normalized(file);
    projectName = obj.value("name").toString();
    if (acquireLock) {
        lock = std::make_unique<QLockFile>(root() + "/.imagesifter.lock");
        lock->setStaleLockTime(0);
        if (!lock->tryLock(0)) {
            close();
            fail(QObject::tr("工程已在另一个窗口打开"));
        }
    }
    if (!initialize && !QFileInfo::exists(root() + "/catalog.sqlite")) {
        close();
        fail(QObject::tr("工程数据库缺失，请恢复完整工程；不会新建空数据库替代已有记录"));
    }
    db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(root() + "/catalog.sqlite");
    if (!db.open()) {
        auto error = db.lastError().text();
        close();
        fail(error);
    }
    execute("PRAGMA busy_timeout=5000");
    execute("PRAGMA journal_mode=WAL");
    execute("PRAGMA synchronous=FULL");
    QSqlQuery version(db);
    version.exec("PRAGMA user_version");
    version.next();
    int schema = version.value(0).toInt();
    version.finish();
    if (schema > 2) {
        close();
        fail(QObject::tr("工程由更新的程序创建，请先升级"));
    }
    if (!initialize) {
        const auto tables = db.tables();
        for (auto name : QStringList{"photos", "sources", "settings", "operations"})
            if (!tables.contains(name))
                fail(QObject::tr("工程数据库不完整，请从备份恢复；未重建缺失表"));
    }
    execute("CREATE TABLE IF NOT EXISTS sources(id TEXT PRIMARY KEY,path TEXT NOT NULL UNIQUE)");
    execute(
        "CREATE TABLE IF NOT EXISTS photos(id TEXT PRIMARY KEY,path TEXT NOT NULL UNIQUE,original TEXT NOT "
        "NULL,source TEXT NOT NULL,relative TEXT NOT NULL,rating INTEGER NOT NULL DEFAULT 0,flag INTEGER NOT "
        "NULL DEFAULT 0,label INTEGER NOT NULL DEFAULT 0,rejected INTEGER NOT NULL DEFAULT 0)");
    execute("CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY,value TEXT NOT NULL)");
    execute("CREATE TABLE IF NOT EXISTS operations(id TEXT PRIMARY KEY,photo TEXT NOT NULL,source TEXT NOT "
            "NULL,target TEXT NOT NULL,digest BLOB NOT NULL,state TEXT NOT NULL,restore INTEGER NOT "
            "NULL,primaryfile INTEGER NOT NULL,error TEXT NOT NULL DEFAULT '')");
    if (schema < 2) {
        if (schema == 1)
            backup();
        if (!db.transaction())
            fail(db.lastError().text());
        try {
            execute("ALTER TABLE photos ADD COLUMN pairkey TEXT NOT NULL DEFAULT ''");
            execute("ALTER TABLE operations ADD COLUMN kind TEXT NOT NULL DEFAULT 'trash'");
            for (const auto &p : photos())
                execute("UPDATE photos SET pairkey=? WHERE id=?",
                        {p.source + "/" + Formats::pairKey(p.relative), p.id});
            execute("PRAGMA user_version=2");
            if (!db.commit())
                fail(db.lastError().text());
        } catch (...) {
            db.rollback();
            throw;
        }
    }
    if (acquireLock) {
        auto previous = setting("projectRoot").toString();
        if (!previous.isEmpty() && normalized(previous) != normalized(root())) {
            if (!db.transaction())
                fail(db.lastError().text());
            try {
                auto rebase = [&](const QString &path) {
                    return isWithin(path, previous)
                               ? QDir(root()).filePath(QDir(previous).relativeFilePath(path))
                               : path;
                };
                for (auto p : photos())
                    execute("UPDATE photos SET path=?,original=? WHERE id=?",
                            {rebase(p.path), rebase(p.original), p.id});
                QSqlQuery q(db);
                q.exec("SELECT id,source,target FROM operations");
                QList<QStringList> rows;
                while (q.next())
                    rows.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString()});
                q.finish();
                for (auto row : rows)
                    execute("UPDATE operations SET source=?,target=? WHERE id=?",
                            {rebase(row[1]), rebase(row[2]), row[0]});
                auto rejected = setting("rejectedDirectory").toString();
                if (!rejected.isEmpty())
                    setSetting("rejectedDirectory", rebase(rejected));
                if (!db.commit())
                    fail(db.lastError().text());
            } catch (...) {
                db.rollback();
                throw;
            }
        }
        setSetting("projectRoot", root());
        recover();
    }
    QDir().mkpath(root() + "/previews");
}
QString Catalog::rejectedDirectory() const {
    return setting("rejectedDirectory", root() + "/Rejected").toString();
}
void Catalog::setRejectedDirectory(const QString &directory) {
    if (directory.isEmpty())
        fail(QObject::tr("弃片目录不能为空"));
    if (isWithin(root(), directory))
        fail(QObject::tr("请使用专用子目录，不能选择工程根目录或其上级"));
    for (auto s : sources())
        if (normalized(s.path) == normalized(directory) || isWithin(s.path, directory))
            fail(QObject::tr("弃片目录不能包含照片来源目录"));
    for (auto p : photos())
        if (p.rejected)
            fail(QObject::tr("请先恢复现有弃片，再更改弃片目录"));
    setSetting("rejectedDirectory", normalized(directory));
}
QList<Photo> Catalog::photos() const {
    QList<Photo> result;
    QSqlQuery q(db);
    if (!q.exec("SELECT id,path,original,source,relative,rating,flag,label,rejected,pairkey FROM photos"))
        fail(q.lastError().text());
    while (q.next())
        result.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
                       q.value(3).toString(), q.value(4).toString(), q.value(5).toInt(), q.value(6).toInt(),
                       q.value(7).toInt(), q.value(8).toBool(), q.value(9).toString()});
    return result;
}
QList<Source> Catalog::sources() const {
    QList<Source> r;
    QSqlQuery q(db);
    q.exec("SELECT id,path FROM sources ORDER BY rowid");
    while (q.next())
        r.append({q.value(0).toString(), q.value(1).toString()});
    return r;
}
QString Catalog::addSource(const QString &path) {
    QFileInfo info(path);
    QString p = info.canonicalFilePath();
    if (!info.isDir() || info.isSymLink() || p.isEmpty())
        fail(QObject::tr("来源目录不存在或是符号链接"));
    if (isWithin(p, root()) || isWithin(p, rejectedDirectory()))
        fail(QObject::tr("工程和弃片目录不能作为照片来源"));
    for (auto s : sources()) {
        if (normalized(s.path) == p)
            return s.id;
        if (isWithin(p, s.path) || isWithin(s.path, p))
            fail(QObject::tr("此目录与已有来源重叠，请刷新已有来源"));
    }
    QString id = QUuid::createUuid().toString(QUuid::Id128);
    execute("INSERT INTO sources VALUES(?,?)", {id, p});
    return id;
}
int Catalog::importPaths(const QString &sourceId, const QStringList &paths) {
    QString source;
    for (auto s : sources())
        if (s.id == sourceId)
            source = s.path;
    if (source.isEmpty())
        fail(QObject::tr("来源不存在"));
    if (!db.transaction())
        fail(db.lastError().text());
    int count = 0;
    try {
        for (const QString &path : paths) {
            auto p = normalized(path);
            if (!isWithin(p, source) || isWithin(p, root()) || isWithin(p, rejectedDirectory()) ||
                QFileInfo(p).isSymLink())
                continue;
            QSqlQuery exists(db);
            exists.prepare("SELECT id FROM photos WHERE original=? OR path=?");
            exists.addBindValue(p);
            exists.addBindValue(p);
            exists.exec();
            if (exists.next())
                continue;
            execute(
                "INSERT OR IGNORE INTO photos(id,path,original,source,relative,pairkey) VALUES(?,?,?,?,?,?)",
                {QUuid::createUuid().toString(QUuid::Id128), p, p, sourceId, QDir(source).relativeFilePath(p),
                 sourceId + "/" + Formats::pairKey(QDir(source).relativeFilePath(p))});
            count++;
        }
        if (!db.commit())
            fail(db.lastError().text());
    } catch (...) {
        db.rollback();
        throw;
    }
    return count;
}
void Catalog::relinkSource(const QString &sourceId, const QString &path) {
    QString previous;
    for (auto s : sources())
        if (s.id == sourceId)
            previous = s.path;
    if (previous.isEmpty())
        fail(QObject::tr("来源不存在"));
    QString next = QFileInfo(path).canonicalFilePath();
    if (next.isEmpty() || !QFileInfo(next).isDir())
        fail(QObject::tr("新目录不存在"));
    if (isWithin(next, root()) || isWithin(next, rejectedDirectory()))
        fail(QObject::tr("不能定位到工程或弃片目录"));
    for (auto s : sources())
        if (s.id != sourceId && (isWithin(next, s.path) || isWithin(s.path, next)))
            fail(QObject::tr("来源目录重叠"));
    if (!db.transaction())
        fail(db.lastError().text());
    try {
        for (auto p : photos())
            if (p.source == sourceId && isWithin(p.original, previous)) {
                QString target = QDir(next).filePath(QDir(previous).relativeFilePath(p.original));
                execute(
                    "UPDATE photos SET original=?,path=CASE WHEN rejected=0 THEN ? ELSE path END WHERE id=?",
                    {target, target, p.id});
            }
        execute("UPDATE sources SET path=? WHERE id=?", {next, sourceId});
        if (!db.commit())
            fail(db.lastError().text());
    } catch (...) {
        db.rollback();
        throw;
    }
}
void Catalog::setMark(const QStringList &ids, const QString &field, int value) {
    if (!QStringList{"rating", "flag", "label"}.contains(field))
        fail("Invalid mark field");
    if ((field == "rating" && (value < 0 || value > 5)) || (field == "flag" && (value < -1 || value > 1)) ||
        (field == "label" && (value < 0 || value > 5)))
        fail("Invalid mark value");
    if (!db.transaction())
        fail(db.lastError().text());
    try {
        for (const auto &id : ids)
            execute("UPDATE photos SET " + field + "=? WHERE id=?", {value, id});
        if (!db.commit())
            fail(db.lastError().text());
    } catch (...) {
        db.rollback();
        throw;
    }
}
QVariant Catalog::setting(const QString &key, const QVariant &fallback) const {
    QSqlQuery q(db);
    q.prepare("SELECT value FROM settings WHERE key=?");
    q.addBindValue(key);
    q.exec();
    return q.next() ? QVariant(q.value(0).toString()) : fallback;
}
void Catalog::setSetting(const QString &key, const QVariant &value) {
    execute("INSERT OR REPLACE INTO settings VALUES(?,?)", {key, value.toString()});
}
QString Catalog::backup() {
    if (!QDir().mkpath(root() + "/backups"))
        fail(QObject::tr("无法创建备份目录"));
    QString target = root() + "/backups/catalog-" +
                     QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "-" +
                     QUuid::createUuid().toString(QUuid::Id128).left(6) + ".sqlite";
    execute("VACUUM INTO ?", {target});
    return target;
}
bool Catalog::isWithin(const QString &path, const QString &directory) {
    auto p = normalized(path), d = normalized(directory);
#ifdef Q_OS_WIN
    p = p.toCaseFolded();
    d = d.toCaseFolded();
#endif
    return p == d || p.startsWith(d + "/");
}
ScanResult Catalog::scan(const QString &source, const QString &project, const QString &rejected,
                         bool recursive, const std::shared_ptr<std::atomic_bool> &cancel) {
    ScanResult result;
    if (!QFileInfo(source).isDir()) {
        result.error = QObject::tr("照片来源离线或不存在");
        return result;
    }
    QStringList extensions =
        Formats::rawExtensions() +
        QStringList{"jpg", "jpeg", "jpe", "png", "bmp", "webp", "gif", "tif", "tiff", "avif", "heic"};
    QList<QDir> pending{QDir(source)};
    while (!pending.isEmpty()) {
        if (cancel && cancel->load()) {
            result.cancelled = true;
            break;
        }
        QDir current = pending.takeLast();
        if (!QFileInfo(current.path()).isReadable()) {
            result.error = QObject::tr("部分目录没有读取权限");
            continue;
        }
        for (auto info : current.entryInfoList(
                 QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
            if (cancel && cancel->load()) {
                result.cancelled = true;
                break;
            }
            auto p = info.absoluteFilePath();
            if (isWithin(p, project) || isWithin(p, rejected))
                continue;
            if (info.isDir()) {
                if (recursive)
                    pending.append(QDir(p));
            } else if (extensions.contains(info.suffix().toLower()) || Formats::isRaw(p))
                result.paths.append(p);
        }
    }
    return result;
}
void Catalog::moveOne(const Photo &photo, const QString &source, const QString &target, bool restore,
                      bool primary, bool organize) {
    QFileInfo info(source);
    if (!info.exists() || !info.isFile() || info.isSymLink())
        fail(QObject::tr("源文件不存在或不是普通文件：%1").arg(source));
    if (QFileInfo::exists(target))
        fail(QObject::tr("目标文件已存在，未覆盖：%1").arg(target));
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        fail(QObject::tr("无法创建目标目录"));
    QByteArray hash = digest(source);
    QString id = QUuid::createUuid().toString(QUuid::Id128);
    execute("INSERT INTO operations(id,photo,source,target,digest,state,restore,primaryfile,error,kind) "
            "VALUES(?,?,?,?,?,'prepared',?,?,'',?)",
            {id, photo.id, source, target, hash, restore, primary, organize ? "organize" : "trash"});
    try {
        bool renamed = false;
#ifdef Q_OS_WIN
        if (!setting("verifiedCopy", 0).toInt())
            renamed = MoveFileExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(source).utf16()),
                                  reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(target).utf16()),
                                  MOVEFILE_WRITE_THROUGH);
#endif
        if (!renamed) {
            QFile input(source);
            if (!input.open(QIODevice::ReadOnly))
                fail(QObject::tr("无法读取源文件"));
            QTemporaryFile output(target + ".imagesifter-XXXXXX");
            if (!output.open())
                fail(QObject::tr("目标目录不可写"));
            while (!input.atEnd()) {
                auto block = input.read(1024 * 1024);
                if (block.isEmpty() && input.error() != QFile::NoError)
                    fail(QObject::tr("读取失败"));
                if (output.write(block) != block.size())
                    fail(QObject::tr("写入失败，可能空间不足"));
            }
            if (!output.flush())
                fail(QObject::tr("无法完成目标文件写入"));
            QString temporary = output.fileName();
            output.close();
            if (digest(temporary) != hash || digest(source) != hash)
                fail(QObject::tr("校验不一致，保留源文件，请检查操作记录"));
            // Rename through the QTemporaryFile owner: it retains a Windows handle after close().
            bool published = output.rename(target);
            if (!published)
                fail(QObject::tr("无法完成目标文件写入，已有目标不会覆盖"));
            output.setAutoRemove(false);
            execute("UPDATE operations SET state='target_ready' WHERE id=?", {id});
            input.close();
            if (!QFile::remove(source))
                fail(QObject::tr("目标已保存，但无法移除源文件，两个文件均保留"));
        } else if (digest(target) != hash)
            fail(QObject::tr("移动后文件发生变化，请检查操作记录"));
        if (!db.transaction())
            fail(db.lastError().text());
        try {
            if (organize)
                execute("UPDATE photos SET path=?,original=?,rejected=0 WHERE path=?",
                        {target, target, source});
            else
                execute("UPDATE photos SET path=?,rejected=? WHERE path=?", {target, !restore, source});
            execute("UPDATE operations SET state='done' WHERE id=?", {id});
            if (!db.commit())
                fail(db.lastError().text());
        } catch (...) {
            db.rollback();
            throw;
        }
    } catch (const std::exception &e) {
        execute("UPDATE operations SET error=? WHERE id=?", {QString::fromUtf8(e.what()), id});
        throw;
    }
}

QList<Photo> Catalog::expandedSelection(const QStringList &ids, bool includePairs) const {
    const auto all = photos();
    QHash<QString, QPair<int, int>> counts;
    for (auto p : all) {
        if (Formats::isJpeg(p.path))
            counts[p.pair].first++;
        else if (Formats::isRaw(p.path))
            counts[p.pair].second++;
    }
    QSet<QString> pairs;
    for (auto p : all)
        if (ids.contains(p.id) && !p.pair.isEmpty() && (Formats::isJpeg(p.path) || Formats::isRaw(p.path)))
            if (counts[p.pair].first == 1 && counts[p.pair].second == 1)
                pairs.insert(p.pair);
    QList<Photo> result;
    for (auto p : all)
        if (ids.contains(p.id) ||
            (includePairs && pairs.contains(p.pair) && (Formats::isJpeg(p.path) || Formats::isRaw(p.path))))
            result.append(p);
    return result;
}
OperationResult Catalog::relocate(const QStringList &ids, bool restore, bool includePairs,
                                  const std::shared_ptr<std::atomic_bool> &cancel) {
    recover();
    OperationResult result;
    if (!unresolvedOperations().isEmpty()) {
        result.failed = 1;
        result.errors << QObject::tr("请先核对未完成的文件操作");
        return result;
    }
    auto selected = expandedSelection(ids, includePairs);
    // Check every restore target before moving any member. A collision cannot split a pair.
    if (restore)
        for (auto p : selected)
            if (p.rejected && QFileInfo::exists(p.original)) {
                result.failed = 1;
                result.errors << QObject::tr("原位置已存在文件，未恢复：%1").arg(p.original);
                return result;
            }
    for (auto p : selected) {
        if (cancel && cancel->load())
            break;
        if (p.rejected != restore)
            continue;
        try {
            QString target;
            if (restore)
                target = p.original;
            else
                target = uniquePath(rejectedDirectory() + "/" + Formats::category(p.original) + "/" +
                                    p.source.left(8) + "/" + p.relative);
            if (!restore && (QDir::isAbsolutePath(p.relative) || !isWithin(target, rejectedDirectory())))
                fail(QObject::tr("工程中的相对路径无效，未移动文件"));
            moveOne(p, p.path, target, restore, true);
            result.succeeded++;
        } catch (const std::exception &e) {
            result.failed++;
            result.errors << QString::fromUtf8(e.what());
        }
    }
    return result;
}
OperationResult Catalog::organize(const QStringList &ids, const QString &destination,
                                  const std::shared_ptr<std::atomic_bool> &cancel) {
    recover();
    OperationResult result;
    if (!unresolvedOperations().isEmpty()) {
        result.failed = 1;
        result.errors << QObject::tr("请先核对未完成的文件操作");
        return result;
    }
    if (isWithin(destination, rejectedDirectory()) ||
        isWithin(rejectedDirectory(), QDir(destination).filePath("JPG")) ||
        isWithin(rejectedDirectory(), QDir(destination).filePath("RAW"))) {
        result.failed = 1;
        result.errors << QObject::tr("整理目录与弃片目录不能相互包含");
        return result;
    }
    for (auto p : photos()) {
        if (cancel && cancel->load())
            break;
        if (p.rejected || !ids.contains(p.id) || (!Formats::isRaw(p.path) && !Formats::isJpeg(p.path)))
            continue;
        try {
            QString target = QDir::cleanPath(destination + "/" + Formats::category(p.path) + "/" +
                                             p.source.left(8) + "/" + p.relative);
            if (QDir::isAbsolutePath(p.relative) || !isWithin(target, destination))
                fail(QObject::tr("工程中的相对路径无效，未整理文件"));
            if (normalized(target) == normalized(p.path))
                continue;
            moveOne(p, p.path, uniquePath(target), false, true, true);
            result.succeeded++;
        } catch (const std::exception &e) {
            result.failed++;
            result.errors << QString::fromUtf8(e.what());
        }
    }
    return result;
}
OperationResult Catalog::undoOrganization(const std::shared_ptr<std::atomic_bool> &cancel) {
    recover();
    OperationResult result;
    QSqlQuery q(db);
    q.exec("SELECT photo,source,target FROM operations WHERE kind='organize' AND state='done' ORDER BY rowid "
           "DESC");
    struct Row {
        QString id, source, target;
    };
    QList<Row> rows;
    while (q.next())
        rows.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString()});
    q.finish();
    QSet<QString> processed;
    for (auto row : rows) {
        if (cancel && cancel->load())
            break;
        if (processed.contains(row.id))
            continue;
        processed.insert(row.id);
        auto all = photos();
        for (auto p : all)
            if (p.id == row.id && !p.rejected && p.path == row.target) {
                try {
                    moveOne(p, row.target, row.source, false, true, true);
                    execute("UPDATE operations SET kind='undo-organize' WHERE rowid=(SELECT MAX(rowid) FROM "
                            "operations WHERE photo=?)",
                            {p.id});
                    execute("UPDATE operations SET state='undone' WHERE photo=? AND source=? AND target=? "
                            "AND kind='organize' AND state='done'",
                            {p.id, row.source, row.target});
                    result.succeeded++;
                } catch (const std::exception &e) {
                    result.failed++;
                    result.errors << QString::fromUtf8(e.what());
                }
                break;
            }
    }
    return result;
}
void Catalog::recover() {
    QSqlQuery q(db);
    q.exec("SELECT id,photo,source,target,digest,restore,primaryfile,kind FROM operations WHERE state NOT IN "
           "('done','cancelled','undone')");
    struct Pending {
        QString id, photo, source, target;
        QByteArray hash;
        bool restore, primary;
        QString kind;
    };
    QList<Pending> pending;
    while (q.next())
        pending.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
                        q.value(3).toString(), q.value(4).toByteArray(), q.value(5).toBool(),
                        q.value(6).toBool(), q.value(7).toString()});
    q.finish();
    for (auto p : pending) {
        bool s = QFileInfo::exists(p.source), t = QFileInfo::exists(p.target);
        try {
            if (!s && t && digest(p.target) == p.hash) {
                if (!db.transaction())
                    fail(db.lastError().text());
                try {
                    if (p.kind == "organize")
                        execute("UPDATE photos SET path=?,original=?,rejected=0 WHERE path=?",
                                {p.target, p.target, p.source});
                    else
                        execute("UPDATE photos SET path=?,rejected=? WHERE path=?",
                                {p.target, !p.restore, p.source});
                    execute("UPDATE operations SET state='done',error='' WHERE id=?", {p.id});
                    if (!db.commit())
                        fail(db.lastError().text());
                } catch (...) {
                    db.rollback();
                    throw;
                }
            } else if (s && !t)
                execute("UPDATE operations SET state='cancelled',error='未完成移动，源文件保留' WHERE id=?",
                        {p.id});
            else
                execute("UPDATE operations SET "
                        "state='review',error='请核对来源和目标；程序不会自动删除任一副本' WHERE id=?",
                        {p.id});
        } catch (...) {
            execute("UPDATE operations SET state='review' WHERE id=?", {p.id});
        }
    }
}
QStringList Catalog::unresolvedOperations() const {
    QStringList r;
    QSqlQuery q(db);
    q.exec("SELECT source,target,error FROM operations WHERE state NOT IN ('done','cancelled','undone')");
    while (q.next())
        r.append(QObject::tr("来源：%1\n目标：%2\n%3")
                     .arg(q.value(0).toString(), q.value(1).toString(), q.value(2).toString()));
    return r;
}
QStringList Catalog::operationHistory() const {
    QStringList result;
    QSqlQuery q(db);
    q.exec("SELECT state,source,target,error FROM operations ORDER BY rowid DESC LIMIT 200");
    while (q.next())
        result.append(QObject::tr("[%1]\n来源：%2\n目标：%3\n%4")
                          .arg(q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
                               q.value(3).toString()));
    return result;
}
