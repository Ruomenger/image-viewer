#pragma once

#include <QByteArray>
#include <QImage>

// 相机 RAW 解码(LibRaw,线程安全变体 raw_r)。供 ImageDecoder 注册表使用。

// RAW 无统一 magic bytes(多为 TIFF 变体,与普通 TIFF 无法可靠区分),
// 按文件后缀判定;误标后缀的文件 LibRaw 会拒绝,注册表兜底接手。
bool looksLikeRaw(const QByteArray& bytes, const QString& suffix);

// LibRaw 全流程(unpack → dcraw_process,含相机方向翻转)解出 RGB QImage;
// 失败返回空 QImage。RAW 解码耗时以百毫秒计,依赖预读掩盖延迟。
QImage decodeRaw(const QByteArray& bytes);
