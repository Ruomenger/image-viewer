#pragma once

#include <QHashFunctions>
#include <QtTypes>

// 解码档(DESIGN §4.2):同一条目按用途解到不同尺寸,缓存与调度都按档区分。
enum class Tier : quint8 {
    Thumb,  // 胶片条 / 缩略图:长边约 256px
    Fit,    // 适应窗口:屏幕物理像素(App 层传入)
    Full,   // 原图:放大超过 Fit 档分辨率时
};

// 条目的全局键 = 来源 id(ImageSource::id(),每个来源实例唯一)+ 条目在该来源
// 列表中的下标。来源列表建成后不再变化,下标在来源实例内就是稳定 id;来源一变
// id 就变,旧来源迟到的结果不会串到新来源。
struct EntryKey {
    quint64 source = 0;
    int entry = -1;

    friend bool operator==(const EntryKey&, const EntryKey&) = default;
};

// 缓存与在途去重的键:(EntryKey, 档)。
struct ImageKey {
    EntryKey entry;
    Tier tier = Tier::Fit;

    friend bool operator==(const ImageKey&, const ImageKey&) = default;
};

inline size_t qHash(const EntryKey& key, size_t seed = 0) noexcept {
    return qHashMulti(seed, key.source, key.entry);
}

inline size_t qHash(const ImageKey& key, size_t seed = 0) noexcept {
    return qHashMulti(seed, key.entry, static_cast<int>(key.tier));
}
