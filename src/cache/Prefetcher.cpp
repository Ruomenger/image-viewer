#include "cache/Prefetcher.h"

#include "cache/ImageCache.h"
#include "decode/ImageDecoder.h"
#include "source/ImageSource.h"

#include <QImage>
#include <QMutexLocker>
#include <QRunnable>
#include <QSet>

namespace {
// 把任意整数索引规整到 [0, n)(n > 0),实现环绕。
int wrapIndex(int index, int n) {
    return ((index % n) + n) % n;
}
}  // namespace

Prefetcher::Prefetcher(ImageCache& cache, int ahead, int behind)
    : m_cache(cache), m_ahead(ahead > 0 ? ahead : 1), m_behind(behind >= 0 ? behind : 0) {
    m_pool.setMaxThreadCount(2);  // 后台预读,不抢满 CPU
    // LibRaw/dav1d 解码栈深远超 macOS 次线程默认的 512KB,按主线程规格给足,
    // 否则预读 RAW/AVIF 邻页时池线程栈溢出(SIGBUS)。
    m_pool.setStackSize(8 * 1024 * 1024);
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

    const quint64 generation = m_cache.generation();
    m_pool.clear();  // 丢弃尚未开始的过期任务,只保留当前邻域

    // 先排主导方向(向前),再排向后;小来源环绕可能重叠,去重。
    QSet<int> queued{wrapIndex(currentIndex, n)};  // 当前页不预读
    const auto enqueue = [&](int rawIndex) {
        const int index = wrapIndex(rawIndex, n);
        if (queued.contains(index))
            return;
        queued.insert(index);
        submit(index, source, epoch, generation);
    };
    for (int d = 1; d <= m_ahead; ++d)
        enqueue(currentIndex + d);
    for (int d = 1; d <= m_behind; ++d)
        enqueue(currentIndex - d);
}

void Prefetcher::submit(int index, const std::shared_ptr<ImageSource>& source, int epoch,
                        quint64 generation) {
    if (m_cache.contains(index))
        return;

    ImageCache* cache = &m_cache;
    QAtomicInt* epochPtr = &m_epoch;

    // 按值捕获 source:lambda 持有 shared_ptr 副本,保证来源在解码期间存活。
    m_pool.start(QRunnable::create([cache, epochPtr, source, index, epoch, generation] {
        if (epochPtr->loadAcquire() != epoch)
            return;  // epoch 校验只是省掉无谓的读取/解码;正确性由代次校验兜底
        if (cache->contains(index))
            return;
        const QByteArray bytes = source->readEntry(index);
        if (epochPtr->loadAcquire() != epoch)
            return;
        const QImage image = decodeImage(bytes, source->entryName(index));
        if (image.isNull())
            return;
        cache->insertIfGeneration(generation, index, image);
    }));
}
