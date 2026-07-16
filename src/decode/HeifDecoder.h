#pragma once

#include <QByteArray>
#include <QImage>

// HEIC/HEIF 解码(libheif + libde265,仅解码不编码)。
// 供 ImageDecoder 注册表使用,不直接面向上层。

// 按 ISO-BMFF ftyp 主 brand 判断字节流是否像 HEIF 家族文件。
bool looksLikeHeif(const QByteArray& bytes);

// 解码 HEIF 主图为 RGBA QImage;失败返回空 QImage。
// libheif 默认应用 irot/imir 变换,方向无需额外处理。
QImage decodeHeif(const QByteArray& bytes);
