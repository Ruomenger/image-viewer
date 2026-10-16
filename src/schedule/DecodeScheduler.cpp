#include "schedule/DecodeScheduler.h"

#include "cache/ImageCache.h"
#include "source/ImageSource.h"

#include <QMetaObject>
#include <QMutexLocker>

#include <algorithm>
#include <utility>

namespace {

constexpr QSize kThumbTarget(256, 256);
constexpr qint64 kBytesPerPixel = 4;  // RGB32 / ARGB32_Premultiplied

SchedulerOptions sanitized(SchedulerOptions options) {
    options.threads = std::max(options.threads, 1);
    options.ahead = std::max(options.ahead, 0);
    options.behind = std::max(options.behind, 0);
    return options;
}

}  // namespace

DecodeScheduler::DecodeScheduler(ImageCache& cache, SchedulerOptions options, QObject* parent)
    : QObject(parent),
      m_cache(cache),
      m_options(sanitized(options)),
      m_fitFloorBytes(cache.maxBytes(Tier::Fit)) {
    m_pool.setMaxThreadCount(m_options.threads);
    m_pool.setStackSize(8 * 1024 * 1024);
}

DecodeScheduler::~DecodeScheduler() {
    {
        const QMutexLocker locker(&m_mutex);
        m_stopping = true;
        m_queue.clear();
    }
    m_pool.waitForDone();
}

void DecodeScheduler::setSource(std::shared_ptr<ImageSource> source) {
    const QMutexLocker locker(&m_mutex);
    m_source = std::move(source);
    m_queue.clear();
    m_running.clear();  // 在途任务作废:跑完后结果不入缓存、不送达
    m_cache.clear();    // 旧来源的条目不会再命中,释放内存
}

void DecodeScheduler::setFitTarget(QSize target) {
    const QMutexLocker locker(&m_mutex);
    if (target == m_fitTarget)
        return;
    m_fitTarget = target;
    ++m_fitVersion;
    // 按旧目标在解的 Fit 任务作废:同键的新请求另起任务,旧结果丢弃。
    m_running.removeIf(
        [](QHash<ImageKey, quint64>::iterator it) { return it.key().tier == Tier::Fit; });
    m_cache.clear(Tier::Fit);

    const qint64 window = 1 + m_options.ahead + m_options.behind;
    const qint64 pixels =
        target.isEmpty() ? 0 : static_cast<qint64>(target.width()) * target.height();
    m_cache.setMaxBytes(Tier::Fit, std::max(m_fitFloorBytes, window * pixels * kBytesPerPixel));
}

QSize DecodeScheduler::target(Tier tier) const {
    const QMutexLocker locker(&m_mutex);
    return targetLocked(tier);
}

std::optional<DecodedImage> DecodeScheduler::focus(int index, Direction direction) {
    const QMutexLocker locker(&m_mutex);
    if (!m_source || index < 0 || index >= m_source->count())
        return std::nullopt;

    // 新窗口,按执行先后:当前页 → 翻页方向 → 反方向;不环绕。
    const int count = m_source->count();
    const int step = direction == Direction::Forward ? 1 : -1;
    QList<std::pair<int, Priority>> window{{index, Priority::Current}};
    const auto add = [&window, count](int entry, Priority priority) {
        if (entry >= 0 && entry < count)
            window.append({entry, priority});
    };
    for (int d = 1; d <= m_options.ahead; ++d)
        add(index + (d * step), Priority::Ahead);
    for (int d = 1; d <= m_options.behind; ++d)
        add(index - (d * step), Priority::Behind);

    const auto inWindow = [&window](int entry) {
        return std::ranges::any_of(window, [entry](const auto& w) { return w.first == entry; });
    };
    m_queue.removeIf([&](const Task& task) {
        if (task.priority == Priority::Thumbnail)
            return false;
        if (task.key.tier == Tier::Full)  // 原图只为当前页解
            return task.key.entry.entry != index;
        return !inWindow(task.key.entry.entry);
    });

    std::optional<DecodedImage> current = m_cache.get(keyLocked(index, Tier::Fit));
    for (const auto& [entry, priority] : window)
        scheduleLocked(keyLocked(entry, Tier::Fit), priority, /*reprioritize=*/true);
    startWorkersLocked();
    return current;
}

