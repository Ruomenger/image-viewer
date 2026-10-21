#include "support/TestData.h"

#include <QBuffer>
#include <QImage>

#include <archive.h>
#include <archive_entry.h>

namespace testdata {

QByteArray makePng(int width, int height) {
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(Qt::blue);

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

void writePng(const QString& path, int width, int height) {
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    image.save(path, "PNG");
}

void makeArchive(const QString& path, const QList<QPair<QString, QByteArray>>& entries,
                 ArchiveFormat format) {
    struct archive* a = archive_write_new();
    switch (format) {
        case ArchiveFormat::Zip:
            archive_write_set_format_zip(a);
            break;
        case ArchiveFormat::ZipStored:
            archive_write_set_format_zip(a);
            archive_write_zip_set_compression_store(a);
            break;
        case ArchiveFormat::Tar:
            archive_write_set_format_pax_restricted(a);
            break;
        case ArchiveFormat::TarGz:
            archive_write_set_format_pax_restricted(a);
            archive_write_add_filter_gzip(a);
            break;
        case ArchiveFormat::SevenZip:
            archive_write_set_format_7zip(a);
            break;
    }
    archive_write_open_filename(a, path.toUtf8().constData());

    for (const auto& [name, bytes] : entries) {
        struct archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, name.toUtf8().constData());
        archive_entry_set_size(entry, bytes.size());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_write_header(a, entry);
        archive_write_data(a, bytes.constData(), static_cast<size_t>(bytes.size()));
        archive_entry_free(entry);
    }

    archive_write_close(a);
    archive_write_free(a);
}

}  // namespace testdata
