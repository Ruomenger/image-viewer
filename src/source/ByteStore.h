#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QTemporaryDir>
#include <QtTypes>

#include <memory>

// 流式压缩包抽出的条目字节(DESIGN §4.5):内存预算内常驻,超出预算的条目写入会话临时目录,
// 本对象销毁即删。漫画图片本身已压缩,整本编码字节通常 100–300MB,默认 512MB 预算下全在内存。
// put 来自抽取线程、get 来自解码工作线程,内部加锁。
class ByteStore {
public:
    static constexpr qint64 kDefaultMemoryBytes = qint64{512} * 1024 * 1024;

    explicit ByteStore(qint64 memoryBytes = kDefaultMemoryBytes);
    ~ByteStore();

    ByteStore(const ByteStore&) = delete;
    ByteStore& operator=(const ByteStore&) = delete;

    // 存入 key 对应的字节:预算内留在内存,否则落盘。落盘失败(临时目录不可用、磁盘满)返回
    // false,该条目视为缺失。同一 key 只应存一次。
    bool put(int key, const QByteArray& bytes);
    // 取出;缺失返回空。
    QByteArray get(int key) const;

    // 观测(测试用)。
    qint64 memoryBytes() const;
    int spilledCount() const;
    QString spillDir() const;  // 没有落盘过则为空

private:
    struct Record {
        QByteArray bytes;  // 在内存
        QString file;      // 或已落盘(二选一)
    };

    const qint64 m_budget;
    mutable QMutex m_mutex;
    QHash<int, Record> m_records;
    qint64 m_memoryBytes = 0;
    int m_spilled = 0;
    std::unique_ptr<QTemporaryDir> m_dir;  // 第一次落盘时创建
};
