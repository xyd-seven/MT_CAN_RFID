#include "canthread.h"

#include <QDateTime>
#include <QString>
#include <cstring>

namespace {
constexpr UINT MaxReceiveFrames = 200;
constexpr UINT CanChannel0 = 0;
constexpr UINT CanChannel1 = 1;
constexpr int RfidControlPeriodMs = 100;
constexpr UINT MaxReceiveBatchesPerCycle = 2;
constexpr unsigned int BacklogReceiveSleepMs = 1;
constexpr unsigned int ActiveReceiveSleepMs = 5;
constexpr unsigned int IdleReceiveSleepMs = 10;
}

CANThread::CANThread() :
    stopped(false),
    m_dev(INVALID_DEVICE_HANDLE),
    m_channel1(INVALID_CHANNEL_HANDLE),
    m_channel2(INVALID_CHANNEL_HANDLE),
    devPtr(nullptr),
    m_canNum(2),
    frameClockValid(false),
    periodicControlEnabled(false),
    periodicControlScanning(false),
    periodicControlResetRequested(true),
    periodicControlChannel(CanChannel0),
    periodicControlNextDueMs(0),
    lastRfidControlSentMs(-1),
    lastRfidControlScanning(false)
{
    std::memset(&m_config, 0, sizeof(m_config));
}

void CANThread::stop()
{
    stopped.store(true);
}

bool CANThread::openDevice(UINT device_type, UINT device_index, UINT reserved)
{
    if (isDeviceOpen()) {
        return false;
    }

    m_dev = ZCAN_OpenDevice(device_type, device_index, reserved);
    if (m_dev == INVALID_DEVICE_HANDLE) {
        return false;
    }

    devPtr = GetIProperty(m_dev);
    if (devPtr == nullptr) {
        ZCAN_CloseDevice(m_dev);
        m_dev = INVALID_DEVICE_HANDLE;
        return false;
    }

    ZCAN_DEVICE_INFO deviceInfo;
    std::memset(&deviceInfo, 0, sizeof(deviceInfo));
    if (ZCAN_GetDeviceInf(m_dev, &deviceInfo) == 1 && deviceInfo.can_Num > 0) {
        m_canNum = deviceInfo.can_Num;
    } else {
        m_canNum = 2;
    }
    resetFrameClock();
    return true;
}

bool CANThread::setCANFDStandard(UINT standard)
{
    if (!isDeviceOpen() || devPtr == nullptr) {
        return false;
    }
    if (devPtr->SetValue("0/canfd_standard", QString::number(standard).toStdString().c_str()) != 1) {
        return false;
    }
    if (m_canNum > 1 &&
        devPtr->SetValue("1/canfd_standard", QString::number(standard).toStdString().c_str()) != 1) {
        return false;
    }
    return true;
}

bool CANThread::setBaudrateFD(UINT rate1, UINT rate2)
{
    if (!isDeviceOpen() || devPtr == nullptr) {
        return false;
    }
    if (devPtr->SetValue("0/canfd_abit_baud_rate", QString::number(rate1).toStdString().c_str()) != 1 ||
        devPtr->SetValue("0/canfd_dbit_baud_rate", QString::number(rate2).toStdString().c_str()) != 1) {
        return false;
    }
    if (m_canNum > 1 &&
        (devPtr->SetValue("1/canfd_abit_baud_rate", QString::number(rate1).toStdString().c_str()) != 1 ||
         devPtr->SetValue("1/canfd_dbit_baud_rate", QString::number(rate2).toStdString().c_str()) != 1)) {
        return false;
    }
    return true;
}

bool CANThread::setClassicBaudrate(UINT rate)
{
    if (!isDeviceOpen() || devPtr == nullptr) {
        return false;
    }
    if (devPtr->SetValue("0/baud_rate", QString::number(rate).toStdString().c_str()) != 1) {
        return false;
    }
    if (m_canNum > 1 &&
        devPtr->SetValue("1/baud_rate", QString::number(rate).toStdString().c_str()) != 1) {
        return false;
    }
    return true;
}

bool CANThread::setCustomBaudrateFD(QString rate)
{
    if (!isDeviceOpen() || devPtr == nullptr) {
        return false;
    }
    if (devPtr->SetValue("0/baud_rate_custom", rate.toStdString().c_str()) != 1) {
        return false;
    }
    if (m_canNum > 1 &&
        devPtr->SetValue("1/baud_rate_custom", rate.toStdString().c_str()) != 1) {
        return false;
    }
    return true;
}