std::optional<DecodedImage> DecodeScheduler::request(int index, Tier tier, Priority priority) {
    const QMutexLocker locker(&m_mutex);
    if (!m_source || index < 0 || index >= m_source->count())
        return std::nullopt;

    const ImageKey key = keyLocked(index, tier);
    if (std::optional<DecodedImage> hit = m_cache.get(key))
        return hit;
    scheduleLocked(key, priority, /*reprioritize=*/false);
    startWorkersLocked();
    return std::nullopt;
}

ImageKey DecodeScheduler::keyLocked(int index, Tier tier) const {
    return {.entry = {.source = m_source->id(), .entry = index}, .tier = tier};
}

QSize DecodeScheduler::targetLocked(Tier tier) const {
    switch (tier) {
        case Tier::Thumb:
            return kThumbTarget;
        case Tier::Fit:
            return m_fitTarget;
        case Tier::Full:
            return {};
    }
    return {};
}

// reprioritize:focus 按新窗口重排(可降级,并排到同优先级末尾);request 只提升。
void DecodeScheduler::scheduleLocked(const ImageKey& key, Priority priority, bool reprioritize) {
    const auto queued =
        std::ranges::find_if(m_queue, [&key](const Task& t) { return t.key == key; });
    if (m_cache.contains(key)) {  // 已缓存:不必再解,残留的排队任务一并撤掉
        if (queued != m_queue.end())
            m_queue.erase(queued);
        return;
    }
    if (m_running.contains(key))  // 执行中:等它的结果
        return;
    if (queued == m_queue.end()) {
        m_queue.append({.key = key, .priority = priority, .order = m_nextOrder++});
    } else if (reprioritize) {
        queued->priority = priority;
        queued->order = m_nextOrder++;
    } else if (priority < queued->priority) {
        queued->priority = priority;
    }
}

void DecodeScheduler::startWorkersLocked() {
    // 空闲(存活但未在执行)的工作循环不够分排队任务时补线程,总数不超过上限。
    while (m_workers < m_options.threads && m_workers - m_executing < m_queue.size()) {
        ++m_workers;
        m_pool.start([this] { runWorker(); });
    }
}

DecodeScheduler::Job DecodeScheduler::takeJobLocked() {
    const auto next = std::ranges::min_element(
        m_queue, {}, [](const Task& t) { return std::pair(t.priority, t.order); });
    Job job{
        .key = next->key,
        .ticket = m_nextTicket++,
        .source = m_source,  // 持有副本:换源后旧来源在读取 / 解码期间仍存活
        .target = targetLocked(next->key.tier),
        .fitVersion = m_fitVersion,
    };
    m_queue.erase(next);
    m_running.insert(job.key, job.ticket);
    ++m_executing;
    return job;
}

void DecodeScheduler::runWorker() {
    for (;;) {
        Job job;
        {
            const QMutexLocker locker(&m_mutex);
            if (m_stopping || m_queue.isEmpty()) {
                --m_workers;
                return;
            }
            job = takeJobLocked();
        }

        const int index = job.key.entry.entry;
        DecodedImage result =
            decodeImage(job.source->readEntry(index), job.source->entryName(index), job.target);
        if (job.key.tier == Tier::Thumb)
            result.animation.clear();  // 缩略图只显示静态首帧
        finish(job, result);
    }
}

void DecodeScheduler::finish(const Job& job, const DecodedImage& result) {
    const QMutexLocker locker(&m_mutex);
    --m_executing;
    const auto it = m_running.constFind(job.key);
    if (it == m_running.constEnd() || it.value() != job.ticket)
        return;  // 已被换源 / Fit 目标变化作废
    m_running.erase(it);
    m_cache.insert(job.key, result);  // 解码失败是空图,insert 内部忽略
    if (m_stopping)
        return;
    // 回到本对象所在线程再送达:在那里与 setSource / setFitTarget 串行,校验无竞态。
    QMetaObject::invokeMethod(
        this,
        [this, key = job.key, result, fitVersion = job.fitVersion] {
            deliver(key, result, fitVersion);
        },
        Qt::QueuedConnection);
}

void DecodeScheduler::deliver(const ImageKey& key, const DecodedImage& image, quint64 fitVersion) {
    {
        const QMutexLocker locker(&m_mutex);
        const bool stale = !m_source || m_source->id() != key.entry.source ||
                           (key.tier == Tier::Fit && fitVersion != m_fitVersion);
        if (stale)
            return;
    }
    if (image.image.isNull())
        emit failed(key);
    else
        emit ready(key, image);
}
