#pragma once

#include <QImage>
#include <QString>

#include <memory>

#include "cache/ImageCache.h"
#include "cache/Prefetcher.h"

class ImageSource;

// 浏览会话:来源 + 当前索引(DESIGN 4.6 的 playlist 模型)。
// 把「查缓存 → 解码 → 触发预读」的编排收进 viewer-core,GUI 只做展示;
// cache 与 prefetcher 的清空/换源协议也由这里统一维护,外部无需关心顺序。
class Browser {
public:
    static constexpr qint64 kDefaultCacheBytes = 256LL * 1024 * 1024;

    explicit Browser(qint64 cacheBytes = kDefaultCacheBytes);

    // 打开图片文件 / 文件夹 / 压缩包;单个图片文件定位到它在所在文件夹
    // 中的索引。失败返回 false 并把原因写入 *error(可传 nullptr),
    // 此时保持原有来源不变。
    bool open(const QString& path, QString* error = nullptr);

    bool hasSource() const { return m_source != nullptr; }
    int count() const;
    int currentIndex() const { return m_index; }
    QString currentName() const;

    // 当前图片:缓存命中直接返回,否则同步解码并入缓存;随后触发邻页预读。
    // 解码失败返回空 QImage。动画条目返回的是首帧(缓存亦只存首帧)。
    QImage currentImage();

    // 当前条目为多帧动画时返回其原始字节(供视图层驱动 QMovie 播放),
    // 否则返回空。先按后缀预筛再重读字节,静态大图不产生额外 I/O。
    QByteArray currentAnimation();

    // 导航:接受任意整数索引,环绕规整到 [0, count)。
    void goTo(int index);
    void next() { goTo(m_index + 1); }
    void prev() { goTo(m_index - 1); }

private:
    std::shared_ptr<ImageSource> m_source;
    ImageCache m_cache;
    Prefetcher m_prefetcher;  // 引用 m_cache:须声明在其后(析构先于 m_cache)
    int m_index = 0;
};
