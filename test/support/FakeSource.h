#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QSize>
#include <QWaitCondition>

#include "source/ImageSource.h"

// 可控的假来源(DESIGN §7),供调度器 / Browser 测试:
// - 条目名为 "<下标>.png",内容是现生成的 PNG(尺寸可配);setCorrupt() 的条目返回坏字节;
// - 记录每个条目的读取次数与读取先后;
// - block() 之后 readEntry 先记账再阻塞,直到 release(),用于把任务卡在「执行中」。
class FakeSource : public ImageSource {
public:
    explicit FakeSource(int count, QSize imageSize = QSize(8, 8));

    int count() const override { return m_count; }
    QString entryName(int index) const override;
    QByteArray readEntry(int index) const override;

    void setCorrupt(int index);
    void block();
    void release();

    // 等到累计已开始的读取数 ≥ n;超时返回 false。
    bool waitForReads(int n, int timeoutMs = 5000) const;
    int readCount(int index) const;
    QList<int> readOrder() const;

private:
    const int m_count;
    const QByteArray m_png;

    mutable QMutex m_mutex;
    mutable QWaitCondition m_changed;
    bool m_blocked = false;
    QSet<int> m_corrupt;
    mutable QList<int> m_order;
};
