#include "source/ArchiveSource.h"

#include "source/ArchiveReader.h"
#include "source/StreamArchiveSource.h"
#include "source/ZipArchiveSource.h"

std::unique_ptr<ImageSource> openArchive(const QString& path) {
    if (probeArchiveAccess(path) == ArchiveAccess::Random)
        return std::make_unique<ZipArchiveSource>(path);
    // 流式格式;打不开的文件也走这里,由它报 openError(列表即刻完成)。
    return std::make_unique<StreamArchiveSource>(path);
}
