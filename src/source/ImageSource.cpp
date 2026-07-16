#include "source/ImageSource.h"

#include "decode/ImageDecoder.h"

#include <QFileInfo>
#include <QSet>

bool ImageSource::isSupportedImage(const QString& name) {
    const QString suffix = QFileInfo(name).suffix().toLower();
    return !suffix.isEmpty() && decodableExtensions().contains(suffix);
}

bool ImageSource::isArchive(const QString& path) {
    static const QSet<QString> kArchiveExtensions = {
        QStringLiteral("zip"), QStringLiteral("cbz"), QStringLiteral("7z"),  QStringLiteral("cb7"),
        QStringLiteral("rar"), QStringLiteral("cbr"), QStringLiteral("tar"), QStringLiteral("cbt"),
    };
    return kArchiveExtensions.contains(QFileInfo(path).suffix().toLower());
}
