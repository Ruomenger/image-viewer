#include "source/ByteStore.h"

#include <QDir>
#include <QFile>
#include <QMutexLocker>

ByteStore::ByteStore(qint64 memoryBytes) : m_budget(memoryBytes) {}

ByteStore::~ByteStore() = default;  // m_dir 随之删除临时目录

bool ByteStore::put(int key, const QByteArray& bytes) {
    const QMutexLocker locker(&m_mutex);
    if (m_memoryBytes + bytes.size() <= m_budget) {
        m_records.insert(key, {.bytes = bytes, .file = {}});
        m_memoryBytes += bytes.size();
        return true;
    }

    if (!m_dir) {
        m_dir = std::make_unique<QTemporaryDir>(QDir::tempPath() +
                                                QStringLiteral("/image-viewer-XXXXXX"));
        if (!m_dir->isValid()) {
            m_dir.reset();
            return false;
        }
    }
    const QString file = m_dir->filePath(QString::number(key));
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) {
        out.close();
        QFile::remove(file);
        return false;
    }
    m_records.insert(key, {.bytes = {}, .file = file});
    ++m_spilled;
    return true;
}

QByteArray ByteStore::get(int key) const {
    Record record;
    {
        const QMutexLocker locker(&m_mutex);
        const auto it = m_records.constFind(key);
        if (it == m_records.constEnd())
            return {};
        record = *it;
    }
    if (record.file.isEmpty())
        return record.bytes;
    QFile in(record.file);  // 文件读取放在锁外,不挡住其他读取
    if (!in.open(QIODevice::ReadOnly))
        return {};
    return in.readAll();
}

qint64 ByteStore::memoryBytes() const {
    const QMutexLocker locker(&m_mutex);
    return m_memoryBytes;
}

int ByteStore::spilledCount() const {
    const QMutexLocker locker(&m_mutex);
    return m_spilled;
}

QString ByteStore::spillDir() const {
    const QMutexLocker locker(&m_mutex);
    return m_dir ? m_dir->path() : QString();
}
