#ifndef CANFRAME_MODEL_H
#define CANFRAME_MODEL_H

#include <QByteArray>
#include <QString>
#include <QDateTime>
#include <QtGlobal>

enum class CanFrameDirection
{
    Rx,
    Tx
};

enum class CanFrameProtocol
{
    ClassicCan,
    CanFd
};

struct CanFrame
{
    quint32 id = 0;
    quint32 channel = 0;
    QByteArray data;
    CanFrameDirection direction = CanFrameDirection::Rx;
    CanFrameProtocol protocol = CanFrameProtocol::ClassicCan;
    bool extendedFrame = false;
    bool remoteFrame = false;
    // 统一 CAN 时钟的相对毫秒，用于时延和周期判断；-1 表示尚未盖章。
    qint64 monotonicElapsedMs = -1;
    QDateTime hostDateTime;
    quint64 zlgTimestampRaw = 0;
    bool hasZlgTimestamp = false;

    int dlc() const;
    QString idText() const;
    QString dataText() const;
    QString directionText() const;
    QString frameTypeText() const;
    QString payloadTypeText() const;
    QString protocolText() const;
};

#endif // CANFRAME_MODEL_H
