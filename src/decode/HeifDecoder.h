#pragma once

#include <QByteArray>
#include <QSize>

#include "decode/ImageDecoder.h"

// HEIC/HEIF 解码(libheif + libde265,仅解码不编码)。
// 供 ImageDecoder 注册表使用,不直接面向上层。

// 按 ISO-BMFF ftyp 主 brand 判断字节流是否像 HEIF 家族文件。
bool looksLikeHeif(const QByteArray& bytes);

// 解码 HEIF 为 RGBA / RGBX QImage(按有无 alpha);失败时 image 为空。
// target 非空时,若文件内有长边覆盖目标的缩略图,解其中最小的一张代替主图
// (返回的图未缩放,可能大于 target);originalSize 恒为主图尺寸。
// libheif 默认应用 irot/imir 变换,方向无需额外处理。
DecodedImage decodeHeif(const QByteArray& bytes, QSize target = QSize());
