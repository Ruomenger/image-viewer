#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>

// 按字节预算的 LRU 解码图片缓存。
//
// 键为「当前来源内的索引」(int);切换来源时上层调用 clear()。
// clear() 同时递增代次(generation):后台任务入队时记下代次,用
// insertIfGeneration 写入,代次不匹配则丢弃——杜绝「旧来源的图在
// clear() 之后才落入新缓存」的竞态(检查与写入在同一把锁内)。
// 线程安全:所有操作内部加锁,可供 UI 线程与预读线程共享。
class ImageCache {
public:
    explicit ImageCache(qint64 maxBytes = 256LL * 1024 * 1024);

    // 命中返回该图片并刷新其 LRU 位置;未命中返回空 QImage。
    QImage get(int key);

    // 插入或更新。单张超过总预算时不缓存(避免清空整个缓存);空图忽略。
    // 插入后若超预算,按最久未用淘汰。
    void insert(int key, const QImage& image);

    // 仅当代次仍等于 generation 时插入(锁内校验),返回是否写入。
    bool insertIfGeneration(quint64 generation, int key, const QImage& image);

    // 当前代次;每次 clear() 递增。
    quint64 generation() const;

    bool contains(int key) const;
    void clear();

    qint64 maxBytes() const { return m_maxBytes; }
    qint64 totalBytes() const;
    int count() const;

private:
    static qint64 imageBytes(const QImage& image);
    bool insertLocked(int key, const QImage& image, qint64 bytes);  // 调用方须持锁
    void touch(int key);                                            // 移到最近使用端;调用方须持锁
    void evict();  // 超预算时淘汰最久未用;调用方须持锁

    const qint64 m_maxBytes;
    qint64 m_totalBytes = 0;
    quint64 m_generation = 0;
    mutable QMutex m_mutex;
    QHash<int, QImage> m_images;
    QList<int> m_lru;  // 队首=最久未用,队尾=最近使用
};
