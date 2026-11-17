#pragma once
#include <QtCore>

namespace Formats {
QStringList rawExtensions();
bool isRaw(const QString &path);
bool isJpeg(const QString &path);
QString category(const QString &path);
QString pairKey(const QString &relative);
bool hasRawSignature(const QString &path);
} // namespace Formats
