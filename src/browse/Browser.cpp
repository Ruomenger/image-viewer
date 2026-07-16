#include "browse/Browser.h"

#include "decode/ImageDecoder.h"
#include "source/ArchiveSource.h"
#include "source/FolderSource.h"
#include "source/ImageSource.h"

#include <QFileInfo>

#include <algorithm>

Browser::Browser(qint64 cacheBytes) : m_cache(cacheBytes), m_prefetcher(m_cache) {}

bool Browser::open(const QString& path, QString* error) {
    const QFileInfo info(path);
    std::shared_ptr<ImageSource> source;
    int initialIndex = 0;

    if (info.isDir()) {
        source = std::make_shared<FolderSource>(path);
    } else if (ImageSource::isArchive(path)) {
        source = std::make_shared<ArchiveSource>(path);
    } else if (info.isFile()) {
        auto folder = std::make_shared<FolderSource>(info.absolutePath());
        initialIndex = std::max(0, folder->indexOf(info.absoluteFilePath()));
        source = folder;
    } else {
        if (error)
            *error = QStringLiteral("路径不存在");
        return false;
    }

    if (!source->openError().isEmpty()) {
        if (error)
            *error = source->openError();
        return false;
    }
    if (source->count() == 0) {
        if (error)
            *error = QStringLiteral("没有可显示的图片");
        return false;
    }

    m_source = std::move(source);
    m_cache.clear();                   // 递增代次:旧来源的在途预读结果一并作废
    m_prefetcher.setSource(m_source);  // 同时作废旧来源尚未开始的预读任务
    goTo(initialIndex);
    return true;
}

int Browser::count() const {
    return m_source ? m_source->count() : 0;
}

QString Browser::currentName() const {
    return m_source ? m_source->entryName(m_index) : QString();
}

QImage Browser::currentImage() {
    if (!m_source)
        return {};

    QImage image = m_cache.get(m_index);
    if (image.isNull()) {
        image = decodeImage(m_source->readEntry(m_index), m_source->entryName(m_index));
        if (!image.isNull())
            m_cache.insert(m_index, image);
    }
    m_prefetcher.prefetchAround(m_index);  // 坏图也预读邻页,便于快速跳过
    return image;
}

QByteArray Browser::currentAnimation() {
    if (!m_source)
        return {};
    if (!maybeAnimatedName(m_source->entryName(m_index)))
        return {};

    QByteArray bytes = m_source->readEntry(m_index);
    if (!isAnimatedImage(bytes))
        return {};
    return bytes;
}

void Browser::goTo(int index) {
    if (!m_source)
        return;
    const int n = m_source->count();
    if (n <= 0)
        return;
    m_index = ((index % n) + n) % n;  // wrap around both ends
}
