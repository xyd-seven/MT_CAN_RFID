#include "rs485rfidservice.h"
#include <QDebug>

static quint16 calculateCrc16Xmodem(const char *data, int len)
{
    quint16 wCRCin = 0x0000;
    quint16 wCPoly = 0x1021;
    for (int j = 0; j < len; ++j) {
        quint8 wChar = static_cast<quint8>(data[j]);
        wCRCin ^= (static_cast<quint16>(wChar) << 8);
        for (int i = 0; i < 8; i++) {
            if (wCRCin & 0x8000) {
                wCRCin = (wCRCin << 1) ^ wCPoly;
            } else {
                wCRCin = wCRCin << 1;
            }
        }
    }
    return wCRCin;
}

Rs485RfidService::Rs485RfidService(Rs485Manager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_isScanning(false)
    , m_protocolMode(2) // BB
    , m_readMode(1) // Auto Poll
    , m_infoStep(0)
    , m_waitingForResponse(false)
    , m_pendingCmdCode(0)
{
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &Rs485RfidService::onPollTimeout);

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &Rs485RfidService::onResponseTimeout);

    connect(m_manager, &Rs485Manager::packetReceived, this, &Rs485RfidService::onPacketReceived);
}

Rs485RfidService::~Rs485RfidService()
{
    stopScan();
}

void Rs485RfidService::setProtocolMode(int mode)
{
    m_protocolMode = mode;
    reset();
}

void Rs485RfidService::startScan(int hostPollIntervalMs, int readMode)
{
    if (m_isScanning) return;

    m_isScanning = true;
    m_readMode = readMode;
    m_pollTimer->setInterval(hostPollIntervalMs);

    // For FF protocol, send start detection (0x06) command
    if (m_protocolMode == 3) {
        sendFfPacket(0x06, QByteArray());
    }

    // Query device info first
    queryDeviceInfo();

    // Start periodic polling or single query timer
    if (m_readMode == 1) {
        m_pollTimer->start();
    }
}

void Rs485RfidService::stopScan()
{
    if (!m_isScanning) return;

    m_isScanning = false;
    m_pollTimer->stop();
    stopTimeoutGuard();

    // For FF protocol, send stop detection (0x08) command
    if (m_protocolMode == 3) {
        sendFfPacket(0x08, QByteArray());
    }
}

void Rs485RfidService::reset()
{
    stopScan();
    m_state = Rs485State();
    m_state.protocolMode = m_protocolMode;
    m_infoStep = 0;
    m_waitingForResponse = false;
    m_pendingCmdCode = 0;
    emit stateUpdated(m_state);
}

void Rs485RfidService::queryDeviceInfo()
{
    m_infoStep = 1;
    if (m_protocolMode == 2) {
        // BB: Query hardware version (type 0x00)
        sendBbPacket(0x00, 0x03, QByteArray::fromHex("00"));
        m_pendingCmdCode = 0x03;
        startTimeoutGuard();
    } else {
        // FF: Query version (0x04)
        sendFfPacket(0x04, QByteArray());
        m_pendingCmdCode = 0x05;
        startTimeoutGuard();
    }
}

void Rs485RfidService::triggerSingleQuery()
{
    if (m_protocolMode == 2) {
        sendBbPacket(0x00, 0x22, QByteArray());
        m_pendingCmdCode = 0x22;
        startTimeoutGuard();
    } else {
        sendFfPacket(0x00, QByteArray());
        m_pendingCmdCode = 0x01; // FF Query tag ID replies with code 0x01
        startTimeoutGuard();
    }
}

void Rs485RfidService::setPower(int powerRaw01Dbm)
{
    quint16 rawPower = static_cast<quint16>(powerRaw01Dbm);
    QByteArray payload;
    payload.append(static_cast<char>((rawPower >> 8) & 0xFF));
    payload.append(static_cast<char>(rawPower & 0xFF));

    if (m_protocolMode == 2) {
        sendBbPacket(0x00, 0xB6, payload);
        m_pendingCmdCode = 0xB6;
        startTimeoutGuard();
    } else {
        sendFfPacket(0x0E, payload);
        m_pendingCmdCode = 0x0F;
        startTimeoutGuard();
    }
}

void Rs485RfidService::queryPower()
{
    if (m_protocolMode == 2) {
        sendBbPacket(0x00, 0xB7, QByteArray());
        m_pendingCmdCode = 0xB7;
        startTimeoutGuard();
    } else {
        sendFfPacket(0x10, QByteArray());
        m_pendingCmdCode = 0x11;
        startTimeoutGuard();
    }
}

void Rs485RfidService::ffReboot()
{
    if (m_protocolMode == 3) {
        sendFfPacket(0x02, QByteArray());
        m_pendingCmdCode = 0x03;
        startTimeoutGuard();
    }
}