bool CANThread::initCAN()
{
    if (!isDeviceOpen()) {
        return false;
    }
    m_config.can_type = TYPE_CANFD;
    m_channel1 = ZCAN_InitCAN(m_dev, 0, &m_config);
    m_channel2 = (m_canNum > 1) ? ZCAN_InitCAN(m_dev, 1, &m_config) : INVALID_CHANNEL_HANDLE;
    resetFrameClock();
    return isChannelValid(m_channel1) && (m_canNum <= 1 || isChannelValid(m_channel2));
}

bool CANThread::initClassicCAN()
{
    if (!isDeviceOpen()) {
        return false;
    }
    m_config.can_type = TYPE_CAN;
    m_channel1 = ZCAN_InitCAN(m_dev, 0, &m_config);
    m_channel2 = (m_canNum > 1) ? ZCAN_InitCAN(m_dev, 1, &m_config) : INVALID_CHANNEL_HANDLE;
    resetFrameClock();
    return isChannelValid(m_channel1) && (m_canNum <= 1 || isChannelValid(m_channel2));
}

bool CANThread::setResistanceEnable(UINT enable)
{
    if (!isDeviceOpen() || devPtr == nullptr) {
        return false;
    }
    devPtr->SetValue("0/initenal_resistance", QString::number(enable).toStdString().c_str());
    if (m_canNum > 1) {
        devPtr->SetValue("1/initenal_resistance", QString::number(enable).toStdString().c_str());
    }
    return true;
}

bool CANThread::setFilter(UINT filterMode, QString startID, QString endID)
{
    if (!isDeviceOpen() || devPtr == nullptr) {
        return false;
    }
    if (devPtr->SetValue("0/filter_clear", "0") != 1 ||
        devPtr->SetValue("0/filter_mode", QString::number(filterMode).toStdString().c_str()) != 1 ||
        devPtr->SetValue("0/filter_start", startID.toStdString().c_str()) != 1 ||
        devPtr->SetValue("0/filter_end", endID.toStdString().c_str()) != 1 ||
        devPtr->SetValue("0/filter_ack", "0") != 1) {
        return false;
    }

    if (m_canNum > 1 &&
        (devPtr->SetValue("1/filter_clear", "0") != 1 ||
         devPtr->SetValue("1/filter_mode", QString::number(filterMode).toStdString().c_str()) != 1 ||
         devPtr->SetValue("1/filter_start", startID.toStdString().c_str()) != 1 ||
         devPtr->SetValue("1/filter_end", endID.toStdString().c_str()) != 1 ||
         devPtr->SetValue("1/filter_ack", "0") != 1)) {
        return false;
    }
    return true;
}

bool CANThread::startCAN()
{
    if (!isChannelValid(m_channel1)) {
        return false;
    }
    if (ZCAN_StartCAN(m_channel1) != 1) {
        return false;
    }
    if (m_canNum > 1 && (!isChannelValid(m_channel2) || ZCAN_StartCAN(m_channel2) != 1)) {
        return false;
    }
    resetFrameClock();
    return true;
}

UINT CANThread::MakeCanId(uint id, int eff, int rtr, int err)
{
    const uint ueff = static_cast<uint>(eff != 0);
    const uint urtr = static_cast<uint>(rtr != 0);
    const uint uerr = static_cast<uint>(err != 0);
    return id | ueff << 31 | urtr << 30 | uerr << 29;
}

