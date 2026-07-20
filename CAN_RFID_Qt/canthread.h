#ifndef CANTHREAD_H
#define CANTHREAD_H

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMutex>
#include <QThread>
#include <QVector>
#include <atomic>
#include <zlgcan.h>
#include "domain/canframe.h"

class CANThread : public QThread
{
    Q_OBJECT
public:
    enum class RfidControlSendResult {
        Sent,
        Suppressed,
        Failed
    };

    CANThread();

    void stop();

    bool openDevice(UINT device_type, UINT device_index, UINT reserved);
    bool setCANFDStandard(UINT standard);
    bool setBaudrateFD(UINT rate1, UINT rate2);
    bool setClassicBaudrate(UINT rate);
    bool setCustomBaudrateFD(QString rate);
    bool initCAN();
    bool initClassicCAN();
    bool setResistanceEnable(UINT enable);
    bool setFilter(UINT filterMode, QString startID, QString endID);
    bool startCAN();
    bool sendData(UINT id, UINT frame_type_index, UINT protocol_index, UINT canfd_exp_index, UINT channel, const char *data, UINT len, CanFrame *sentFrame = nullptr);
    bool sendClassicData(UINT id, UINT channel, const QByteArray &payload, CanFrame *sentFrame = nullptr);
    // 仅供协议异常用例构造 DLC=0~8 的经典 CAN 帧，常规发送仍使用 sendClassicData。
    bool sendClassicDataWithDlc(UINT id, UINT channel, const QByteArray &payload, UINT dlc, CanFrame *sentFrame = nullptr);
    void closeDevice();
    bool reSetCAN();
    UINT MakeCanId(uint id, int eff, int rtr, int err);
    // 为收发帧写入统一的单调时间和对应显示时间。
    void stampFrame(CanFrame &frame);
    // 在 CAN 工作线程内稳定发送美团 0x207 周期控制帧。
    void setRfidControlPeriodicEnabled(bool enabled, UINT channel, bool scanning);
    // 通过与周期帧共用的发送入口下发标准 0x207，并抑制短时间内同状态重复帧。
    RfidControlSendResult sendManualRfidControlFrame(UINT channel, bool scanning, CanFrame *sentFrame);

signals:
    void recvedFrames(const QVector<CanFrame> &frames);

private:
    void run() override;
    void sleep(unsigned int msec);
    bool isDeviceOpen() const;
    bool isChannelValid(CHANNEL_HANDLE channel) const;
    void resetFrameClock();
    void appendPeriodicRfidControlFrame(QVector<CanFrame> *frames);
    RfidControlSendResult sendRfidControlFrame(UINT channel, bool scanning, bool suppressRecentDuplicate, CanFrame *sentFrame);

    std::atomic_bool stopped;
    DEVICE_HANDLE m_dev;
    CHANNEL_HANDLE m_channel1;
    CHANNEL_HANDLE m_channel2;
    IProperty* devPtr;
    ZCAN_CHANNEL_INIT_CONFIG m_config;
    UINT m_canNum;
    QMutex frameClockMutex;
    QElapsedTimer frameClock;
    QDateTime frameClockBaseDateTime;
    bool frameClockValid;
    QMutex transmitMutex;
    QMutex periodicControlMutex;
    QMutex rfidControlSendMutex;
    bool periodicControlEnabled;
    bool periodicControlScanning;
    bool periodicControlResetRequested;
    UINT periodicControlChannel;
    QElapsedTimer periodicControlClock;
    qint64 periodicControlNextDueMs;
    qint64 lastRfidControlSentMs;
    bool lastRfidControlScanning;
};

#endif // CANTHREAD_H
