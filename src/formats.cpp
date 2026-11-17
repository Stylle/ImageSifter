#include "formats.h"
#include <libraw/libraw.h>
#include <memory>
namespace Formats {
QStringList rawExtensions() {
    return {"3fr", "ari", "arw", "bay", "bmq", "cap", "cr2", "cr3", "crw", "cs1", "dc2",
            "dcr", "dng", "erf", "fff", "gpr", "iiq", "k25", "kc2", "kdc", "mdc", "mef",
            "mos", "mrw", "nef", "nrw", "orf", "pef", "ptx", "pxn", "r3d", "raf", "raw",
            "rdc", "rw1", "rw2", "rwl", "rwz", "sr2", "srf", "srw", "sti", "x3f"};
}
bool isRaw(const QString &path) {
    if (rawExtensions().contains(QFileInfo(path).suffix().toLower()))
        return true;
    if (QStringList{"jpg", "jpeg", "jpe", "png", "bmp", "webp", "gif", "avif", "heic", "ico"}.contains(
            QFileInfo(path).suffix().toLower()))
        return false;
    if (!hasRawSignature(path))
        return false;
    auto readerStorage = std::make_unique<LibRaw>();
    auto &reader = *readerStorage;
#ifdef Q_OS_WIN
    int result = reader.open_file(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()));
#else
    int result = reader.open_file(QFile::encodeName(path).constData());
#endif
    return result == LIBRAW_SUCCESS && reader.imgdata.idata.raw_count > 0;
}
bool isJpeg(const QString &path) {
    return QStringList{"jpg", "jpeg", "jpe"}.contains(QFileInfo(path).suffix().toLower());
}
QString category(const QString &path) {
    return isJpeg(path) ? "JPG" : isRaw(path) ? "RAW" : "Images";
}
QString pairKey(const QString &relative) {
    QFileInfo f(relative);
    QString dir = QDir::cleanPath(f.path());
    // Recognize the common sibling JPG/RAW layout when a new project opens already separated files.
    auto components = dir.split('/', Qt::SkipEmptyParts);
    for (int i = 0; i < components.size(); i++)
        if (QStringList{"jpg", "jpeg", "raw"}.contains(components[i].toLower())) {
            components.removeAt(i);
            break;
        }
    dir = components.join('/');
    return QDir::cleanPath(dir + "/" + f.completeBaseName()).toCaseFolded();
}
bool hasRawSignature(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    auto header = f.read(64);
    return header.startsWith("FUJIFILMCCD-RAW") || header.startsWith("FOVb") || header.startsWith("II*\0") ||
           header.startsWith("MM\0*") ||
           (header.mid(4, 4) == "ftyp" && (header.contains("crx") || header.contains("cr3")));
}
} // namespace Formats
