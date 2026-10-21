#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

struct archive;
struct archive_entry;

// libarchive 读取句柄的 RAII 包装,各压缩包来源与格式探测共用。只封装本项目用到的几个
// 调用;一个句柄同一时刻只能由一个线程使用。
//
// 出错即作废:打开失败、读 header / 读数据出错都会释放句柄(isOpen() 变 false)并把
// libarchive 的报错留在 error() 里,调用方据此决定重开还是放弃。
class ArchiveReader {
public:
    explicit ArchiveReader(const QString& path);
    ~ArchiveReader();

    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;

    bool isOpen() const { return m_archive != nullptr; }
    QString error() const { return m_error; }  // 句柄作废的原因;正常时为空

    // 前进到下一个条目的 header(上一个条目未读完的数据由 libarchive 自动跳过——随机访问
    // 格式下只是 seek,不解压)。到末尾或出错返回 nullptr,用 atEnd() 区分。返回的 entry
    // 由 libarchive 持有,下次调用前有效。可恢复的告警(如路径编码)仍算成功。
    struct archive_entry* nextEntry();
    bool atEnd() const { return m_atEnd; }

    // 读取当前条目的全部数据;实际数据比声明尺寸短时截断为已读部分。读数据出错返回空并
    // 作废句柄:流状态已不可靠。
    QByteArray readData(struct archive_entry* entry);

    // 读过第一个 header 之后才有意义:zip / 未压缩 tar 跳过条目只需 seek,可以无状态随机读;
    // 7z / rar / 压缩 tar 只能从头顺序解压。
    bool isRandomAccess() const;

private:
    void fail(const QString& fallback);  // 记下报错并释放句柄

    struct archive* m_archive = nullptr;
    QString m_error;
    bool m_atEnd = false;
};

// 条目的路径:libarchive 能给 UTF-8 就用 UTF-8,否则退回本地 8 位编码。
QString archiveEntryPath(struct archive_entry* entry);

// 压缩包里的一个图片条目:名字 + 包内扫描序号。定位按序号而非名字(名字可能重复)。
struct ArchiveEntryRef {
    QString name;
    int ordinal = 0;
};

// 用刚打开的 reader 从头扫到尾,收集可解码的图片条目并按名字自然排序。一个 entry 都没
// 读到就中断(如非压缩包)时把报错写入 *error;已读到部分条目的截断包保持可浏览。
QList<ArchiveEntryRef> listArchiveImages(ArchiveReader& reader, QString* error);

// 压缩包的读取策略,按 libarchive 探测到的实际格式判定,不信后缀(.cbr 里装 zip 很常见)。
enum class ArchiveAccess : quint8 {
    Unreadable,  // 打不开或不是压缩包
    Random,      // zip / 未压缩 tar:跳过条目只需 seek,可无状态随机读
    Streaming,   // 7z / rar / 压缩 tar:只能从头顺序解压
};
ArchiveAccess probeArchiveAccess(const QString& path);
