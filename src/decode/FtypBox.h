#pragma once

#include <QByteArray>

// ISO-BMFF ftyp box 工具:判断字节流开头的 ftyp box(主 brand + 兼容
// brand 列表)中是否含有给定 4 字符 brand。HEIF / AVIF 探测共用。
// 只看主 brand 不够:例如不少 AVIF 文件主 brand 是结构性的 mif1,
// "avif" 只出现在兼容 brand 列表里。
bool ftypContainsBrand(const QByteArray& bytes, const char* brand);
