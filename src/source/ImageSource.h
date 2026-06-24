#pragma once

#include <QByteArray>
#include <QString>

// Abstract sequence of images coming from a folder or an archive.
// The viewer only ever needs: how many, what's each called, and the raw bytes.
class ImageSource {
public:
    virtual ~ImageSource() = default;

    virtual int count() const = 0;
    virtual QString entryName(int index) const = 0;
    virtual QByteArray readEntry(int index) const = 0;

    // Whether `name` (a file name or archive entry path) looks like an image
    // we can decode, based on the formats Qt currently has plugins for.
    static bool isSupportedImage(const QString& name);

    // Whether `path` is a container we know how to browse with libarchive.
    static bool isArchive(const QString& path);
};