void Rs485RfidService::ffSetDemodulatorParams(int mixer, int ifAmp, int thrd)
{
    if (m_protocolMode == 3) {
        QByteArray payload;
        payload.append(static_cast<char>(mixer));
        payload.append(static_cast<char>(ifAmp));
        payload.append(static_cast<char>((thrd >> 8) & 0xFF));
        payload.append(static_cast<char>(thrd & 0xFF));
        sendFfPacket(0x12, payload);
        m_pendingCmdCode = 0x13;
        startTimeoutGuard();
    }
}

void Rs485RfidService::ffQueryDemodulatorParams()
{
    if (m_protocolMode == 3) {
        sendFfPacket(0x14, QByteArray());
        m_pendingCmdCode = 0x15;
        startTimeoutGuard();
    }
}

void Rs485RfidService::ffQueryCardSwitch()
{
    if (m_protocolMode == 3) {
        sendFfPacket(0x16, QByteArray());
        m_pendingCmdCode = 0x17;
        startTimeoutGuard();
    }
}

void Rs485RfidService::onPollTimeout()
{
    if (!m_isScanning) return;

    if (m_waitingForResponse) {
        emit pollSkipped();
        return;
    }
    
    // In auto polling mode, trigger read
    if (m_protocolMode == 2) {
        sendBbPacket(0x00, 0x22, QByteArray());
        m_pendingCmdCode = 0x22;
        startTimeoutGuard();
    } else {
        sendFfPacket(0x00, QByteArray());
        m_pendingCmdCode = 0x01;
        startTimeoutGuard();
    }
}

void Rs485RfidService::onPacketReceived(quint8 cmdCode, const QByteArray &payload)
{
    if (m_waitingForResponse && isExpectedResponse(cmdCode)) {
        stopTimeoutGuard();
    }
    m_state.isCommunicationTimeout = false;
    m_state.errorMsg.clear();

    if (m_protocolMode == 2) {
        handleBbResponse(cmdCode, payload);
    } else {
        handleFfResponse(cmdCode, payload);
    }
}

void Rs485RfidService::onResponseTimeout()
{
    if (m_waitingForResponse) {
        m_waitingForResponse = false;
        m_state.isCommunicationTimeout = true;
        m_state.errorMsg = QStringLiteral("从机应答超时");
        emit stateUpdated(m_state);

        // 如果是单次控制或参数设置指令，通知外部设置失败
        if (m_protocolMode == 2) { // BB
            if (m_pendingCmdCode == 0x11 || m_pendingCmdCode == 0xB6 || m_pendingCmdCode == 0xB7) {
                emit commandFinished(false, QStringLiteral("指令响应超时"));
            }
        } else { // FF
            if (m_pendingCmdCode == 0x03 || m_pendingCmdCode == 0x0F || m_pendingCmdCode == 0x13 ||
                m_pendingCmdCode == 0x07 || m_pendingCmdCode == 0x09 || m_pendingCmdCode == 0x0B ||
                m_pendingCmdCode == 0x11 || m_pendingCmdCode == 0x15 || m_pendingCmdCode == 0x17) {
                emit commandFinished(false, QStringLiteral("指令响应超时"));
            }
        }

        // If we were in the middle of static info query, proceed to next step or complete it
        if (m_infoStep > 0) {
            m_infoStep++;
            if (m_protocolMode == 2) {
                // BB sequence steps: 1: HW version, 2: SW version, 3: Manufacturer, 4: Device ID
                if (m_infoStep == 2) {
                    sendBbPacket(0x00, 0x03, QByteArray::fromHex("01"));
                    m_pendingCmdCode = 0x03;
                } else if (m_infoStep == 3) {
                    sendBbPacket(0x00, 0x03, QByteArray::fromHex("02"));
                    m_pendingCmdCode = 0x03;
                } else if (m_infoStep == 4) {
                    sendBbPacket(0x00, 0x15, QByteArray::fromHex("01"));
                    m_pendingCmdCode = 0x15;
                }
                else m_infoStep = 0;
            } else {
                // FF sequence steps: 1: version, 2: ID
                if (m_infoStep == 2) {
                    sendFfPacket(0x0C, QByteArray());
                    m_pendingCmdCode = 0x0D;
                }
                else m_infoStep = 0;
            }
            if (m_infoStep > 0) {
                startTimeoutGuard();
            }
        }
    }
}

