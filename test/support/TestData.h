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

// 用 libarchive 创建一个 zip,entries 为 (entryName, bytes)。
void makeZip(const QString& zipPath, const QList<QPair<QString, QByteArray>>& entries);

}  // namespace testdata
