#ifndef CANTHREAD_H
#define CANTHREAD_H

#include <QByteArray>
#include <QDateTime>
#include <QThread>
#include <QVector>
#include <atomic>
#include <zlgcan.h>
#include "domain/canframe.h"

class CANThread : public QThread
{
    Q_OBJECT
public:
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
    bool sendData(UINT id, UINT frame_type_index, UINT protocol_index, UINT canfd_exp_index, UINT channel, const char *data, UINT len);
    bool sendClassicData(UINT id, UINT channel, const QByteArray &payload);
    void closeDevice();
    bool reSetCAN();
    UINT MakeCanId(uint id, int eff, int rtr, int err);

signals:
    void recvedFrames(const QVector<CanFrame> &frames);

private:
    void run() override;
    void sleep(unsigned int msec);
    bool isDeviceOpen() const;
    bool isChannelValid(CHANNEL_HANDLE channel) const;
    void resetTimestampBase();
    QDateTime mappedHostDateTime(quint64 deviceTimestampMs);

    std::atomic_bool stopped;
    DEVICE_HANDLE m_dev;
    CHANNEL_HANDLE m_channel1;
    CHANNEL_HANDLE m_channel2;
    IProperty* devPtr;
    ZCAN_CHANNEL_INIT_CONFIG m_config;
    UINT m_canNum;
    bool timestampBaseValid;
    quint64 firstDeviceTimestampMs;
    QDateTime firstHostDateTime;
};

#endif // CANTHREAD_H
