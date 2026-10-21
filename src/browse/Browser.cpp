#include "browse/Browser.h"

#include "source/ArchiveSource.h"
#include "source/FolderSource.h"
#include "source/ImageSource.h"

#include <QFileInfo>
#include <QMetaObject>

#include <algorithm>
#include <utility>

namespace {

qint64 pixelCount(const QImage& image) {
    return static_cast<qint64>(image.width()) * image.height();
}

// 已是原图(原图不大于 Fit 目标)或动画(QMovie 按原尺寸播)时不必再解 Full。
bool needsFull(const DecodedImage& image) {
    return image.animation.isEmpty() && image.image.size() != image.originalSize;
}

}  // namespace

Browser::Browser(qint64 cacheBytes, QObject* parent)
    : QObject(parent), m_cache(cacheBytes), m_scheduler(m_cache) {
    m_opener.setMaxThreadCount(1);
    connect(&m_scheduler, &DecodeScheduler::ready, this, &Browser::onReady);
    connect(&m_scheduler, &DecodeScheduler::failed, this, &Browser::onFailed);
}

Browser::~Browser() {
    ++m_openTicket;  // 让在途的 open 放弃等列表;m_opener 随后最先析构,等它退出
}

void Browser::open(const QString& path) {
    const quint64 ticket = ++m_openTicket;
    m_opener.clear();  // 尚未开始的旧 open 直接作废
    m_opener.start([this, path, ticket] {
        OpenResult result = buildSource(path);
        if (result.source && !awaitListing(*result.source, ticket))
            return;  // 已被之后的 open 取代:来源随 result 销毁,后台抽取一并取消
        if (result.source && result.error.isEmpty())
            result.error = validate(*result.source);
        QMetaObject::invokeMethod(
            this, [this, ticket, result] { finishOpen(ticket, result); }, Qt::QueuedConnection);
    });
}

bool Browser::awaitListing(ImageSource& source, quint64 ticket) {
    return source.waitForListing([this, ticket, last = -1](int scanned) mutable {
        if (ticket != m_openTicket.load())
            return false;
        if (scanned == last)
            return true;  // 定期唤醒但没有新进度
        last = scanned;
        QMetaObject::invokeMethod(
            this,
            [this, ticket, scanned] {
                if (ticket == m_openTicket.load())
                    emit listingProgress(scanned);
            },
            Qt::QueuedConnection);
        return true;
    });
}

void Browser::openSource(std::shared_ptr<ImageSource> source, int entry) {
    ++m_openTicket;  // 盖过在途的 open(path)
    const QString error = source ? validate(*source) : QStringLiteral("没有来源");
    if (!error.isEmpty()) {
        emit openFailed(error);
        return;
    }
    install(std::move(source), entry);
}

Browser::OpenResult Browser::buildSource(const QString& path) {
    const QFileInfo info(path);
    OpenResult result;
    if (info.isDir()) {
        result.source = std::make_shared<FolderSource>(path);
    } else if (ImageSource::isArchive(path)) {
        result.source = openArchive(path);  // 按实际格式选 Zip / 流式来源
    } else if (info.isFile()) {
        auto folder = std::make_shared<FolderSource>(info.absolutePath());
        result.entry = std::max(0, folder->indexOf(info.absoluteFilePath()));
        result.source = folder;
    } else {
        result.error = QStringLiteral("路径不存在");
    }
    return result;
}

QString Browser::validate(const ImageSource& source) {
    if (!source.openError().isEmpty())
        return source.openError();
    if (source.count() == 0)
        return QStringLiteral("没有可显示的图片");
    return {};
}

void Browser::finishOpen(quint64 ticket, const OpenResult& result) {
    if (ticket != m_openTicket.load())
        return;  // 已被之后的 open 取代
    if (!result.error.isEmpty()) {
        emit openFailed(result.error);
        return;
    }
    install(result.source, result.entry);
}

void Browser::install(std::shared_ptr<ImageSource> source, int entry) {
    m_source = std::move(source);
    m_scheduler.setSource(m_source);  // 丢弃旧来源的排队任务、清空缓存;迟到结果按来源 id 作废
    m_direction = Direction::Forward;
    m_page = {};
    emit opened(m_source->count());
    showPage(pageAt(std::clamp(entry, 0, m_source->count() - 1)));
}

int Browser::count() const {
    return m_source ? m_source->count() : 0;
}

