#include "cache/ImageCache.h"

#include <QMutexLocker>

#include <algorithm>
#include <limits>

ImageCache::ImageCache(qint64 fitBytes, qint64 thumbBytes) {
    lane(Tier::Fit).maxBytes = std::max<qint64>(fitBytes, 0);
    lane(Tier::Thumb).maxBytes = std::max<qint64>(thumbBytes, 0);
    Lane& full = lane(Tier::Full);
    full.maxBytes = std::numeric_limits<qint64>::max();
    full.maxCount = 1;
}

qint64 ImageCache::imageBytes(const DecodedImage& image) {
    return static_cast<qint64>(image.image.sizeInBytes()) + image.animation.size();
}

std::optional<DecodedImage> ImageCache::get(const ImageKey& key) {
    const QMutexLocker locker(&m_mutex);
    Lane& l = lane(key.tier);
    const auto it = l.images.constFind(key.entry);
    if (it == l.images.constEnd())
        return std::nullopt;
    l.lru.removeOne(key.entry);
    l.lru.append(key.entry);
    return it.value();
}

void ImageCache::insert(const ImageKey& key, const DecodedImage& image) {
    if (image.image.isNull())
        return;
    const qint64 bytes = imageBytes(image);

    const QMutexLocker locker(&m_mutex);
    Lane& l = lane(key.tier);
    if (bytes > l.maxBytes)
        return;

    const auto existing = l.images.constFind(key.entry);
    if (existing != l.images.constEnd()) {
        l.totalBytes -= imageBytes(existing.value());
        l.lru.removeOne(key.entry);
    }
    l.images.insert(key.entry, image);
    l.lru.append(key.entry);
    l.totalBytes += bytes;
    evict(l);
}

bool ImageCache::contains(const ImageKey& key) const {
    const QMutexLocker locker(&m_mutex);
    return lane(key.tier).images.contains(key.entry);
}

void ImageCache::clear() {
    const QMutexLocker locker(&m_mutex);
    for (Lane& l : m_lanes) {
        l.images.clear();
        l.lru.clear();
        l.totalBytes = 0;
    }
}

void ImageCache::clear(Tier tier) {
    const QMutexLocker locker(&m_mutex);
    Lane& l = lane(tier);
    l.images.clear();
    l.lru.clear();
    l.totalBytes = 0;
}

void ImageCache::setMaxBytes(Tier tier, qint64 maxBytes) {
    const QMutexLocker locker(&m_mutex);
    Lane& l = lane(tier);
    l.maxBytes = std::max<qint64>(maxBytes, 0);
    evict(l);
}

qint64 ImageCache::maxBytes(Tier tier) const {
    const QMutexLocker locker(&m_mutex);
    return lane(tier).maxBytes;
}

qint64 ImageCache::totalBytes(Tier tier) const {
    const QMutexLocker locker(&m_mutex);
    return lane(tier).totalBytes;
}

int ImageCache::count(Tier tier) const {
    const QMutexLocker locker(&m_mutex);
    return static_cast<int>(lane(tier).images.size());
}

void ImageCache::evict(Lane& lane) {
    const auto over = [&lane] {
        return lane.totalBytes > lane.maxBytes ||
               (lane.maxCount > 0 && lane.images.size() > lane.maxCount);
    };
    while (over() && !lane.lru.isEmpty()) {
        const EntryKey oldest = lane.lru.takeFirst();
        const auto it = lane.images.find(oldest);
        if (it != lane.images.end()) {
            lane.totalBytes -= imageBytes(it.value());
            lane.images.erase(it);
        }
    }
}
