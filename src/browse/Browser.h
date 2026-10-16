#pragma once

#include <QImage>
#include <QString>

#include <memory>

#include "cache/ImageCache.h"
#include "schedule/DecodeScheduler.h"

class ImageSource;

// 浏览会话:来源 + 当前索引(DESIGN 4.6 的 playlist 模型)。
// 把「查缓存 → 解码 → 触发预读」的编排收进 viewer-core,GUI 只做展示;
// 预读与换源经 DecodeScheduler 统一调度。
// 注:M2.5 过渡态——当前页未命中仍在调用线程同步解码,下一步改为异步(DESIGN §4.6)。
class Browser {
public:
    static constexpr qint64 kDefaultCacheBytes = ImageCache::kDefaultFitBytes;

    explicit Browser(qint64 cacheBytes = kDefaultCacheBytes);

    // 打开图片文件 / 文件夹 / 压缩包;单个图片文件定位到它在所在文件夹
    // 中的索引。失败返回 false 并把原因写入 *error(可传 nullptr),
    // 此时保持原有来源不变。
    bool open(const QString& path, QString* error = nullptr);

    bool hasSource() const { return m_source != nullptr; }
    int count() const;
    int currentIndex() const { return m_index; }
    QString currentName() const;

    // 当前图片(Fit 档):缓存命中直接返回,否则同步解码并入缓存;随后按最近一次
    // 翻页方向触发邻页预读。
    // 解码失败返回空 QImage。动画条目返回的是首帧。
    QImage currentImage();

    // 当前条目为多帧动画时返回其原始字节(供视图层驱动 QMovie 播放),
    // 否则返回空。先按后缀预筛再重读字节,静态大图不产生额外 I/O。
    QByteArray currentAnimation();

    // 导航:接受任意整数索引,环绕规整到 [0, count)。goTo 不改变翻页方向。
    void goTo(int index);
    void next();
    void prev();

private:
    std::shared_ptr<ImageSource> m_source;
    ImageCache m_cache;
    DecodeScheduler m_scheduler;  // 引用 m_cache:须声明在其后(析构先于 m_cache)
    int m_index = 0;
    Direction m_direction = Direction::Forward;
};
