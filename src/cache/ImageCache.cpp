#include "cache/ImageCache.h"

#include <QMutexLocker>

ImageCache::ImageCache(qint64 maxBytes) : m_maxBytes(maxBytes > 0 ? maxBytes : 0) {}

qint64 ImageCache::imageBytes(const QImage& image) {
    return static_cast<qint64>(image.sizeInBytes());
}

QImage ImageCache::get(int key) {
    QMutexLocker locker(&m_mutex);
    const auto it = m_images.constFind(key);
    if (it == m_images.constEnd())
        return {};
    touch(key);
    return it.value();
}

void ImageCache::insert(int key, const QImage& image) {
    if (image.isNull())
        return;
    const qint64 bytes = imageBytes(image);

    QMutexLocker locker(&m_mutex);
    insertLocked(key, image, bytes);
}

bool ImageCache::insertIfGeneration(quint64 generation, int key, const QImage& image) {
    if (image.isNull())
        return false;
    const qint64 bytes = imageBytes(image);

    QMutexLocker locker(&m_mutex);
    if (generation != m_generation)
        return false;  // clear() 之后才完成的过期任务:丢弃
    return insertLocked(key, image, bytes);
}

quint64 ImageCache::generation() const {
    const QMutexLocker locker(&m_mutex);
    return m_generation;
}

bool ImageCache::insertLocked(int key, const QImage& image, qint64 bytes) {
    if (bytes > m_maxBytes)  // 单张就超过总预算:不缓存,否则会把其余全挤掉
        return false;

    const auto existing = m_images.find(key);
    if (existing != m_images.end()) {
        m_totalBytes -= imageBytes(existing.value());
        m_lru.removeOne(key);
    }

    m_images.insert(key, image);
    m_lru.append(key);
    m_totalBytes += bytes;
    evict();
    return true;
}

bool ImageCache::contains(int key) const {
    const QMutexLocker locker(&m_mutex);
    return m_images.contains(key);
}

void ImageCache::clear() {
    const QMutexLocker locker(&m_mutex);
    m_images.clear();
    m_lru.clear();
    m_totalBytes = 0;
    ++m_generation;
}

qint64 ImageCache::totalBytes() const {
    const QMutexLocker locker(&m_mutex);
    return m_totalBytes;
}

int ImageCache::count() const {
    const QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_images.size());
}

void ImageCache::touch(int key) {
    m_lru.removeOne(key);
    m_lru.append(key);
}

void ImageCache::evict() {
    while (m_totalBytes > m_maxBytes && !m_lru.isEmpty()) {
        const int oldest = m_lru.takeFirst();
        const auto it = m_images.find(oldest);
        if (it != m_images.end()) {
            m_totalBytes -= imageBytes(it.value());
            m_images.erase(it);
        }
    }
}
