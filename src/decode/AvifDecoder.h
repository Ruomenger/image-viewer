#pragma once

#include <QByteArray>
#include <QImage>

// AVIF 解码(libavif + dav1d,仅解码不编码)。
// 供 ImageDecoder 注册表使用,不直接面向上层。

// ftyp box 的 brand 列表(主 + 兼容)含 avif/avis 即认为是 AVIF。
bool looksLikeAvif(const QByteArray& bytes);

// 解码 AVIF(动图序列取首帧)为 RGBA QImage;失败返回空 QImage。
QImage decodeAvif(const QByteArray& bytes);