bool CANThread::sendData(UINT id, UINT frame_type_index, UINT protocol_index, UINT canfd_exp_index, UINT channel, const char *data, UINT len, CanFrame *sentFrame)
{
    if (data == nullptr || len == 0) {
        return false;
    }

    CHANNEL_HANDLE ch = (channel == CanChannel0) ? m_channel1 : m_channel2;
    if (!isChannelValid(ch)) {
        return false;
    }

    QMutexLocker transmitLocker(&transmitMutex);
    if (sentFrame != nullptr) {
        sentFrame->id = id;
        sentFrame->channel = channel;
        sentFrame->data = QByteArray(data, static_cast<int>(len));
        sentFrame->direction = CanFrameDirection::Tx;
        sentFrame->protocol = protocol_index == 0
            ? CanFrameProtocol::ClassicCan : CanFrameProtocol::CanFd;
        sentFrame->extendedFrame = frame_type_index != 0;
        sentFrame->remoteFrame = false;
        stampFrame(*sentFrame);
    }
    UINT result = 0;
    if (protocol_index == 0) {
        ZCAN_Transmit_Data canData;
        std::memset(&canData, 0, sizeof(canData));
        canData.frame.can_id = MakeCanId(id, frame_type_index, 0, 0);
        canData.frame.can_dlc = len > 8 ? 8 : len;
        std::memcpy(canData.frame.data, data, canData.frame.can_dlc);
        canData.transmit_type = 1;
        result = ZCAN_Transmit(ch, &canData, 1);
    } else {
        ZCAN_TransmitFD_Data canfdData;
        std::memset(&canfdData, 0, sizeof(canfdData));
        canfdData.frame.can_id = MakeCanId(id, frame_type_index, 0, 0);
        canfdData.frame.len = len > 64 ? 64 : len;
        std::memcpy(canfdData.frame.data, data, canfdData.frame.len);
        canfdData.transmit_type = 1;
        canfdData.frame.flags = (canfd_exp_index != 0) ? 1 : 0;
        result = ZCAN_TransmitFD(ch, &canfdData, 1);
    }
    return result == 1;
}

bool CANThread::sendClassicData(UINT id, UINT channel, const QByteArray &payload, CanFrame *sentFrame)
{
    if (id > 0x7FF || payload.isEmpty() || payload.size() > 8) {
        return false;
    }
    return sendData(id, 0, 0, 0, channel, payload.constData(), static_cast<UINT>(payload.size()), sentFrame);
}

bool CANThread::sendClassicDataWithDlc(UINT id, UINT channel, const QByteArray &payload, UINT dlc, CanFrame *sentFrame)
{
    if (id > 0x7FF || dlc > 8 || payload.size() < static_cast<int>(dlc)) {
        return false;
    }

    const CHANNEL_HANDLE ch = (channel == CanChannel0) ? m_channel1 : m_channel2;
    if (!isChannelValid(ch)) {
        return false;
    }

    QMutexLocker transmitLocker(&transmitMutex);
    if (sentFrame != nullptr) {
        sentFrame->id = id;
        sentFrame->channel = channel;
        sentFrame->data = payload.left(static_cast<int>(dlc));
        sentFrame->direction = CanFrameDirection::Tx;
        sentFrame->protocol = CanFrameProtocol::ClassicCan;
        sentFrame->extendedFrame = false;
        sentFrame->remoteFrame = false;
        stampFrame(*sentFrame);
    }
    ZCAN_Transmit_Data canData;
    std::memset(&canData, 0, sizeof(canData));
    canData.frame.can_id = MakeCanId(id, 0, 0, 0);
    canData.frame.can_dlc = dlc;
    if (dlc > 0) {
        std::memcpy(canData.frame.data, payload.constData(), dlc);
    }
    canData.transmit_type = 1;
    return ZCAN_Transmit(ch, &canData, 1) == 1;
}

void CANThread::closeDevice()
{
    if (isDeviceOpen()) {
        ZCAN_CloseDevice(m_dev);
    }
    m_dev = INVALID_DEVICE_HANDLE;
    m_channel1 = INVALID_CHANNEL_HANDLE;
    m_channel2 = INVALID_CHANNEL_HANDLE;
    devPtr = nullptr;
    m_canNum = 0;
    resetFrameClock();
}

bool CANThread::reSetCAN()
{
    if (!isChannelValid(m_channel1) || ZCAN_ResetCAN(m_channel1) != 1) {
        return false;
    }
    ZCAN_ClearBuffer(m_channel1);

    if (m_canNum > 1 && (!isChannelValid(m_channel2) || ZCAN_ResetCAN(m_channel2) != 1)) {
        return false;
    }
    if (m_canNum > 1) {
        ZCAN_ClearBuffer(m_channel2);
    }

    resetFrameClock();
    return true;
}