QString Browser::entryName(int entry) const {
    return m_source ? m_source->entryName(entry) : QString();
}

void Browser::goTo(int entry) {
    if (!m_source)
        return;
    showPage(pageAt(std::clamp(entry, 0, m_source->count() - 1)));
}

void Browser::next() {
    if (!m_source)
        return;
    const int entry = m_page.first + m_page.count;
    if (entry >= m_source->count()) {
        emit reachedEnd(true);
        return;
    }
    m_direction = Direction::Forward;
    showPage(pageAt(entry));
}

void Browser::prev() {
    if (!m_source)
        return;
    if (m_page.first <= 0) {
        emit reachedEnd(false);
        return;
    }
    m_direction = Direction::Backward;
    showPage(pageAt(m_page.first - 1));
}

void Browser::setFitTarget(QSize physicalPixels) {
    if (physicalPixels == m_scheduler.target(Tier::Fit))
        return;
    m_scheduler.setFitTarget(physicalPixels);
    if (m_page.isValid())
        fetchPage();  // 按新尺寸重解;更清晰时再发一次 pageReady
}

void Browser::requestFull() {
    if (!m_page.isValid() || m_wantFull)
        return;
    m_wantFull = true;
    bool changed = false;
    for (int slot = 0; slot < m_page.count; ++slot)
        changed = fetchFull(slot) || changed;
    if (changed)
        emitIfComplete();
}

Page Browser::pageAt(int entry) {
    return {.first = entry, .count = 1};  // 单页布局;双页在 M3(PageLayout)
}

void Browser::showPage(Page page) {
    m_page = page;
    m_images = QList<std::optional<DecodedImage>>(page.count);
    m_wantFull = false;
    emit pageChanged(page);
    fetchPage();
}

void Browser::fetchPage() {
    bool changed = false;
    for (int slot = 0; slot < m_page.count; ++slot) {
        const int entry = m_page.first + slot;
        // 首个条目经 focus 带出预读窗口;双页的第二个条目同为 P0。
        const std::optional<DecodedImage> hit =
            slot == 0 ? m_scheduler.focus(entry, m_direction)
                      : m_scheduler.request(entry, Tier::Fit, Priority::Current);
        if (hit)
            changed = accept(slot, *hit) || changed;
        if (m_wantFull)
            changed = fetchFull(slot) || changed;
    }
    if (changed)
        emitIfComplete();
}

bool Browser::fetchFull(int slot) {
    const std::optional<DecodedImage>& current = m_images.at(slot);
    if (!current || !needsFull(*current))
        return false;  // Fit 未到时等它到了再判断
    const std::optional<DecodedImage> hit =
        m_scheduler.request(m_page.first + slot, Tier::Full, Priority::Current);
    return hit && accept(slot, *hit);
}

bool Browser::accept(int slot, const DecodedImage& image) {
    std::optional<DecodedImage>& current = m_images[slot];
    if (current && pixelCount(current->image) >= pixelCount(image.image))
        return false;  // 不比已有的清晰(如预读结果的重复送达)
    current = image;
    return true;
}

int Browser::slotOf(const ImageKey& key) const {
    if (!m_source || key.entry.source != m_source->id() || !m_page.isValid())
        return -1;
    const int slot = key.entry.entry - m_page.first;
    return slot >= 0 && slot < m_page.count ? slot : -1;
}

void Browser::onReady(const ImageKey& key, const DecodedImage& image) {
    const int slot = slotOf(key);
    if (slot < 0 || key.tier == Tier::Thumb)
        return;  // 已翻走的页、预读结果、缩略图
    bool changed = accept(slot, image);
    if (m_wantFull && key.tier == Tier::Fit)
        changed = fetchFull(slot) || changed;
    if (changed)
        emitIfComplete();
}

void Browser::onFailed(const ImageKey& key) {
    const int slot = slotOf(key);
    if (slot < 0 || key.tier != Tier::Fit || m_images.at(slot))
        return;
    emit pageFailed(m_page, QStringLiteral("无法解码"));
}

void Browser::emitIfComplete() {
    QList<DecodedImage> images;
    images.reserve(m_images.size());
    for (const std::optional<DecodedImage>& image : std::as_const(m_images)) {
        if (!image)
            return;
        images.append(*image);
    }
    emit pageReady(m_page, images);
}
