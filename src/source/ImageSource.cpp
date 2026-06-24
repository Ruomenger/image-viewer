#include "source/ImageSource.h"

#include <QFileInfo>
#include <QImageReader>
#include <QSet>

bool ImageSource::isSupportedImage(const QString& name)
{
    static const QSet<QString> kExtensions = [] {
        QSet<QString> set;
        for (const QByteArray& fmt : QImageReader::supportedImageFormats())
            set.insert(QString::fromLatin1(fmt).toLower());
        // Common aliases QImageReader reports as "jpeg".
        set.insert(QStringLiteral("jpg"));
        set.insert(QStringLiteral("jpeg"));
        set.insert(QStringLiteral("jpe"));
        return set;
    }();

    const QString suffix = QFileInfo(name).suffix().toLower();
    return !suffix.isEmpty() && kExtensions.contains(suffix);
}

bool ImageSource::isArchive(const QString& path)
{
    static const QSet<QString> kArchiveExtensions = {
        QStringLiteral("zip"), QStringLiteral("cbz"),
        QStringLiteral("7z"),  QStringLiteral("cb7"),
        QStringLiteral("rar"), QStringLiteral("cbr"),
        QStringLiteral("tar"), QStringLiteral("cbt"),
    };
    return kArchiveExtensions.contains(QFileInfo(path).suffix().toLower());
}
