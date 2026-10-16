#include "support/FakeSource.h"

#include <QDeadlineTimer>
#include <QMutexLocker>

#include "support/TestData.h"

FakeSource::FakeSource(int count, QSize imageSize)
    : m_count(count), m_png(testdata::makePng(imageSize.width(), imageSize.height())) {}

QString FakeSource::entryName(int index) const {
    return QStringLiteral("%1.png").arg(index);
}

QByteArray FakeSource::readEntry(int index) const {
    QMutexLocker locker(&m_mutex);
    if (m_forbidden != nullptr && QThread::currentThread() == m_forbidden) {
        ++m_forbiddenReads;
        qWarning("FakeSource: readEntry(%d) 在被禁止的线程上调用", index);
    }
    m_order.append(index);
    m_changed.wakeAll();
    while (m_blocked)
        m_changed.wait(&m_mutex);
    return m_corrupt.contains(index) ? QByteArrayLiteral("corrupt") : m_png;
}

void FakeSource::setCorrupt(int index) {
    const QMutexLocker locker(&m_mutex);
    m_corrupt.insert(index);
}

void FakeSource::block() {
    const QMutexLocker locker(&m_mutex);
    m_blocked = true;
}

void FakeSource::release() {
    const QMutexLocker locker(&m_mutex);
    m_blocked = false;
    m_changed.wakeAll();
}

void FakeSource::forbidReadsOn(QThread* thread) {
    const QMutexLocker locker(&m_mutex);
    m_forbidden = thread;
}

int FakeSource::forbiddenReads() const {
    const QMutexLocker locker(&m_mutex);
    return m_forbiddenReads;
}

bool FakeSource::waitForReads(int n, int timeoutMs) const {
    QMutexLocker locker(&m_mutex);
    const QDeadlineTimer deadline(timeoutMs);
    while (m_order.size() < n) {
        if (!m_changed.wait(&m_mutex, deadline))
            return m_order.size() >= n;
    }
    return true;
}

int FakeSource::readCount(int index) const {
    const QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_order.count(index));
}

QList<int> FakeSource::readOrder() const {
    const QMutexLocker locker(&m_mutex);
    return m_order;
}
