#include "decode/FtypBox.h"

#include <algorithm>
#include <cstring>

bool ftypContainsBrand(const QByteArray& bytes, const char* brand) {
    // 布局:[4 字节大端 box 长度]"ftyp"[主 brand][4 字节 minor version][兼容 brand…]
    if (bytes.size() < 12 || std::memcmp(bytes.constData() + 4, "ftyp", 4) != 0)
        return false;

    const auto boxSize =
        static_cast<qsizetype>((static_cast<quint32>(static_cast<uchar>(bytes[0])) << 24) |
                               (static_cast<quint32>(static_cast<uchar>(bytes[1])) << 16) |
                               (static_cast<quint32>(static_cast<uchar>(bytes[2])) << 8) |
                               static_cast<quint32>(static_cast<uchar>(bytes[3])));
    // 防御异常 box 长度:最多扫 256 字节,足够覆盖正常 brand 列表。
    const qsizetype end = std::min({bytes.size(), boxSize, qsizetype{256}});

    if (std::memcmp(bytes.constData() + 8, brand, 4) == 0)
        return true;
    for (qsizetype offset = 16; offset + 4 <= end; offset += 4) {
        if (std::memcmp(bytes.constData() + offset, brand, 4) == 0)
            return true;
    }
    return false;
}
