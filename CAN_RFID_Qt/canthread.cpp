#include "canthread.h"

#include <QDateTime>
#include <QString>
#include <cstring>

namespace {
constexpr UINT MaxReceiveFrames = 200;
constexpr UINT CanChannel0 = 0;
constexpr UINT CanChannel1 = 1;
}

CANThread::CANThread() :
    stopped(false),
    m_dev(INVALID_DEVICE_HANDLE),
    m_channel1(INVALID_CHANNEL_HANDLE),
    m_channel2(INVALID_CHANNEL_HANDLE),
    devPtr(nullptr),
    m_canNum(2),
    timestampBaseValid(false),
    firstDeviceTimestampMs(0)
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
    resetTimestampBase();
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
    resetTimestampBase();
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
    resetTimestampBase();
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
    resetTimestampBase();
    return true;
}

UINT CANThread::MakeCanId(uint id, int eff, int rtr, int err)
{
    const uint ueff = static_cast<uint>(eff != 0);
    const uint urtr = static_cast<uint>(rtr != 0);
    const uint uerr = static_cast<uint>(err != 0);
    return id | ueff << 31 | urtr << 30 | uerr << 29;
}

bool CANThread::sendData(UINT id, UINT frame_type_index, UINT protocol_index, UINT canfd_exp_index, UINT channel, const char *data, UINT len)
{
    if (data == nullptr || len == 0) {
        return false;
    }

    CHANNEL_HANDLE ch = (channel == CanChannel0) ? m_channel1 : m_channel2;
    if (!isChannelValid(ch)) {
        return false;
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

bool CANThread::sendClassicData(UINT id, UINT channel, const QByteArray &payload)
{
    if (id > 0x7FF || payload.size() != 8) {
        return false;
    }
    return sendData(id, 0, 0, 0, channel, payload.constData(), static_cast<UINT>(payload.size()));
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
    resetTimestampBase();
}

bool CANThread::reSetCAN()
{
    if (!isChannelValid(m_channel1) || ZCAN_ResetCAN(m_channel1) != 1) {
        return false;
    }
    if (m_canNum > 1 && (!isChannelValid(m_channel2) || ZCAN_ResetCAN(m_channel2) != 1)) {
        return false;
    }
    resetTimestampBase();
    return true;
}

void CANThread::run()
{
    ZCAN_Receive_Data recvCANData[MaxReceiveFrames];
    ZCAN_ReceiveFD_Data recvCANFDData[MaxReceiveFrames];

    while (!stopped.load()) {
        QVector<CanFrame> parsedFrames;

        auto collectClassicFrames = [&](CHANNEL_HANDLE channelHandle, quint32 channelIndex) {
            if (!isChannelValid(channelHandle)) {
                return;
            }
            UINT frameCount = ZCAN_GetReceiveNum(channelHandle, TYPE_CAN);
            if (frameCount == 0) {
                return;
            }
            frameCount = ZCAN_Receive(channelHandle, recvCANData, qMin(frameCount, MaxReceiveFrames), 50);
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
                frame.hostDateTime = mappedHostDateTime(frame.zlgTimestampRaw);
                parsedFrames.append(frame);
            }
        };

        auto collectCanFdFrames = [&](CHANNEL_HANDLE channelHandle, quint32 channelIndex) {
            if (!isChannelValid(channelHandle)) {
                return;
            }
            UINT frameCount = ZCAN_GetReceiveNum(channelHandle, TYPE_CANFD);
            if (frameCount == 0) {
                return;
            }
            frameCount = ZCAN_ReceiveFD(channelHandle, recvCANFDData, qMin(frameCount, MaxReceiveFrames), 50);
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
                frame.hostDateTime = mappedHostDateTime(frame.zlgTimestampRaw);
                parsedFrames.append(frame);
            }
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
        sleep(10);
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

void CANThread::resetTimestampBase()
{
    timestampBaseValid = false;
    firstDeviceTimestampMs = 0;
    firstHostDateTime = QDateTime();
}

QDateTime CANThread::mappedHostDateTime(quint64 deviceTimestampMs)
{
    const QDateTime now = QDateTime::currentDateTime();
    if (!timestampBaseValid || deviceTimestampMs < firstDeviceTimestampMs) {
        timestampBaseValid = true;
        firstDeviceTimestampMs = deviceTimestampMs;
        firstHostDateTime = now;
        return now;
    }
    return firstHostDateTime.addMSecs(static_cast<qint64>(deviceTimestampMs - firstDeviceTimestampMs));
}
