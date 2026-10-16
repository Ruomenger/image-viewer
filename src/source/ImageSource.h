#pragma once

#include <QByteArray>
#include <QString>
#include <QtTypes>

// Abstract sequence of images coming from a folder or an archive.
// The viewer only ever needs: how many, what's each called, and the raw bytes.
class ImageSource {
public:
    ImageSource();
    virtual ~ImageSource() = default;

    ImageSource(const ImageSource&) = delete;
    ImageSource& operator=(const ImageSource&) = delete;

    // 进程内唯一的来源 id:每个实例一个,同一路径重新打开也是新 id。缓存与调度的
    // 键(EntryKey)带上它,旧来源迟到的结果不会串到新来源。
    quint64 id() const { return m_id; }

    virtual int count() const = 0;
    virtual QString entryName(int index) const = 0;
    virtual QByteArray readEntry(int index) const = 0;

    // 打开阶段的失败原因;为空表示打开成功。借此区分「打开失败」
    // (openError 非空)与「打开成功但没有图片」(openError 为空且
    // count() == 0),供上层给出不同提示。
    QString openError() const { return m_openError; }

    // Whether `name` (a file name or archive entry path) looks like an image
    // we can decode, based on the formats Qt currently has plugins for.
    static bool isSupportedImage(const QString& name);

    // Whether `path` is a container we know how to browse with libarchive.
    static bool isArchive(const QString& path);

protected:
    QString m_openError;  // 由子类构造函数填写

private:
    const quint64 m_id;
};
