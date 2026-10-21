#pragma once

#include "source/ImageSource.h"

#include <QString>

#include <memory>

// 压缩包来源的入口:按 libarchive 探测到的实际格式选择读取策略,不信后缀(.cbr 里装 zip
// 很常见):
//  - zip / cbz / 未压缩 tar → ZipArchiveSource(无状态随机读,列表构造时建好);
//  - 7z / rar / 压缩 tar 等 → StreamArchiveSource(后台顺序抽取一遍,列表随抽取完成,
//    调用方用 waitForListing() 等它)。
// 打不开或不是压缩包时同样返回来源,原因见其 openError()。
std::unique_ptr<ImageSource> openArchive(const QString& path);
