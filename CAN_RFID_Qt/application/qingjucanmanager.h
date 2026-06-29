#ifndef QINGJUCANMANAGER_H
#define QINGJUCANMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QMap>
#include <QMutex>
#include <QVector>
#include "domain/canframe.h"
#include "domain/qingjucanid.h"

class CANThread;

class QingjuCanManager : public QObject
{
    Q_OBJECT
public:
    explicit QingjuCanManager(CANThread *canThread, QObject *parent = nullptr);

    // 接收底层数据入口
    void handleIncomingFrame(const CanFrame &frame);

    // 发送通用 Modbus 报文 (自动计算并追加 CRC16，支持多帧拆包发送)
    bool sendModbusRequest(quint8 destAddr, quint8 funcCode, const QByteArray &payload, quint8 priority = 5);

    // 辅助接口：写入多个寄存器 (功能码 0x10 或 0x90)
    bool writeRegisters(quint8 destAddr, quint16 startReg, const QVector<quint16> &values, bool forceNoAck = false, quint8 priority = 5);

    // 辅助接口：读取多个寄存器 (功能码 0x03)
    bool readRegisters(quint8 destAddr, quint16 startReg, quint8 regCount, quint8 priority = 5);

public slots:
    void setSendChannel(int channel);

signals:
    // 当收到并重组出完整的 Modbus 包且 CRC 校验通过时触发
    void modbusPacketReceived(quint8 srcAddr, quint8 destAddr, quint8 funcCode, const QByteArray &payload);

private:
    struct AssemblyBuffer {
        qint64 lastFrameTime = 0;
        int maxIdx = -1;
        QByteArray frames[32];
    };

    CANThread *m_canThread;
    quint8 m_nextQueue;
    QMap<quint32, AssemblyBuffer> m_buffers; // key: (srcAddr << 16) | (destAddr << 8) | queue
    QMutex m_sendMutex;
    int m_channel;
};

#endif // QINGJUCANMANAGER_H
