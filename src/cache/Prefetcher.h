#pragma once

#include <QAtomicInt>
#include <QMutex>
#include <QThreadPool>

#include <memory>

class ImageCache;
class ImageSource;

// 在后台线程预解码「当前索引前后各若干张」并写入 ImageCache,
// 使翻页命中缓存、免去同步解码延迟。
//
// - setSource()：切换来源时提升 epoch 使在途任务作废,避免旧来源的图片落入新缓存。
// - prefetchAround()：每次导航调用,先丢弃队列中尚未开始的过期任务,再排入当前邻域。
// - 线程安全;析构时等待在途任务结束(成员声明顺序保证 m_pool 最先析构)。
class Prefetcher {
public:
    explicit Prefetcher(ImageCache& cache, int radius = 3);
    ~Prefetcher();

    Prefetcher(const Prefetcher&) = delete;
    Prefetcher& operator=(const Prefetcher&) = delete;

    void setSource(std::shared_ptr<ImageSource> source);
    void prefetchAround(int currentIndex);
    void cancelAll();

private:
    void submit(int index, const std::shared_ptr<ImageSource>& source, int epoch);

    ImageCache& m_cache;
    const int m_radius;
    QAtomicInt m_epoch{0};
    QMutex m_mutex;
    std::shared_ptr<ImageSource> m_source;
    QThreadPool m_pool;  // 声明在最后:析构最先,waitForDone 期间其余成员仍有效
};
