#include "canframe.h"

int CanFrame::dlc() const
{
    return data.size();
}

QString CanFrame::idText() const
{
    return QString("0x%1").arg(id, 0, 16);
}

QString CanFrame::dataText() const
{
    QString text;
    for (int index = 0; index < data.size(); ++index) {
        text += QString("%1 ").arg(static_cast<int>(static_cast<quint8>(data[index])), 2, 16, QChar('0'));
    }
    return text;
}

QString CanFrame::directionText() const
{
    return direction == CanFrameDirection::Rx ? QStringLiteral("收") : QStringLiteral("发");
}

QString CanFrame::frameTypeText() const
{
    return extendedFrame ? QStringLiteral("扩展帧") : QStringLiteral("标准帧");
}

QString CanFrame::payloadTypeText() const
{
    return remoteFrame ? QStringLiteral("远程帧") : QStringLiteral("数据帧");
}

QString CanFrame::protocolText() const
{
    return protocol == CanFrameProtocol::ClassicCan ? QStringLiteral("CAN") : QStringLiteral("CANFD");
}