void Rs485RfidService::handleBbResponse(quint8 cmdCode, const QByteArray &payload)
{
    // For BB protocol:
    // Success reply to tag query is cmdCode 0x22, error is 0xFF (payload has error code like 0x15).
    if (cmdCode == 0x22) {
        m_state.errCode = 0;
        if (payload.size() >= 17) {
            // Success format: RSSI (1 byte) + PC (2 bytes) + Tag ID (12 bytes) + CRC (2 bytes)
            m_state.bbRssi = QString("0x%1").arg(static_cast<quint8>(payload.at(0)), 2, 16, QChar('0')).toUpper();
            m_state.bbPc = payload.mid(1, 2).toHex(' ').toUpper();
            m_state.tagId = payload.mid(3, 12).toHex().toUpper();
            m_state.bbCrc = payload.mid(15, 2).toHex(' ').toUpper();
        } else {
            m_state.bbRssi.clear();
            m_state.bbPc.clear();
            m_state.bbCrc.clear();
            m_state.tagId = payload.toHex().toUpper();
        }
        emit stateUpdated(m_state);
    } 
    else if (cmdCode == 0xFF) {
        m_state.tagId.clear();
        m_state.bbRssi.clear();
        m_state.bbPc.clear();
        m_state.bbCrc.clear();
        m_state.errCode = payload.isEmpty() ? 0xFF : static_cast<quint8>(payload.at(0));
        m_state.errorMsg = (m_state.errCode == 0x15) ? QStringLiteral("未扫描到标签") : QStringLiteral("读卡失败");
        emit stateUpdated(m_state);
    }
    else if (cmdCode == 0x03) {
        if (!payload.isEmpty()) {
            quint8 type = static_cast<quint8>(payload.at(0));
            QString val = QString::fromLatin1(payload.mid(1)).trimmed();
            if (type == 0x00) m_state.hwVersion = val;
            else if (type == 0x01) m_state.swVersion = val;
            else if (type == 0x02) m_state.manufacturer = val;
        }
        emit stateUpdated(m_state);

        // Step transition
        if (m_infoStep == 1) {
            m_infoStep = 2;
            sendBbPacket(0x00, 0x03, QByteArray::fromHex("01")); // SW version query
            m_pendingCmdCode = 0x03;
            startTimeoutGuard();
        } else if (m_infoStep == 2) {
            m_infoStep = 3;
            sendBbPacket(0x00, 0x03, QByteArray::fromHex("02")); // Manufacturer query
            m_pendingCmdCode = 0x03;
            startTimeoutGuard();
        } else if (m_infoStep == 3) {
            m_infoStep = 4;
            sendBbPacket(0x00, 0x15, QByteArray::fromHex("01")); // Device ID query
            m_pendingCmdCode = 0x15;
            startTimeoutGuard();
        }
    }
    else if (cmdCode == 0x15) {
        if (!payload.isEmpty()) {
            m_state.deviceId = payload.toHex().toUpper();
        }
        emit stateUpdated(m_state);
        if (m_infoStep == 4) {
            m_infoStep = 0; // Completed
        }
    }
    else if (cmdCode == 0xB6) {
        m_state.errCode = 0;
        emit commandFinished(true, QStringLiteral("设置功率成功"));
        queryPower();
    }
    else if (cmdCode == 0xB7) {
        if (payload.size() >= 2) {
            quint16 rawPower = (static_cast<quint8>(payload.at(0)) << 8) | static_cast<quint8>(payload.at(1));
            m_state.transmitPower = rawPower;
        }
        emit stateUpdated(m_state);
    }
}

