#ifndef CANTRANSPORT_H
#define CANTRANSPORT_H

#include <QByteArray>
#include <QtGlobal>

#include "domain/canframe.h"

class CanTransport
{
public:
    enum class RfidControlSendResult {
        Sent,
        Suppressed,
        Failed
    };

    virtual ~CanTransport() = default;

    virtual bool sendData(quint32 id,
                          quint32 frameTypeIndex,
                          quint32 protocolIndex,
                          quint32 canFdExpansionIndex,
                          quint32 channel,
                          const char *data,
                          quint32 length,
                          CanFrame *sentFrame = nullptr) = 0;
    virtual bool sendClassicData(quint32 id,
                                 quint32 channel,
                                 const QByteArray &payload,
                                 CanFrame *sentFrame = nullptr) = 0;
    virtual RfidControlSendResult sendManualRfidControlFrame(
        quint32 channel, bool scanning, CanFrame *sentFrame) = 0;
    virtual void setRfidControlPeriodicEnabled(
        bool enabled, quint32 channel, bool scanning) = 0;
};

#endif // CANTRANSPORT_H
