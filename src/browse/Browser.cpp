#include "browse/Browser.h"

#include "decode/ImageDecoder.h"
#include "source/ArchiveSource.h"
#include "source/FolderSource.h"
#include "source/ImageSource.h"

#include <QFileInfo>

#include <algorithm>

Browser::Browser(qint64 cacheBytes) : m_cache(cacheBytes), m_scheduler(m_cache) {}

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
    m_scheduler.setSource(m_source);  // 丢弃旧来源的排队任务、清空缓存
    m_direction = Direction::Forward;
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

    const ImageKey key{.entry = {.source = m_source->id(), .entry = m_index}, .tier = Tier::Fit};
    std::optional<DecodedImage> decoded = m_cache.get(key);
    if (!decoded) {
        decoded = decodeImage(m_source->readEntry(m_index), m_source->entryName(m_index),
                              m_scheduler.target(Tier::Fit));
        m_cache.insert(key, *decoded);  // 解码失败是空图,insert 内部忽略
    }
    m_scheduler.focus(m_index, m_direction);  // 坏图也预读邻页,便于快速跳过
    return decoded->image;
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

void Browser::next() {
    m_direction = Direction::Forward;
    goTo(m_index + 1);
}

void Browser::prev() {
    m_direction = Direction::Backward;
    goTo(m_index - 1);
}
