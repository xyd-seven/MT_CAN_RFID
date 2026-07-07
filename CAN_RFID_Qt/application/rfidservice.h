#ifndef RFIDSERVICE_H
#define RFIDSERVICE_H

#include <QString>
#include "domain/canframe.h"
#include "rfidprotocol.h"

struct RfidState
{
    QString workMode;
    QString cardStatus;
    QString faultStatus;
    QString scanPeriod;
    QString tag;
    QString tagPart1;
    QString tagPart2;
    QString tagPart3;
    QString deviceId;
    QString version;
    QString response;
};

class RfidService
{
public:
    bool handleFrame(const CanFrame &frame);
    RfidState state() const;
    void reset();

    // 获取底层解析出来的原始设备信息，供 OTA 服务使用
    quint8 vendorCode() const { return m_vendorCode; }
    quint16 hardwareVersion() const { return m_hardwareVersion; }
    quint16 softwareVersion() const { return m_softwareVersion; }

private:
    void handleTagPartFrame(quint32 frameId, const QByteArray &payload);
    void handleStatusFrame(const QByteArray &payload);
    void handleVersionFrame(const QByteArray &payload);
    void handleResponseFrame(const QByteArray &payload);
    void updateTagText();
    void updateDeviceIdText();
    static QString withHexValue(const QString &text, quint8 value);

    RfidState currentState;
    QString tagPart1;
    QString tagPart2;
    QString tagPart3;
    QString deviceIdPart1;
    QString deviceIdPart2;

    quint8 m_vendorCode = 0x02; // 默认威科姆超高频 (0x02)
    quint16 m_hardwareVersion = 0;
    quint16 m_softwareVersion = 0;
};

#endif // RFIDSERVICE_H
