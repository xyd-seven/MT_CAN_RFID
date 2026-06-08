#ifndef ISOTPTRANSPORT_H
#define ISOTPTRANSPORT_H

#include <QByteArray>
#include <QVector>
#include "domain/canframe.h"

struct IsoTpConfig
{
    quint32 requestId = 0x07;
    quint32 responseId = 0x107;
    quint32 channel = 0;
    int flowControlTimeoutMs = 500;
    int responseTimeoutMs = 3000;
    int consecutiveFrameIntervalMs = 1;
};

struct IsoTpFlowControl
{
    quint8 flowStatus = 0; // 0: CTS (Clear to Send), 1: WAIT, 2: OVERFLOW
    quint8 blockSize = 0;
    quint8 stMin = 0;      // Separation time (ms)
};

class IsoTpTransport
{
public:
    enum class Result
    {
        Ok,
        Unsupported,
        InvalidPayload,
        Timeout,
        Failed
    };

    explicit IsoTpTransport(const IsoTpConfig &config = IsoTpConfig());

    void setConfig(const IsoTpConfig &config);
    IsoTpConfig config() const;

    // 构建单包/单帧（Payload <= 7）
    Result buildRequestFrames(const QByteArray &payload, QVector<CanFrame> *frames) const;

    // ISO-TP 组包辅助：构建首帧 (FF)
    CanFrame buildFirstFrame(const QByteArray &payload, int totalSize) const;

    // ISO-TP 组包辅助：构建连续帧 (CF)，sequenceNumber 范围 0x0 ~ 0xF
    CanFrame buildConsecutiveFrame(const QByteArray &payload, quint8 sequenceNumber) const;

    // ISO-TP 流控帧 (FC) 解析
    static bool parseFlowControl(const QByteArray &frameData, IsoTpFlowControl &fc);

private:
    CanFrame buildClassicFrame(const QByteArray &payload) const;

    IsoTpConfig transportConfig;
};

#endif // ISOTPTRANSPORT_H
