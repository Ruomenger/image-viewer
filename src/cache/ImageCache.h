#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>

// 按字节预算的 LRU 解码图片缓存。
//
// 键为「当前来源内的索引」(int);切换来源时上层须调用 clear()。
// 线程安全:get/insert 等都在内部加锁,可供 UI 线程与预读线程共享。
class ImageCache {
public:
    explicit ImageCache(qint64 maxBytes = 256LL * 1024 * 1024);

    // 命中返回该图片并刷新其 LRU 位置;未命中返回空 QImage。
    QImage get(int key);

    // 插入或更新。单张超过总预算时不缓存(避免清空整个缓存);空图忽略。
    // 插入后若超预算,按最久未用淘汰。
    void insert(int key, const QImage& image);

    bool contains(int key) const;
    void clear();

    qint64 maxBytes() const { return m_maxBytes; }
    qint64 totalBytes() const;
    int count() const;

private:
    static qint64 imageBytes(const QImage& image);
    void touch(int key);  // 移到最近使用端;调用方须持锁
    void evict();         // 超预算时淘汰最久未用;调用方须持锁

    const qint64 m_maxBytes;
    qint64 m_totalBytes = 0;
    mutable QMutex m_mutex;
    QHash<int, QImage> m_images;
    QList<int> m_lru;  // 队首=最久未用,队尾=最近使用
};
