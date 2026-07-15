#include "source/FolderSource.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

FolderSource::FolderSource(const QString& dirPath) {
    const QDir dir(dirPath);
    if (!dir.exists()) {
        m_openError = QStringLiteral("文件夹不存在");
        return;
    }
    if (!dir.isReadable()) {
        m_openError = QStringLiteral("没有读取文件夹的权限");
        return;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::NoSort);
    for (const QFileInfo& info : entries) {
        if (ImageSource::isSupportedImage(info.fileName()))
            m_files.push_back(info.absoluteFilePath());
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(m_files, [&collator](const QString& a, const QString& b) {
        return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
    });
}

QString FolderSource::entryName(int index) const {
    if (index < 0 || index >= m_files.size())
        return {};
    return QFileInfo(m_files.at(index)).fileName();
}

QByteArray FolderSource::readEntry(int index) const {
    if (index < 0 || index >= m_files.size())
        return {};

    QFile file(m_files.at(index));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

int FolderSource::indexOf(const QString& filePath) const {
    const QString target = QFileInfo(filePath).absoluteFilePath();
    for (int i = 0; i < m_files.size(); ++i) {
        if (m_files.at(i) == target)
            return i;
    }
    return -1;
}
