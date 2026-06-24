#include "cache/Prefetcher.h"

#include "cache/ImageCache.h"
#include "source/ImageSource.h"

#include <QImage>
#include <QMutexLocker>
#include <QRunnable>

namespace {
// 把任意整数索引规整到 [0, n)(n > 0),实现环绕。
int wrapIndex(int index, int n) {
    return ((index % n) + n) % n;
}
}  // namespace

Prefetcher::Prefetcher(ImageCache& cache, int radius)
    : m_cache(cache), m_radius(radius > 0 ? radius : 1) {
    m_pool.setMaxThreadCount(2);  // 后台预读,不抢满 CPU
}

Prefetcher::~Prefetcher() = default;  // m_pool 析构会 waitForDone

void Prefetcher::setSource(std::shared_ptr<ImageSource> source) {
    const QMutexLocker locker(&m_mutex);
    m_epoch.fetchAndAddOrdered(1);  // 旧来源的在途任务作废
    m_pool.clear();
    m_source = std::move(source);
}

void Prefetcher::cancelAll() {
    const QMutexLocker locker(&m_mutex);
    m_epoch.fetchAndAddOrdered(1);
    m_pool.clear();
}

void Prefetcher::prefetchAround(int currentIndex) {
    std::shared_ptr<ImageSource> source;
    int epoch = 0;
    {
        const QMutexLocker locker(&m_mutex);
        source = m_source;
        epoch = m_epoch.loadAcquire();
    }
    if (!source)
        return;
    const int n = source->count();
    if (n <= 1)
        return;

    m_pool.clear();  // 丢弃尚未开始的过期任务,只保留当前邻域
    for (int d = 1; d <= m_radius; ++d) {
        submit(wrapIndex(currentIndex + d, n), source, epoch);
        submit(wrapIndex(currentIndex - d, n), source, epoch);
    }
}

void Prefetcher::submit(int index, const std::shared_ptr<ImageSource>& source, int epoch) {
    if (m_cache.contains(index))
        return;

    ImageCache* cache = &m_cache;
    QAtomicInt* epochPtr = &m_epoch;

    // 按值捕获 source:lambda 持有 shared_ptr 副本,保证来源在解码期间存活。
    m_pool.start(QRunnable::create([cache, epochPtr, source, index, epoch] {
        if (epochPtr->loadAcquire() != epoch)
            return;
        if (cache->contains(index))
            return;
        const QByteArray bytes = source->readEntry(index);
        if (epochPtr->loadAcquire() != epoch)
            return;  // 解码前再查,快速跳过已作废任务
        const QImage image = QImage::fromData(bytes);
        if (image.isNull() || epochPtr->loadAcquire() != epoch)
            return;
        cache->insert(index, image);
    }));
}
