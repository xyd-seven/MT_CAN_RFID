#ifndef QINGJUCANID_H
#define QINGJUCANID_H

#include <QtGlobal>

struct QingjuCanId
{
    quint8 priority = 5;      // 3 bits (bits 26~28)
    quint8 reserved1 = 0;     // 2 bits (bits 24~25)
    quint8 srcAddr = 0;       // 6 bits (bits 18~23)
    quint8 protoVer = 1;      // 2 bits (bits 16~17)
    quint8 reserved2 = 0;     // 2 bits (bits 14~15)
    quint8 destAddr = 0;      // 6 bits (bits 8~13)
    quint8 queue = 0;         // 3 bits (bits 5~7)
    quint8 index = 0;         // 5 bits (bits 0~4)

    static QingjuCanId parse(quint32 rawId)
    {
        QingjuCanId id;
        id.priority = (rawId >> 26) & 0x07;
        id.reserved1 = (rawId >> 24) & 0x03;
        id.srcAddr = (rawId >> 18) & 0x3F;
        id.protoVer = (rawId >> 16) & 0x03;
        id.reserved2 = (rawId >> 14) & 0x03;
        id.destAddr = (rawId >> 8) & 0x3F;
        id.queue = (rawId >> 5) & 0x07;
        id.index = rawId & 0x1F;
        return id;
    }

    quint32 toRawId() const
    {
        return (static_cast<quint32>(priority & 0x07) << 26) |
               (static_cast<quint32>(reserved1 & 0x03) << 24) |
               (static_cast<quint32>(srcAddr & 0x3F) << 18) |
               (static_cast<quint32>(protoVer & 0x03) << 16) |
               (static_cast<quint32>(reserved2 & 0x03) << 14) |
               (static_cast<quint32>(destAddr & 0x3F) << 8) |
               (static_cast<quint32>(queue & 0x07) << 5) |
               (index & 0x1F);
    }
};

#endif // QINGJUCANID_H
