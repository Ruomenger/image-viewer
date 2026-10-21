#pragma once

#include "source/ArchiveReader.h"
#include "source/ImageSource.h"

#include <QList>
#include <QString>

// zip / cbz / 未压缩 tar 的来源(DESIGN §4.5):无状态随机读。
//
// 列表从中央目录 / 条目头一次扫出。每次 readEntry 独立打开文件,按包内序号前进到目标
// 条目再读数据——这类格式下前进只是 seek、不解压,所以没有共享游标、没有锁,任意线程
// 并发调用互不影响。由 openArchive() 按 libarchive 探测到的实际格式选用,不看后缀。
class ZipArchiveSource : public ImageSource {
public:
    explicit ZipArchiveSource(QString archivePath);

    int count() const override { return static_cast<int>(m_entries.size()); }
    QString entryName(int index) const override;
    QByteArray readEntry(int index) const override;

private:
    QString m_path;
    QList<ArchiveEntryRef> m_entries;  // 按名字自然排序;构造后不再改变
};
