#include "source/ZipArchiveSource.h"

#include <utility>

ZipArchiveSource::ZipArchiveSource(QString archivePath) : m_path(std::move(archivePath)) {
    ArchiveReader reader(m_path);
    if (!reader.isOpen()) {
        m_openError = reader.error();
        return;
    }
    m_entries = listArchiveImages(reader, &m_openError);
}

QString ZipArchiveSource::entryName(int index) const {
    if (index < 0 || index >= m_entries.size())
        return {};
    return m_entries.at(index).name;
}

QByteArray ZipArchiveSource::readEntry(int index) const {
    if (index < 0 || index >= m_entries.size())
        return {};
    const int target = m_entries.at(index).ordinal;

    ArchiveReader reader(m_path);  // 每次独立打开:不与其他读取共享任何状态
    for (int ordinal = 0; ordinal < target; ++ordinal) {
        if (reader.nextEntry() == nullptr)
            return {};
    }
    return reader.readData(reader.nextEntry());
}
