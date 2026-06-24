#pragma once

#include "source/ImageSource.h"

#include <QStringList>

// All decodable images in a single directory, sorted in natural order
// (so "img2.png" comes before "img10.png").
class FolderSource : public ImageSource {
public:
    explicit FolderSource(const QString& dirPath);

    int count() const override { return static_cast<int>(m_files.size()); }
    QString entryName(int index) const override;
    QByteArray readEntry(int index) const override;

    // Index of a specific file inside this folder, or -1 if absent.
    int indexOf(const QString& filePath) const;

private:
    QStringList m_files;  // absolute paths
};
