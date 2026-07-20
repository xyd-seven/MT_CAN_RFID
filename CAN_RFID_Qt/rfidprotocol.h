#ifndef RFIDPROTOCOL_H
#define RFIDPROTOCOL_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

struct RfidStatus
{
    quint8 workMode = 0;
    quint8 cardStatus = 0;
    quint8 faultStatus = 0;
    quint8 scanPeriod10ms = 0;
    bool valid = false;
};

struct RfidVersion
{
    quint8 bootVersion = 0;
    quint8 vendorCode = 0;
    quint16 hardwareVersion = 0;
    quint16 softwareVersion = 0;
    quint16 materialRecord = 0;
    bool valid = false;
};

struct RfidResponse
{
    quint8 sid = 0;
    quint8 originalSid = 0;
    quint8 negativeCode = 0;
    QByteArray data;
    bool positive = false;
    bool negative = false;
    bool valid = false;
};

class RfidProtocol
{
public:
    enum FrameId {
        ControlFrameId = 0x207,
        RequestFrameId = 0x07,
        ResponseFrameId = 0x107,
        StatusFrameId = 0x2C0,
        TagPart1FrameId = 0x2C1,
        TagPart2FrameId = 0x2C2,
        VersionFrameId = 0x2C3,
        DeviceIdPart1FrameId = 0x2C4,
        DeviceIdPart2FrameId = 0x2C5,
        TagPart3FrameId = 0x2C6
    };
    enum {
        ClassicCanDlc = 8,
        FillByte = 0x55
    };

    static QByteArray buildControlFrame(bool enableScan);
    static QByteArray buildSetScanPeriodFrame(quint8 period10ms);
    static QByteArray buildRestartFrame();

    static QByteArray buildBootAppJumpFrame(quint8 targetMode);
    static QByteArray buildSoftwareResetFrame();
    static QByteArray buildCommunicationControlFrame(bool enableBroadcast);
    static QByteArray buildSetBroadcastPeriodFrame(quint16 canId, quint16 periodMs);
    static QByteArray buildCommunicationDiagnosticFrame(bool enableDiag);
    static QByteArray buildWriteNonVolatileFrame(quint16 dataId, const QByteArray &data);

    static RfidStatus parseStatusFrame(const QByteArray &payload);
    static RfidVersion parseVersionFrame(const QByteArray &payload);
    static RfidResponse parseResponseFrame(const QByteArray &payload);
    static bool hasValidSingleFramePadding(const QByteArray &payload);

    static QString parseAsciiPayload(const QByteArray &payload);
    static bool isUnrecognizedTagPlaceholderPayload(const QByteArray &payload);
    static bool isUnrecognizedTagPlaceholderText(const QString &text);
    static QString workModeText(quint8 value);
    static QString cardStatusText(quint8 value);
    static QString faultStatusText(quint8 value);
    static QString vendorText(quint8 value);

private:
    static QByteArray buildFilledFrame();
    static QByteArray buildSingleFrame(const QByteArray &serviceData);
    static bool isClassicCanPayload(const QByteArray &payload);
    static quint16 readBigEndianUInt16(const QByteArray &payload, int offset);
};

#endif // RFIDPROTOCOL_H
