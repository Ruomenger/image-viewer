#include "source/ImageSource.h"

#include "decode/ImageDecoder.h"

#include <QFileInfo>
#include <QSet>

#include <atomic>

namespace {
std::atomic<quint64> g_nextSourceId{1};
}  // namespace

ImageSource::ImageSource() : m_id(g_nextSourceId.fetch_add(1, std::memory_order_relaxed)) {}

bool ImageSource::waitForListing(const std::function<bool(int)>& /*progress*/) {
    return true;  // 列表在构造时已建好
}

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
