#pragma once
#include <QtCore>
#include <QtSql>
#include <atomic>
#include <memory>

struct Photo {
    QString id, path, original, source, relative;
    int rating = 0, flag = 0, label = 0;
    bool rejected = false;
    QString pair;
};
struct Source {
    QString id, path;
};
struct ScanResult {
    QStringList paths;
    QString error;
    bool cancelled = false;
};
struct OperationResult {
    int succeeded = 0, failed = 0;
    QStringList errors;
};

class Catalog {
  public:
    Catalog();
    ~Catalog();
    Catalog(const Catalog &) = delete;
    Catalog &operator=(const Catalog &) = delete;
    static QString create(const QString &directory, const QString &name);
    void open(const QString &file, bool acquireLock = true, bool initialize = false);
    void close();
    bool isOpen() const {
        return db.isOpen();
    }
    QString file() const {
        return projectFile;
    }
    QString root() const {
        return QFileInfo(projectFile).absolutePath();
    }
    QString name() const {
        return projectName;
    }
    QString rejectedDirectory() const;
    void setRejectedDirectory(const QString &directory);
    QList<Photo> photos() const;
    QList<Source> sources() const;
    QString addSource(const QString &path);
    int importPaths(const QString &sourceId, const QStringList &paths);
    void relinkSource(const QString &sourceId, const QString &path);
    void setMark(const QStringList &ids, const QString &field, int value);
    QVariant setting(const QString &key, const QVariant &fallback = {}) const;
    void setSetting(const QString &key, const QVariant &value);
    QString backup();
    OperationResult relocate(const QStringList &ids, bool restore, bool includeRaw,
                             const std::shared_ptr<std::atomic_bool> &cancel = {});
    QList<Photo> expandedSelection(const QStringList &ids, bool includePairs) const;
    OperationResult organize(const QStringList &ids, const QString &destination,
                             const std::shared_ptr<std::atomic_bool> &cancel = {});
    OperationResult undoOrganization(const std::shared_ptr<std::atomic_bool> &cancel = {});
    QStringList unresolvedOperations() const;
    QStringList operationHistory() const;
    static ScanResult scan(const QString &source, const QString &project, const QString &rejected,
                           bool recursive, const std::shared_ptr<std::atomic_bool> &cancel);
    static bool isWithin(const QString &path, const QString &directory);

  private:
    QSqlDatabase db;
    QString connection, projectFile, projectName;
    std::unique_ptr<QLockFile> lock;
    void execute(const QString &sql, const QVariantList &params = {}) const;
    void recover();
    void moveOne(const Photo &photo, const QString &source, const QString &target, bool restore, bool primary,
                 bool organize = false);
};
