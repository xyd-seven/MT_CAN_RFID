#include "isotptransport.h"

#include "rfidprotocol.h"

IsoTpTransport::IsoTpTransport(const IsoTpConfig &config) :
    transportConfig(config)
{
}

void IsoTpTransport::setConfig(const IsoTpConfig &config)
{
    transportConfig = config;
}

IsoTpConfig IsoTpTransport::config() const
{
    return transportConfig;
}

IsoTpTransport::Result IsoTpTransport::buildRequestFrames(const QByteArray &payload, QVector<CanFrame> *frames) const
{
    if (frames == nullptr || payload.isEmpty()) {
        return Result::InvalidPayload;
    }

    frames->clear();
    if (payload.size() <= 7) {
        frames->append(buildClassicFrame(payload));
        return Result::Ok;
    }

    return Result::Unsupported;
}

CanFrame IsoTpTransport::buildFirstFrame(const QByteArray &payload, int totalSize) const
{
    QByteArray frameData(8, static_cast<char>(0x55));
    frameData[0] = static_cast<char>(0x10 | ((totalSize >> 8) & 0x0F));
    frameData[1] = static_cast<char>(totalSize & 0xFF);
    for (int index = 0; index < payload.size() && index < 6; ++index) {
        frameData[index + 2] = payload[index];
    }

    CanFrame frame;
    frame.id = transportConfig.requestId;
    frame.channel = transportConfig.channel;
    frame.data = frameData;
    frame.direction = CanFrameDirection::Tx;
    frame.protocol = CanFrameProtocol::ClassicCan;
    return frame;
}

CanFrame IsoTpTransport::buildConsecutiveFrame(const QByteArray &payload, quint8 sequenceNumber) const
{
    QByteArray frameData(8, static_cast<char>(0x55));
    frameData[0] = static_cast<char>(0x20 | (sequenceNumber & 0x0F));
    for (int index = 0; index < payload.size() && index < 7; ++index) {
        frameData[index + 1] = payload[index];
    }

    CanFrame frame;
    frame.id = transportConfig.requestId;
    frame.channel = transportConfig.channel;
    frame.data = frameData;
    frame.direction = CanFrameDirection::Tx;
    frame.protocol = CanFrameProtocol::ClassicCan;
    return frame;
}

bool IsoTpTransport::parseFlowControl(const QByteArray &frameData, IsoTpFlowControl &fc)
{
    if (frameData.size() < 3) {
        return false;
    }
    quint8 type = (static_cast<quint8>(frameData[0]) >> 4) & 0x0F;
    if (type != 3) { // 3: Flow Control
        return false;
    }
    fc.flowStatus = static_cast<quint8>(frameData[0]) & 0x0F;
    fc.blockSize = static_cast<quint8>(frameData[1]);
    fc.stMin = static_cast<quint8>(frameData[2]);
    return true;
}

CanFrame IsoTpTransport::buildClassicFrame(const QByteArray &payload) const
{
    QByteArray frameData(RfidProtocol::ClassicCanDlc, static_cast<char>(RfidProtocol::FillByte));
    frameData[0] = static_cast<char>(payload.size());
    for (int index = 0; index < payload.size() && index < 7; ++index) {
        frameData[index + 1] = payload[index];
    }

    CanFrame frame;
    frame.id = transportConfig.requestId;
    frame.channel = transportConfig.channel;
    frame.data = frameData;
    frame.direction = CanFrameDirection::Tx;
    frame.protocol = CanFrameProtocol::ClassicCan;
    return frame;
}