void CANThread::run()
{
    ZCAN_Receive_Data recvCANData[MaxReceiveFrames];
    ZCAN_ReceiveFD_Data recvCANFDData[MaxReceiveFrames];

    if (isChannelValid(m_channel1)) {
        ZCAN_ClearBuffer(m_channel1);
    }
    if (m_canNum > 1 && isChannelValid(m_channel2)) {
        ZCAN_ClearBuffer(m_channel2);
    }
    resetFrameClock();
    {
        QMutexLocker locker(&periodicControlMutex);
        periodicControlClock.start();
        periodicControlNextDueMs = 0;
    }
    {
        // 新一轮接收线程启动后，不能沿用上一轮周期发送的去重时间。
        QMutexLocker locker(&rfidControlSendMutex);
        lastRfidControlSentMs = -1;
        lastRfidControlScanning = false;
    }
    while (!stopped.load()) {
        QVector<CanFrame> parsedFrames;
        bool receiveBacklog = false;
        appendPeriodicRfidControlFrame(&parsedFrames);

        auto collectClassicFrames = [&](CHANNEL_HANDLE channelHandle, quint32 channelIndex) {
            if (!isChannelValid(channelHandle)) {
                return;
            }
            for (UINT batch = 0; batch < MaxReceiveBatchesPerCycle; ++batch) {
                const UINT available = ZCAN_GetReceiveNum(channelHandle, TYPE_CAN);
                if (available == 0) {
                    return;
                }
                const UINT requested = qMin(available, MaxReceiveFrames);
                const UINT frameCount = ZCAN_Receive(channelHandle, recvCANData, requested, 0);
                for (UINT index = 0; index < frameCount; ++index) {
                    CanFrame frame;
                    frame.id = GET_ID(recvCANData[index].frame.can_id);
                    frame.channel = channelIndex;
                    frame.data = QByteArray(reinterpret_cast<const char *>(recvCANData[index].frame.data),
                                            recvCANData[index].frame.can_dlc);
                    frame.direction = CanFrameDirection::Rx;
                    frame.protocol = CanFrameProtocol::ClassicCan;
                    frame.extendedFrame = IS_EFF(recvCANData[index].frame.can_id);
                    frame.remoteFrame = IS_RTR(recvCANData[index].frame.can_id);
                    frame.zlgTimestampRaw = recvCANData[index].timestamp;
                    frame.hasZlgTimestamp = true;
                    stampFrame(frame);
                    parsedFrames.append(frame);
                }
                if (frameCount < requested) {
                    return;
                }
            }
            receiveBacklog = receiveBacklog || ZCAN_GetReceiveNum(channelHandle, TYPE_CAN) > 0;
        };

        auto collectCanFdFrames = [&](CHANNEL_HANDLE channelHandle, quint32 channelIndex) {
            if (!isChannelValid(channelHandle)) {
                return;
            }
            for (UINT batch = 0; batch < MaxReceiveBatchesPerCycle; ++batch) {
                const UINT available = ZCAN_GetReceiveNum(channelHandle, TYPE_CANFD);
                if (available == 0) {
                    return;
                }
                const UINT requested = qMin(available, MaxReceiveFrames);
                const UINT frameCount = ZCAN_ReceiveFD(channelHandle, recvCANFDData, requested, 0);
                for (UINT index = 0; index < frameCount; ++index) {
                    CanFrame frame;
                    frame.id = GET_ID(recvCANFDData[index].frame.can_id);
                    frame.channel = channelIndex;
                    frame.data = QByteArray(reinterpret_cast<const char *>(recvCANFDData[index].frame.data),
                                            recvCANFDData[index].frame.len);
                    frame.direction = CanFrameDirection::Rx;
                    frame.protocol = CanFrameProtocol::CanFd;
                    frame.extendedFrame = IS_EFF(recvCANFDData[index].frame.can_id);
                    frame.remoteFrame = IS_RTR(recvCANFDData[index].frame.can_id);
                    frame.zlgTimestampRaw = recvCANFDData[index].timestamp;
                    frame.hasZlgTimestamp = true;
                    stampFrame(frame);
                    parsedFrames.append(frame);
                }
                if (frameCount < requested) {
                    return;
                }
            }
            receiveBacklog = receiveBacklog || ZCAN_GetReceiveNum(channelHandle, TYPE_CANFD) > 0;
        };

        collectClassicFrames(m_channel1, CanChannel0);
        collectCanFdFrames(m_channel1, CanChannel0);
        if (m_canNum > 1) {
            collectClassicFrames(m_channel2, CanChannel1);
            collectCanFdFrames(m_channel2, CanChannel1);
        }

        if (!parsedFrames.isEmpty()) {
            emit recvedFrames(parsedFrames);
        }
        sleep(receiveBacklog ? BacklogReceiveSleepMs
                             : (parsedFrames.isEmpty() ? IdleReceiveSleepMs : ActiveReceiveSleepMs));
    }
    stopped.store(false);
}

void CANThread::sleep(unsigned int msec)
{
    QThread::msleep(msec);
}

