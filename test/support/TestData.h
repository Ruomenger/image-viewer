#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

// 测试数据辅助:生成真实可解码的图片与压缩包,避免手写字节带来的格式问题。
namespace testdata {

// 返回一张 width×height 的合法 PNG 的字节(用 QImage 编码)。
QByteArray makePng(int width, int height);

// 把一张 width×height 的 PNG 写入文件。
void writePng(const QString& path, int width, int height);

// 用 libarchive 现写的压缩包格式。Zip 默认 deflate;ZipStored 不压缩(漫画 cbz 常见);
// Tar 未压缩(可随机读);TarGz / SevenZip 需顺序解压(流式)。
enum class ArchiveFormat { Zip, ZipStored, Tar, TarGz, SevenZip };

// 用 libarchive 创建压缩包,entries 为 (entryName, bytes),按给定顺序写入。
void makeArchive(const QString& path, const QList<QPair<QString, QByteArray>>& entries,
                 ArchiveFormat format);

// 创建一个 zip(deflate)。
inline void makeZip(const QString& zipPath, const QList<QPair<QString, QByteArray>>& entries) {
    makeArchive(zipPath, entries, ArchiveFormat::Zip);
}

}  // namespace testdata
