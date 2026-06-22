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
    QByteArray data;
    data.append(static_cast<char>(0x01));
    data.append(static_cast<char>(period10ms));
    return buildSingleFrame(data);
}

QByteArray RfidProtocol::buildRestartFrame()
{
    QByteArray data;
    data.append(static_cast<char>(0x02));
    return buildSingleFrame(data);
}

QByteArray RfidProtocol::buildSingleFrame(const QByteArray &serviceData)
{
    QByteArray payload = buildFilledFrame();
    if (serviceData.isEmpty() || serviceData.size() > 7) {
        return payload;
    }
    payload[0] = static_cast<char>(serviceData.size());
    for (int index = 0; index < serviceData.size(); ++index) {
        payload[index + 1] = serviceData[index];
    }
    return payload;
}

QByteArray RfidProtocol::buildBootAppJumpFrame(quint8 targetMode)
{
    QByteArray data;
    data.append(static_cast<char>(0x10));
    data.append(static_cast<char>(targetMode));
    return buildSingleFrame(data);
}

QByteArray RfidProtocol::buildSoftwareResetFrame()
{
    QByteArray data;
    data.append(static_cast<char>(0x11));
    return buildSingleFrame(data);
}

QByteArray RfidProtocol::buildCommunicationControlFrame(bool enableBroadcast)
{
    QByteArray data;
    data.append(static_cast<char>(0x28));
    data.append(static_cast<char>(enableBroadcast ? 0x01 : 0x00));
    return buildSingleFrame(data);
}

QByteArray RfidProtocol::buildSetBroadcastPeriodFrame(quint16 canId, quint16 periodMs)
{
    QByteArray data;
    data.append(static_cast<char>(0x29));
    data.append(static_cast<char>((canId >> 8) & 0xFF));
    data.append(static_cast<char>(canId & 0xFF));
    data.append(static_cast<char>((periodMs >> 8) & 0xFF));
    data.append(static_cast<char>(periodMs & 0xFF));
    return buildSingleFrame(data);
}

QByteArray RfidProtocol::buildCommunicationDiagnosticFrame(bool enableDiag)
{
    QByteArray data;
    data.append(static_cast<char>(0x85));
    data.append(static_cast<char>(enableDiag ? 0x01 : 0x00));
    return buildSingleFrame(data);
}

QByteArray RfidProtocol::buildWriteNonVolatileFrame(quint16 dataId, const QByteArray &data)
{
    if (data.size() > 4) {
        return QByteArray();
    }
    QByteArray sdata;
    sdata.append(static_cast<char>(0x2E));
    sdata.append(static_cast<char>((dataId >> 8) & 0xFF));
    sdata.append(static_cast<char>(dataId & 0xFF));
    sdata.append(data);
    return buildSingleFrame(sdata);
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

    const quint8 frameType = static_cast<quint8>(payload[0]) >> 4;
    const quint8 serviceLength = static_cast<quint8>(payload[0]) & 0x0F;
    if (frameType != 0x00 || serviceLength < 1 || serviceLength > 7) {
        return response;
    }

    response.sid = static_cast<quint8>(payload[1]);
    if (response.sid == 0x7F) {
        if (serviceLength < 3) {
            return response;
        }
        response.originalSid = static_cast<quint8>(payload[2]);
        response.negativeCode = static_cast<quint8>(payload[3]);
        response.negative = true;
        response.valid = true;
        return response;
    }

    if (response.sid >= 0x40 && response.sid <= 0xEF) {
        response.data = payload.mid(2, serviceLength - 1);
        response.positive = true;
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
