#pragma once

#include "source/ArchiveReader.h"
#include "source/ImageSource.h"

#include <QList>
#include <QMutex>
#include <QString>

#include <memory>

// 按 libarchive 探测到的实际格式选择压缩包的读取策略,不信后缀(.cbr 里装 zip 很常见):
//  - zip / cbz / 未压缩 tar → ZipArchiveSource(无状态随机读);
//  - 7z / rar / 压缩 tar 等 → ArchiveSource(单游标顺序读;M2.5 将由 StreamArchiveSource 取代)。
// 打不开或不是压缩包时同样返回来源,原因见其 openError()。
std::unique_ptr<ImageSource> openArchive(const QString& path);

// 单游标顺序读的压缩包来源(M1):保持一个打开的 reader 及其扫描位置,目标条目在当前位置
// 之后就接着向前扫,回退才重开;并发读取由 m_readMutex 串行化。
//
// 只剩顺序解压的格式(7z / rar / 压缩 tar)在用它——这些格式重开等于重新解压前面的全部
// 内容,回退代价很大;M2.5 的 StreamArchiveSource(后台抽取一遍 + ByteStore)落地后删除。
class ArchiveSource : public ImageSource {
public:
    explicit ArchiveSource(QString archivePath);
    ~ArchiveSource() override = default;

    int count() const override { return static_cast<int>(m_entries.size()); }
    QString entryName(int index) const override;
    QByteArray readEntry(int index) const override;

private:
    // 以下两个仅在持有 m_readMutex 时调用。
    bool reopenReaderLocked() const;
    void closeReaderLocked() const;

    QString m_path;
    QList<ArchiveEntryRef> m_entries;  // 按名字自然排序

    mutable QMutex m_readMutex;
    mutable std::unique_ptr<ArchiveReader> m_reader;  // 顺序读快路径的持久 reader
    mutable int m_nextOrdinal = 0;                    // 下一次 nextEntry 将返回的条目序号
};
