#pragma once

#include "source/ImageSource.h"

#include <QList>
#include <QMutex>
#include <QString>

struct archive;

// Browses image entries inside an archive (zip / cbz / 7z / rar / tar ...)
// via libarchive. Entry order is natural-sorted by path.
//
// libarchive 是流式读取,随机定位只能从头扫。顺序读快路径:保持一个
// 打开的 reader 及其扫描位置,目标 entry 在当前位置之后就接着向前扫,
// 只有回退时才重开——顺序翻页由每次 O(n) 降为 O(1) 摊还。
// readEntry 可能被 UI 线程与预读线程并发调用,由 m_readMutex 串行化。
class ArchiveSource : public ImageSource {
public:
    explicit ArchiveSource(QString archivePath);
    ~ArchiveSource() override;

    ArchiveSource(const ArchiveSource&) = delete;
    ArchiveSource& operator=(const ArchiveSource&) = delete;

    int count() const override { return static_cast<int>(m_entries.size()); }
    QString entryName(int index) const override;
    QByteArray readEntry(int index) const override;

private:
    struct Entry {
        QString name;
        int ordinal;  // 包内扫描顺序位置;按它定位而非名字(名字可能重复)
    };

    // 以下两个仅在持有 m_readMutex 时调用。
    bool reopenReaderLocked() const;
    void closeReaderLocked() const;

    QString m_path;
    QList<Entry> m_entries;  // 按名字自然排序

    mutable QMutex m_readMutex;
    mutable struct archive* m_reader = nullptr;  // 顺序读快路径的持久 reader
    mutable int m_nextOrdinal = 0;               // 下一次 next_header 将返回的 entry 序号
};
