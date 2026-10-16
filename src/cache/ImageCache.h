#pragma once

#include <QHash>
#include <QList>
#include <QMutex>

#include <array>
#include <optional>

#include "cache/ImageKey.h"
#include "decode/ImageDecoder.h"

// 解码结果缓存,按档分道(DESIGN §4.4):每档一条独立的 LRU、独立的上限,
// 放大看原图、刷胶片条都挤不掉预读窗口里的 Fit 档。
// - Fit:按字节预算,默认 256MB;调度器按 Fit 目标尺寸把它抬到至少容纳整个预读窗口。
// - Full:只留一张(当前页),不设字节上限——原图再大也要能放大看。
// - Thumb:独立的小预算。
// 换源时 clear() 释放内存;正确性靠键里的来源 id,不靠清空的时序。
// 线程安全:所有操作内部加锁,可供 UI 线程与调度器工作线程共享。
class ImageCache {
public:
    static constexpr qint64 kDefaultFitBytes = 256LL * 1024 * 1024;
    static constexpr qint64 kDefaultThumbBytes = 32LL * 1024 * 1024;

    explicit ImageCache(qint64 fitBytes = kDefaultFitBytes, qint64 thumbBytes = kDefaultThumbBytes);

    // 命中返回结果并刷新其 LRU 位置;未命中返回 nullopt。
    std::optional<DecodedImage> get(const ImageKey& key);

    // 插入或更新,随后按该档上限淘汰最久未用。空图忽略;单张超过该档字节
    // 预算时不缓存(否则会把该档其余全部挤掉)。图与动画字节都计入占用。
    void insert(const ImageKey& key, const DecodedImage& image);

    bool contains(const ImageKey& key) const;
    void clear();           // 清空全部档
    void clear(Tier tier);  // 只清某档(如 Fit 目标尺寸变化)

    // 调整某档字节预算,调小时立即淘汰。
    void setMaxBytes(Tier tier, qint64 maxBytes);
    qint64 maxBytes(Tier tier) const;
    qint64 totalBytes(Tier tier) const;
    int count(Tier tier) const;

private:
    struct Lane {
        qint64 maxBytes = 0;
        int maxCount = 0;  // 0 表示不限张数
        qint64 totalBytes = 0;
        QHash<EntryKey, DecodedImage> images;
        QList<EntryKey> lru;  // 队首 = 最久未用,队尾 = 最近使用
    };

    static qint64 imageBytes(const DecodedImage& image);
    static void evict(Lane& lane);  // 超上限时淘汰最久未用;调用方须持锁
    Lane& lane(Tier tier) { return m_lanes.at(static_cast<size_t>(tier)); }
    const Lane& lane(Tier tier) const { return m_lanes.at(static_cast<size_t>(tier)); }

    mutable QMutex m_mutex;
    std::array<Lane, 3> m_lanes;  // 按 Tier 取值下标
};
