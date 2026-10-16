#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>

#include "decode/ImageDecoder.h"

// 相机 RAW 解码(LibRaw,线程安全变体 raw_r)。供 ImageDecoder 注册表使用。

// RAW 无统一 magic bytes(多为 TIFF 变体,与普通 TIFF 无法可靠区分),
// 按文件后缀判定;误标后缀的文件 LibRaw 会拒绝,注册表兜底接手。
bool looksLikeRaw(const QByteArray& bytes, const QString& suffix);

// 解出 RAW;originalSize 恒为全流程输出尺寸(已含相机方向)。
// target 为空:LibRaw 全流程(unpack → dcraw_process,含相机方向翻转)解原图。
// 否则按目标尺寸走捷径:内嵌 JPEG 预览长边够用就直接用它(已缩到目标);
// 不够才走全流程,半尺寸输出仍覆盖目标时开 half_size(跳过插值,快数倍)。
// 失败时 image 为空。全流程耗时以百毫秒计,依赖预读掩盖延迟。
DecodedImage decodeRaw(const QByteArray& bytes, QSize target = QSize());