void Rs485RfidService::handleFfResponse(quint8 cmdCode, const QByteArray &payload)
{
    if (cmdCode == 0x01) { // Query tag reply
        if (payload.size() == 2 && static_cast<quint8>(payload.at(0)) == 0xFF && static_cast<quint8>(payload.at(1)) == 0xFF) {
            // -1 error: tag not acquired
            m_state.tagId.clear();
            m_state.errCode = -1;
            m_state.errorMsg = QStringLiteral("RFID标签未获取");
        } else if (payload.size() == 2 && static_cast<quint8>(payload.at(0)) == 0xFF && static_cast<quint8>(payload.at(1)) == 0xFE) {
            // -2 error: other errors
            m_state.tagId.clear();
            m_state.errCode = -2;
            m_state.errorMsg = QStringLiteral("其他错误");
        } else {
            // Success
            m_state.tagId = payload.toHex().toUpper();
            m_state.errCode = 0;
            m_state.errorMsg.clear();
        }
        emit stateUpdated(m_state);
    }
    else if (cmdCode == 0x03) {
        emit commandFinished(true, QStringLiteral("重启指令应答成功"));
    }
    else if (cmdCode == 0x05) {
        if (payload.size() >= 3) {
            m_state.manufacturer = QString("0x%1").arg(static_cast<quint8>(payload.at(0)), 2, 16, QChar('0')).toUpper();
            m_state.hwVersion = QString("v%1").arg(static_cast<quint8>(payload.at(1)));
            m_state.swVersion = QString("v%1").arg(static_cast<quint8>(payload.at(2)));
        }
        emit stateUpdated(m_state);
        
        if (m_infoStep == 1) {
            m_infoStep = 2;
            sendFfPacket(0x0C, QByteArray()); // Query ID
            m_pendingCmdCode = 0x0D;
            startTimeoutGuard();
        }
    }
    else if (cmdCode == 0x07) {
        m_state.ffCardSwitch = true;
        emit stateUpdated(m_state);
        emit commandFinished(true, QStringLiteral("开始检测成功"));
    }
    else if (cmdCode == 0x09) {
        m_state.ffCardSwitch = false;
        emit stateUpdated(m_state);
        emit commandFinished(true, QStringLiteral("停止检测成功"));
    }
    else if (cmdCode == 0x0B) {
        emit commandFinished(false, QStringLiteral("波特率设置非法"));
    }
    else if (cmdCode == 0x0D) {
        m_state.deviceId = payload.toHex().toUpper();
        emit stateUpdated(m_state);
        if (m_infoStep == 2) {
            m_infoStep = 0; // Completed
        }
    }
    else if (cmdCode == 0x0F) {
        emit commandFinished(true, QStringLiteral("设置功率成功"));
        queryPower();
    }
    else if (cmdCode == 0x11) {
        if (payload.size() >= 2) {
            quint16 rawPower = (static_cast<quint8>(payload.at(0)) << 8) | static_cast<quint8>(payload.at(1));
            m_state.transmitPower = rawPower;
        }
        emit stateUpdated(m_state);
    }
    else if (cmdCode == 0x13) {
        emit commandFinished(true, QStringLiteral("设置解调参数成功"));
        ffQueryDemodulatorParams();
    }
    else if (cmdCode == 0x15) {
        if (payload.size() >= 4) {
            m_state.ffMixer = static_cast<quint8>(payload.at(0));
            m_state.ffIfAmp = static_cast<quint8>(payload.at(1));
            m_state.ffThrd = (static_cast<quint8>(payload.at(2)) << 8) | static_cast<quint8>(payload.at(3));
        }
        emit stateUpdated(m_state);
    }
    else if (cmdCode == 0x17) {
        if (!payload.isEmpty()) {
            m_state.ffCardSwitch = (static_cast<quint8>(payload.at(0)) == 0x01);
        }
        emit stateUpdated(m_state);
    }
}

void Rs485RfidService::startTimeoutGuard(int timeoutMs)
{
    m_waitingForResponse = true;
    m_timeoutTimer->start(timeoutMs);
}

void Rs485RfidService::stopTimeoutGuard()
{
    m_waitingForResponse = false;
    m_timeoutTimer->stop();
}

bool Rs485RfidService::isExpectedResponse(quint8 cmdCode) const
{
    if (m_pendingCmdCode == 0) {
        return false;
    }

    if (m_protocolMode == 2 && m_pendingCmdCode == 0x22 && cmdCode == 0xFF) {
        return true;
    }

    return cmdCode == m_pendingCmdCode;
}

bool Rs485RfidService::sendBbPacket(quint8 type, quint8 code, const QByteArray &payload)
{
    QByteArray pkt;
    pkt.append(static_cast<char>(0xBB));
    pkt.append(static_cast<char>(type));
    pkt.append(static_cast<char>(code));
    int len = payload.size();
    pkt.append(static_cast<char>((len >> 8) & 0xFF));
    pkt.append(static_cast<char>(len & 0xFF));
    pkt.append(payload);
    
    quint8 sum = 0;
    for (int i = 1; i < pkt.size(); ++i) {
        sum += static_cast<quint8>(pkt.at(i));
    }
    pkt.append(static_cast<char>(sum));
    pkt.append(static_cast<char>(0x7E));
    
    return m_manager->sendRawData(pkt);
}

bool Rs485RfidService::sendFfPacket(quint8 code, const QByteArray &payload)
{
    QByteArray pkt;
    pkt.append(static_cast<char>(0xFF));
    pkt.append(static_cast<char>(0x02)); // Default Address 0x02
    pkt.append(static_cast<char>(code));
    pkt.append(static_cast<char>(payload.size()));
    pkt.append(payload);
    
    // Calculate CRC16/XMODEM
    quint16 crc = calculateCrc16Xmodem(pkt.constData(), pkt.size());
    pkt.append(static_cast<char>((crc >> 8) & 0xFF));
    pkt.append(static_cast<char>(crc & 0xFF));
    
    return m_manager->sendRawData(pkt);
}
