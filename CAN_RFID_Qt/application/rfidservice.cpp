#include "rfidservice.h"

bool RfidService::handleFrame(const CanFrame &frame)
{
    if (frame.protocol != CanFrameProtocol::ClassicCan ||
        frame.data.size() != RfidProtocol::ClassicCanDlc) {
        return false;
    }

    switch (frame.id) {
    case RfidProtocol::StatusFrameId:
        handleStatusFrame(frame.data);
        return true;
    case RfidProtocol::TagPart1FrameId:
    case RfidProtocol::TagPart2FrameId:
    case RfidProtocol::TagPart3FrameId:
        handleTagPartFrame(frame.id, frame.data);
        return true;
    case RfidProtocol::VersionFrameId:
        handleVersionFrame(frame.data);
        return true;
    case RfidProtocol::DeviceIdPart1FrameId:
        deviceIdPart1 = RfidProtocol::parseAsciiPayload(frame.data);
        updateDeviceIdText();
        return true;
    case RfidProtocol::DeviceIdPart2FrameId:
        deviceIdPart2 = RfidProtocol::parseAsciiPayload(frame.data);
        updateDeviceIdText();
        return true;
    case RfidProtocol::ResponseFrameId:
        handleResponseFrame(frame.data);
        return true;
    default:
        return false;
    }
}

void RfidService::handleTagPartFrame(quint32 frameId, const QByteArray &payload)
{
    if (RfidProtocol::isUnrecognizedTagPlaceholderPayload(payload)) {
        if (frameId == RfidProtocol::TagPart3FrameId) {
            tagPart3.clear();
            currentState.tagPart3.clear();
        } else {
            tagPart1.clear();
            tagPart2.clear();
            tagPart3.clear();
            currentState.tagPart1.clear();
            currentState.tagPart2.clear();
            currentState.tagPart3.clear();
        }
        updateTagText();
        return;
    }

    const QString payloadText = RfidProtocol::parseAsciiPayload(payload);
    if (frameId == RfidProtocol::TagPart1FrameId) {
        tagPart1 = payloadText;
        currentState.tagPart1 = tagPart1;
    } else if (frameId == RfidProtocol::TagPart2FrameId) {
        tagPart2 = payloadText;
        currentState.tagPart2 = tagPart2;
    } else if (frameId == RfidProtocol::TagPart3FrameId) {
        tagPart3 = payloadText;
        currentState.tagPart3 = tagPart3;
    }
    updateTagText();
}

RfidState RfidService::state() const
{
    return currentState;
}

void RfidService::reset()
{
    currentState = RfidState();
    tagPart1.clear();
    tagPart2.clear();
    tagPart3.clear();
    deviceIdPart1.clear();
    deviceIdPart2.clear();
}

void RfidService::handleStatusFrame(const QByteArray &payload)
{
    const RfidStatus status = RfidProtocol::parseStatusFrame(payload);
    if (!status.valid) {
        return;
    }

    currentState.workMode = withHexValue(RfidProtocol::workModeText(status.workMode), status.workMode);
    currentState.cardStatus = withHexValue(RfidProtocol::cardStatusText(status.cardStatus), status.cardStatus);
    currentState.faultStatus = withHexValue(RfidProtocol::faultStatusText(status.faultStatus), status.faultStatus);
    currentState.scanPeriod = QString("%1 ms").arg(static_cast<int>(status.scanPeriod10ms) * 10);

    // 如果未识别到卡片，自动将卡片数据清空
    if (status.cardStatus == 0x00) {
        tagPart1.clear();
        tagPart2.clear();
        tagPart3.clear();
        currentState.tag.clear();
        currentState.tagPart1.clear();
        currentState.tagPart2.clear();
        currentState.tagPart3.clear();
    }
}

void RfidService::handleVersionFrame(const QByteArray &payload)
{
    const RfidVersion version = RfidProtocol::parseVersionFrame(payload);
    if (!version.valid) {
        return;
    }

    m_vendorCode = version.vendorCode;
    m_hardwareVersion = version.hardwareVersion;
    m_softwareVersion = version.softwareVersion;

    int hwMajor = (version.hardwareVersion >> 8) & 0xFF;
    int hwMinor = version.hardwareVersion & 0xFF;
    int swMajor = (version.softwareVersion >> 8) & 0xFF;
    int swMinor = version.softwareVersion & 0xFF;

    currentState.version = QString("Boot:%1 Vendor:%2 HW:v%3.%4 SW:v%5.%6 MAT:%7")
        .arg(static_cast<int>(version.bootVersion))
        .arg(RfidProtocol::vendorText(version.vendorCode))
        .arg(hwMajor, 2, 16, QChar('0'))
        .arg(hwMinor, 2, 16, QChar('0'))
        .arg(swMajor, 2, 16, QChar('0'))
        .arg(swMinor, 2, 16, QChar('0'))
        .arg(static_cast<int>(version.materialRecord), 4, 16, QChar('0'));
}

void RfidService::handleResponseFrame(const QByteArray &payload)
{
    const RfidResponse response = RfidProtocol::parseResponseFrame(payload);
    if (!response.valid) {
        return;
    }

    if (response.positive) {
        if (response.data.isEmpty()) {
            currentState.response = QString("Positive SID=0x%1")
                .arg(static_cast<int>(response.sid), 2, 16, QChar('0'));
        } else {
            currentState.response = QString("Positive SID=0x%1 Data=%2")
                .arg(static_cast<int>(response.sid), 2, 16, QChar('0'))
                .arg(QString::fromLatin1(response.data.toHex(' ').toUpper()));
        }
        return;
    }

    if (response.negative) {
        currentState.response = QString("Negative SID=0x%1 NRC=0x%2")
            .arg(static_cast<int>(response.originalSid), 2, 16, QChar('0'))
            .arg(static_cast<int>(response.negativeCode), 2, 16, QChar('0'));
    }
}

void RfidService::updateTagText()
{
    const QString tagText = tagPart1 + tagPart2 + tagPart3;
    currentState.tag = RfidProtocol::isUnrecognizedTagPlaceholderText(tagText) ? QString() : tagText;
}

void RfidService::updateDeviceIdText()
{
    currentState.deviceId = deviceIdPart1 + deviceIdPart2;
}

QString RfidService::withHexValue(const QString &text, quint8 value)
{
    return QString("%1 (0x%2)").arg(text).arg(static_cast<int>(value), 2, 16, QChar('0'));
}
