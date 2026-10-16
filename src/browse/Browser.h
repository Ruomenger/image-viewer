#pragma once

#include <QList>
#include <QObject>
#include <QSize>
#include <QString>
#include <QThreadPool>

#include <memory>
#include <optional>

#include "cache/ImageCache.h"
#include "schedule/DecodeScheduler.h"

class ImageSource;

// 一页 = 从第 first 个条目起的 count 个条目(1 或 2,DESIGN §4.6)。M2.5 只有单页布局。
struct Page {
    int first = -1;
    int count = 0;

    bool isValid() const { return first >= 0 && count > 0; }
    friend bool operator==(const Page&, const Page&) = default;
};

// 浏览会话(DESIGN §4.6):来源 + 当前页。全部异步——调用线程上不做任何读取、列表与解码。
//
// - open():在后台线程建列表;成功才替换来源(发 opened,紧接着 pageChanged),失败发
//   openFailed 且原来源不变。连续 open 时只有最后一次生效。
// - 导航:pageChanged 立即发出(序号 / 文件名先更新);页内条目全部就绪时发 pageReady——
//   缓存命中时在 goTo / next / prev 返回前同步发出,否则由调度器解完后排队发回;
//   已翻走的页的结果不再发出。解码失败发 pageFailed。
// - 同一页的 pageReady 可能再次发出,每次分辨率更高:requestFull() 换成原图,或
//   setFitTarget() 调大后按新尺寸重解。
// - 末页向后 / 首页向前继续翻:停住并发 reachedEnd,不环绕。
class Browser : public QObject {
    Q_OBJECT
public:
    explicit Browser(qint64 cacheBytes = ImageCache::kDefaultFitBytes, QObject* parent = nullptr);
    ~Browser() override;

    Browser(const Browser&) = delete;
    Browser& operator=(const Browser&) = delete;

    // 打开图片文件 / 文件夹 / 压缩包;单个图片文件定位到它在所在文件夹中的位置。
    void open(const QString& path);
    // 打开已建好的来源并定位到 entry(测试注入假来源;M3 的相邻来源导航也走这里)。
    // 结果信号在调用返回前同步发出。
    void openSource(std::shared_ptr<ImageSource> source, int entry = 0);

    bool hasSource() const { return m_source != nullptr; }
    int count() const;
    QString entryName(int entry) const;
    Page currentPage() const { return m_page; }

    // 跳到包含该条目的页,越界时贴到首 / 末页;不改变翻页方向。
    void goTo(int entry);
    void next();
    void prev();

    // Fit 档目标尺寸:屏幕物理像素(App 层传入,换屏时更新;与窗口大小无关)。
    void setFitTarget(QSize physicalPixels);
    // 视图放大超过 Fit 档分辨率时调用:当前页换成原图后再发一次 pageReady。
    // 已是原图(原图不大于 Fit 目标)或动画条目时不做任何事。
    void requestFull();

signals:
    void opened(int entryCount);
    void openFailed(const QString& reason);
    void listingProgress(int scanned);  // 流式压缩包的后台列表进度(StreamArchiveSource 落地后发出)
    void pageChanged(Page page);
    void pageReady(Page page, const QList<DecodedImage>& images);
    void pageFailed(Page page, const QString& reason);
    void reachedEnd(bool forward);

private:
    struct OpenResult {
        std::shared_ptr<ImageSource> source;
        int entry = 0;
        QString error;
    };
    static OpenResult buildSource(const QString& path);  // 任意线程:建来源并校验
    static QString validate(const ImageSource& source);  // 打开失败 / 没图的原因,成功为空

    void finishOpen(quint64 ticket, const OpenResult& result);
    void install(std::shared_ptr<ImageSource> source, int entry);
    static Page pageAt(int entry);  // 单页布局;M3 的 PageLayout 接管后改回成员
    void showPage(Page page);       // 换页:清空当前页结果,发 pageChanged,再取图
    void fetchPage();               // 请求当前页各条目(Fit 档,需要时 Full);命中的立即收下
    bool fetchFull(int slot);       // 该条目需要且尚无原图时请求 Full;命中返回 true
    bool accept(int slot, const DecodedImage& image);  // 收下更清晰的结果,返回是否有变化
    void onReady(const ImageKey& key, const DecodedImage& image);
    void onFailed(const ImageKey& key);
    int slotOf(const ImageKey& key) const;  // 属于当前页的第几个条目,不属于返回 -1
    void emitIfComplete();

    std::shared_ptr<ImageSource> m_source;
    ImageCache m_cache;
    DecodeScheduler m_scheduler;  // 引用 m_cache:须声明在其后(析构先于 m_cache)

    Page m_page;
    Direction m_direction = Direction::Forward;
    QList<std::optional<DecodedImage>> m_images;  // 当前页各条目已收到的结果
    bool m_wantFull = false;                      // 当前页已请求原图
    quint64 m_openTicket = 0;                     // 每次 open 递增;迟到的旧结果丢弃

    QThreadPool m_opener;  // 后台建列表(单线程);声明在最后,析构最先
};
