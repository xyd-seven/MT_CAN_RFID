#include "rfidprotocol.h"

QByteArray RfidProtocol::buildFilledFrame()
{
    return QByteArray(ClassicCanDlc, static_cast<char>(FillByte));
}

QByteArray RfidProtocol::buildControlFrame(bool enableScan)
{
    QByteArray payload = buildFilledFrame();
    payload[0] = static_cast<char>(enableScan ? 0x01 : 0x00);
    return payload;
}

QByteArray RfidProtocol::buildSetScanPeriodFrame(quint8 period10ms)
{
    QByteArray payload = buildFilledFrame();
    payload[0] = 0x01;
    payload[1] = 0x01;
    payload[2] = static_cast<char>(period10ms);
    return payload;
}

QByteArray RfidProtocol::buildRestartFrame()
{
    QByteArray payload = buildFilledFrame();
    payload[0] = 0x02;
    payload[1] = 0x02;
    return payload;
}

RfidStatus RfidProtocol::parseStatusFrame(const QByteArray &payload)
{
    RfidStatus status;
    if (!isClassicCanPayload(payload)) {
        return status;
    }

    status.workMode = static_cast<quint8>(payload[0]);
    status.cardStatus = static_cast<quint8>(payload[1]);
    status.faultStatus = static_cast<quint8>(payload[2]);
    status.scanPeriod10ms = static_cast<quint8>(payload[3]);
    status.valid = true;
    return status;
}

RfidVersion RfidProtocol::parseVersionFrame(const QByteArray &payload)
{
    RfidVersion version;
    if (!isClassicCanPayload(payload)) {
        return version;
    }

    version.bootVersion = static_cast<quint8>(payload[0]);
    version.vendorCode = static_cast<quint8>(payload[1]);
    version.hardwareVersion = readBigEndianUInt16(payload, 2);
    version.softwareVersion = readBigEndianUInt16(payload, 4);
    version.materialRecord = readBigEndianUInt16(payload, 6);
    version.valid = true;
    return version;
}

RfidResponse RfidProtocol::parseResponseFrame(const QByteArray &payload)
{
    RfidResponse response;
    if (!isClassicCanPayload(payload)) {
        return response;
    }

    response.sid = static_cast<quint8>(payload[1]);
    if (response.sid == 0x41 || response.sid == 0x42) {
        response.positive = true;
        response.valid = true;
        return response;
    }

    if (response.sid == 0x7F) {
        response.originalSid = static_cast<quint8>(payload[2]);
        response.negativeCode = static_cast<quint8>(payload[3]);
        response.negative = true;
        response.valid = true;
    }
    return response;
}

QString RfidProtocol::parseAsciiPayload(const QByteArray &payload)
{
    if (!isClassicCanPayload(payload)) {
        return QString();
    }

    int payloadEnd = payload.size();
    while (payloadEnd > 0) {
        const quint8 value = static_cast<quint8>(payload[payloadEnd - 1]);
        if (value != 0x00 && value != FillByte) {
            break;
        }
        --payloadEnd;
    }

    QByteArray ascii;
    for (int index = 0; index < payloadEnd; ++index) {
        const char value = payload[index];
        if (value != '\0') {
            ascii.append(value);
        }
    }
    return QString::fromLatin1(ascii);
}

QString RfidProtocol::workModeText(quint8 value)
{
    switch (value) {
    case 0x00:
        return QStringLiteral("停止检测");
    case 0x01:
        return QStringLiteral("开始检测");
    default:
        return QStringLiteral("非法值");
    }
}

QString RfidProtocol::cardStatusText(quint8 value)
{
    switch (value) {
    case 0x00:
        return QStringLiteral("未识别到 TAG");
    case 0x01:
        return QStringLiteral("识别到 TAG");
    case 0x02:
        return QStringLiteral("TAG 长度异常");
    default:
        return QStringLiteral("非法值");
    }
}

QString RfidProtocol::faultStatusText(quint8 value)
{
    switch (value) {
    case 0x00:
        return QStringLiteral("无故障");
    case 0x01:
        return QStringLiteral("RFID 模块故障");
    case 0x02:
        return QStringLiteral("RFID 通信异常");
    default:
        return QStringLiteral("非法值");
    }
}

QString RfidProtocol::vendorText(quint8 value)
{
    switch (value) {
    case 0x01:
        return QStringLiteral("福芯科技高频 RFID");
    case 0x02:
        return QStringLiteral("威科姆超高频 RFID");
    default:
        return QStringLiteral("未知厂商");
    }
}

bool RfidProtocol::isClassicCanPayload(const QByteArray &payload)
{
    return payload.size() == ClassicCanDlc;
}

quint16 RfidProtocol::readBigEndianUInt16(const QByteArray &payload, int offset)
{
    return static_cast<quint16>(
        (static_cast<quint8>(payload[offset]) << 8) |
        static_cast<quint8>(payload[offset + 1]));
}