bool CANThread::isDeviceOpen() const
{
    return m_dev != INVALID_DEVICE_HANDLE;
}

bool CANThread::isChannelValid(CHANNEL_HANDLE channel) const
{
    return channel != INVALID_CHANNEL_HANDLE && channel != nullptr;
}

void CANThread::resetFrameClock()
{
    QMutexLocker locker(&frameClockMutex);
    frameClock.invalidate();
    frameClockBaseDateTime = QDateTime();
    frameClockValid = false;
}

void CANThread::setRfidControlPeriodicEnabled(bool enabled, UINT channel, bool scanning)
{
    QMutexLocker locker(&periodicControlMutex);
    const bool changed = periodicControlEnabled != enabled ||
        periodicControlChannel != channel || periodicControlScanning != scanning;
    periodicControlEnabled = enabled;
    periodicControlChannel = channel;
    periodicControlScanning = scanning;
    if (changed) {
        if (periodicControlClock.isValid()) {
            periodicControlNextDueMs = periodicControlClock.elapsed() + RfidControlPeriodMs;
            periodicControlResetRequested = false;
        } else {
            periodicControlResetRequested = true;
        }
    }
}

CANThread::RfidControlSendResult CANThread::sendManualRfidControlFrame(UINT channel, bool scanning, CanFrame *sentFrame)
{
    {
        QMutexLocker locker(&periodicControlMutex);
        periodicControlChannel = channel;
        periodicControlScanning = scanning;
        if (periodicControlClock.isValid()) {
            periodicControlNextDueMs = periodicControlClock.elapsed() + RfidControlPeriodMs;
            periodicControlResetRequested = false;
        } else {
            periodicControlResetRequested = true;
        }
    }
    return sendRfidControlFrame(channel, scanning, true, sentFrame);
}

void CANThread::appendPeriodicRfidControlFrame(QVector<CanFrame> *frames)
{
    if (frames == nullptr || !periodicControlClock.isValid()) {
        return;
    }

    bool enabled = false;
    bool scanning = false;
    UINT channel = CanChannel0;
    CanFrame frame;
    {
        QMutexLocker locker(&periodicControlMutex);
        enabled = periodicControlEnabled;
        scanning = periodicControlScanning;
        channel = periodicControlChannel;
        const qint64 nowMs = periodicControlClock.elapsed();
        if (periodicControlResetRequested) {
            periodicControlNextDueMs = nowMs;
            periodicControlResetRequested = false;
        }
        if (!enabled || nowMs < periodicControlNextDueMs) {
            return;
        }

        const qint64 missedPeriods = (nowMs - periodicControlNextDueMs) / RfidControlPeriodMs;
        periodicControlNextDueMs += (missedPeriods + 1) * RfidControlPeriodMs;
        if (sendRfidControlFrame(channel, scanning, false, &frame) != RfidControlSendResult::Sent) {
            return;
        }
    }
    frames->append(frame);
}

CANThread::RfidControlSendResult CANThread::sendRfidControlFrame(UINT channel,
                                                                  bool scanning,
                                                                  bool suppressRecentDuplicate,
                                                                  CanFrame *sentFrame)
{
    QMutexLocker locker(&rfidControlSendMutex);
    const qint64 nowMs = periodicControlClock.isValid() ? periodicControlClock.elapsed() : -1;
    if (suppressRecentDuplicate && nowMs >= 0 && lastRfidControlSentMs >= 0 &&
        scanning == lastRfidControlScanning && nowMs - lastRfidControlSentMs < RfidControlPeriodMs) {
        return RfidControlSendResult::Suppressed;
    }

    QByteArray payload(8, static_cast<char>(0x55));
    payload[0] = scanning ? static_cast<char>(0x01) : static_cast<char>(0x00);
    if (!sendClassicData(0x207, channel, payload, sentFrame)) {
        return RfidControlSendResult::Failed;
    }

    lastRfidControlSentMs = nowMs;
    lastRfidControlScanning = scanning;
    return RfidControlSendResult::Sent;
}

void CANThread::stampFrame(CanFrame &frame)
{
    QMutexLocker locker(&frameClockMutex);
    if (!frameClockValid) {
        frameClockBaseDateTime = QDateTime::currentDateTime();
        frameClock.start();
        frameClockValid = true;
    }
    frame.monotonicElapsedMs = frameClock.elapsed();
    frame.hostDateTime = frameClockBaseDateTime.addMSecs(frame.monotonicElapsedMs);
}
