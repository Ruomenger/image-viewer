#pragma once

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSize>
#include <QThreadPool>

#include <memory>
#include <optional>

#include "cache/ImageKey.h"
#include "decode/ImageDecoder.h"

class ImageCache;
class ImageSource;

// 调度优先级(DESIGN §4.4),取值越小越先执行。
enum class Priority : quint8 {
    Current,    // P0 当前页
    Ahead,      // P1 翻页方向的邻页
    Behind,     // P2 反方向的邻页
    Thumbnail,  // P3 缩略图
};

// 最近一次翻页的方向,决定哪一侧算 P1。RTL 只反转键位,不改变方向语义。
enum class Direction : quint8 { Forward, Backward };

struct SchedulerOptions {
    int threads = 2;  // 工作线程数
    int ahead = 3;    // 翻页方向预读张数(P1)
    int behind = 1;   // 反方向预读张数(P2)
};

// 读取 + 解码的唯一入口(DESIGN §4.4):当前页、邻页预读、缩略图共用一条优先级队列。
//
// - 请求 = (条目下标, 档, 优先级),针对 setSource() 设定的当前来源。
// - 在途去重:同一 (EntryKey, 档) 已在排队则只提升优先级,已在执行则等它的结果,
//   绝不重复读 / 解。
// - 结果先写缓存,再排队回到本对象所在线程发 ready / failed;送达前在该线程上
//   再校验一次,换源后旧来源的结果、Fit 目标改变前开始的 Fit 结果都不会送达。
// - 取消:focus() 丢弃新窗口外的 P0–P2 排队任务,setSource() 丢弃全部排队任务;
//   正在执行的跑完照常入缓存(换源或 Fit 目标已变的除外)。
// - 公有方法只在本对象所在线程(UI)调用。工作线程栈 8MB:LibRaw / dav1d 栈深
//   远超 macOS 次线程默认的 512KB。析构时丢弃排队任务,等待执行中的任务结束。
class DecodeScheduler : public QObject {
    Q_OBJECT
public:
    // 默认参数不写成 {}:LLVM 23 的 readability-trailing-comma 会把「= {},」误报成尾逗号。
    explicit DecodeScheduler(ImageCache& cache, SchedulerOptions options = SchedulerOptions(),
                             QObject* parent = nullptr);
    ~DecodeScheduler() override;

    DecodeScheduler(const DecodeScheduler&) = delete;
    DecodeScheduler& operator=(const DecodeScheduler&) = delete;

    // 切换来源:丢弃全部排队任务并清空缓存;旧来源在途任务的结果不入缓存、不送达。
    void setSource(std::shared_ptr<ImageSource> source);

    // Fit 档目标尺寸(屏幕物理像素;默认为空 = 解原图)。改变时清掉缓存里的 Fit 档,
    // 并把 Fit 预算抬到至少容纳整个预读窗口(窗口张数 × 目标像素 × 4B)。
    void setFitTarget(QSize target);
    QSize target(Tier tier) const;  // 各档传给 decodeImage 的目标尺寸

    // 导航到 index:P0 当前页、P1 沿 direction 的 ahead 张、P2 反方向的 behind 张,
    // 均为 Fit 档、不环绕;丢弃不在新窗口内的 P0–P2 排队任务(当前页以外的 Full
    // 一并丢弃,P3 不受影响)。当前页已在缓存时直接返回它。
    std::optional<DecodedImage> focus(int index, Direction direction);

    // 请求单个条目的某档:缓存命中直接返回;否则入队(已排队则只提升优先级)并
    // 返回 nullopt,结果稍后以 ready / failed 送达——除非期间被 focus / setSource 丢弃。
    std::optional<DecodedImage> request(int index, Tier tier, Priority priority);

signals:
    void ready(const ImageKey& key, const DecodedImage& image);
    void failed(const ImageKey& key);

private:
    struct Task {  // 排队中
        ImageKey key;
        Priority priority = Priority::Thumbnail;
        quint64 order = 0;  // 同优先级内按它先后执行
    };
    struct Job {  // 执行中
        ImageKey key;
        quint64 ticket = 0;  // 与 m_running 中的票号一致才算有效
        std::shared_ptr<ImageSource> source;
        QSize target;
        quint64 fitVersion = 0;
    };

    // 以下 *Locked 须持 m_mutex 调用。
    ImageKey keyLocked(int index, Tier tier) const;
    QSize targetLocked(Tier tier) const;
    void scheduleLocked(const ImageKey& key, Priority priority, bool reprioritize);
    void startWorkersLocked();
    Job takeJobLocked();

    void runWorker();  // 工作线程:循环取最高优先级任务执行,队列空了退出
    void finish(const Job& job, const DecodedImage& result);
    void deliver(const ImageKey& key, const DecodedImage& image, quint64 fitVersion);

    ImageCache& m_cache;
    const SchedulerOptions m_options;
    const qint64 m_fitFloorBytes;  // 构造时的 Fit 预算:按目标尺寸调整时不低于它

    mutable QMutex m_mutex;  // 保护以下全部状态
    std::shared_ptr<ImageSource> m_source;
    QList<Task> m_queue;                 // 任务很少(窗口 + 可见缩略图),线性扫描即可
    QHash<ImageKey, quint64> m_running;  // 执行中:键 → 票号;被作废的任务已移除
    QSize m_fitTarget;
    quint64 m_fitVersion = 0;
    quint64 m_nextOrder = 0;
    quint64 m_nextTicket = 0;
    int m_workers = 0;    // 存活的工作循环
    int m_executing = 0;  // 其中正在执行任务的
    bool m_stopping = false;

    QThreadPool m_pool;  // 声明在最后:析构最先,等待期间其余成员仍有效
};
