#pragma once

#include "source/ArchiveReader.h"
#include "source/ByteStore.h"
#include "source/ImageSource.h"

#include <QList>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <functional>
#include <memory>

// 7z / rar / 压缩 tar 等只能顺序解压的格式(DESIGN §4.5):后台顺序抽取一遍。
//
// 构造时打开文件、启动抽取线程即返回。线程按包内顺序走一遍,图片条目的编码字节存入
// ByteStore——全程不重开、每个条目只解压一次。这些格式拿到列表本就要解压到底,所以列表
// (名字 + 序号)与抽取是同一遍,走完才排序、才算完成:waitForListing() 等它完成并报进度;
// readEntry() 等列表完成后从 ByteStore 取。析构取消抽取并等线程退出。
// 由 openArchive() 按 libarchive 探测到的实际格式选用,不看后缀。
class StreamArchiveSource : public ImageSource {
public:
    explicit StreamArchiveSource(QString archivePath,
                                 qint64 memoryBytes = ByteStore::kDefaultMemoryBytes);
    ~StreamArchiveSource() override;

    int count() const override;
    QString entryName(int index) const override;
    QByteArray readEntry(int index) const override;
    bool waitForListing(const std::function<bool(int)>& progress) override;

    // 观测(测试用):抽取是否已结束、落盘条目数、落盘目录。
    bool isListed() const;
    int spilledCount() const { return m_store.spilledCount(); }
    QString spillDir() const { return m_store.spillDir(); }

private:
    void extract();  // 抽取线程

    QString m_path;
    ByteStore m_store;
    std::unique_ptr<ArchiveReader> m_reader;  // 构造时打开,之后只由抽取线程使用

    mutable QMutex m_mutex;
    mutable QWaitCondition m_changed;  // 每扫过一个条目、列表完成、取消时唤醒
    QList<ArchiveEntryRef> m_entries;  // 抽取中按包内顺序追加,完成后按名字自然排序
    int m_scanned = 0;                 // 已扫过的条目数(含非图片)
    bool m_listed = false;
    std::atomic<bool> m_cancel{false};

    std::unique_ptr<QThread> m_worker;
};
