#include "qingjucanmanager.h"
#include "canthread.h"
#include "domain/crc16.h"
#include <QDateTime>
#include <QMutexLocker>
#include <QThread>
#include <QtGlobal>

QingjuCanManager::QingjuCanManager(CANThread *canThread, QObject *parent)
    : QObject(parent)
    , m_canThread(canThread)
    , m_nextQueue(0)
    , m_channel(0)
{
}

void QingjuCanManager::setSendChannel(int channel)
{
    m_channel = channel;
}

void QingjuCanManager::handleIncomingFrame(const CanFrame &frame)
{
    // 只有扩展帧且ID符合青桔协议
    if (!frame.extendedFrame) {
        return;
    }

    QingjuCanId id = QingjuCanId::parse(frame.id);

    // 目的地址必须为中控 0x01，或者是广播 0x00
    if (id.destAddr != 0x01 && id.destAddr != 0x00) {
        return;
    }

    quint32 key = (static_cast<quint32>(id.srcAddr) << 16) |
                  (static_cast<quint32>(id.destAddr) << 8) |
                  id.queue;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    AssemblyBuffer &buf = m_buffers[key];

    // 超时判定：如果与上一帧间隔超过 100ms，清除缓存重新组包
    if (buf.lastFrameTime > 0 && (now - buf.lastFrameTime) > 100) {
        buf.maxIdx = -1;
        for (int i = 0; i < 32; ++i) {
            buf.frames[i].clear();
        }
    }

    buf.lastFrameTime = now;

    // 存储当前帧
    int idx = id.index;
    if (idx >= 0 && idx < 32) {
        buf.frames[idx] = frame.data;
        if (idx > buf.maxIdx) {
            buf.maxIdx = idx;
        }
    }

    // 检查是否完整
    // 1. 尾帧（index = 0）必须已收到
    if (buf.frames[0].isEmpty()) {
        return;
    }

    // 2. 从 maxIdx 到 0 的每一帧都必须不为空
    bool complete = true;
    for (int i = 0; i <= buf.maxIdx; ++i) {
        if (buf.frames[i].isEmpty()) {
            complete = false;
            break;
        }
    }

    if (!complete) {
        return;
    }

    // 组装报文
    QByteArray packet;
    for (int i = buf.maxIdx; i >= 0; --i) {
        packet.append(buf.frames[i]);
    }

    // 清理缓存
    m_buffers.remove(key);

    // Modbus 报文格式校验：[Src] [Dest] [Func] [Payload] [CRC_L] [CRC_H]
    // 最小长度：1 (src) + 1 (dest) + 1 (func) + 2 (crc) = 5 字节
    // 由于底层物理层没有传输前导的 2 字节地址，这里收到的 packet 最小大小应为 3 字节（func + crc）
    if (packet.size() < 3) {
        return;
    }

    // 将 CAN ID 中的源地址与目的地址做为前导字节补回数据包，以便参与 CRC-16 校验
    QByteArray fullPacket;
    fullPacket.append(id.srcAddr);
    fullPacket.append(id.destAddr);
    fullPacket.append(packet);

    quint16 receivedCrc = static_cast<quint8>(fullPacket.at(fullPacket.size() - 2)) |
                          (static_cast<quint16>(static_cast<quint8>(fullPacket.at(fullPacket.size() - 1))) << 8);

    quint16 calculatedCrc = calculateModbusCrc16(reinterpret_cast<const quint8*>(fullPacket.constData()), fullPacket.size() - 2);

    if (receivedCrc != calculatedCrc) {
        return;
    }

    quint8 src = fullPacket.at(0);
    quint8 dest = fullPacket.at(1);
    quint8 func = fullPacket.at(2);
    QByteArray payload = fullPacket.mid(3, fullPacket.size() - 5);

    emit modbusPacketReceived(src, dest, func, payload);
}

bool QingjuCanManager::sendModbusRequest(quint8 destAddr, quint8 funcCode, const QByteArray &payload, quint8 priority)
{
    if (!m_canThread) {
        return false;
    }
    QMutexLocker locker(&m_sendMutex);

    quint8 srcAddr = 0x01; // ECU 地址固定为 0x01
    QByteArray packet;
    packet.append(srcAddr);
    packet.append(destAddr);
    packet.append(funcCode);
    packet.append(payload);

    // 计算并追加 Modbus CRC16
    quint16 crc = calculateModbusCrc16(reinterpret_cast<const quint8*>(packet.constData()), packet.size());
    packet.append(crc & 0xFF);
    packet.append((crc >> 8) & 0xFF);

    // 剥离数据包前导的 2 字节（srcAddr 与 destAddr），因为它们仅参与 CRC 计算，不通过 CAN 数据区传输
    QByteArray transmitPacket = packet.mid(2);

    int totalBytes = transmitPacket.size();
    int numFrames = (totalBytes + 7) / 8;

    quint8 queue = m_nextQueue;
    m_nextQueue = (m_nextQueue + 1) & 0x07;

    bool success = true;
    for (int i = 0; i < numFrames; ++i) {
        int offset = i * 8;
        int len = qMin(8, totalBytes - offset);
        QByteArray frameData = transmitPacket.mid(offset, len);

        QingjuCanId id;
        id.priority = priority;
        id.reserved1 = 0;
        id.srcAddr = srcAddr;
        id.protoVer = 1;
        id.reserved2 = 0;
        id.destAddr = destAddr;
        id.queue = queue;
        id.index = numFrames - 1 - i; // 从 N-1 递减至 0

        quint32 rawId = id.toRawId();
        if (!m_canThread->sendData(rawId, 1, 0, 0, m_channel, frameData.constData(), frameData.size())) {
            success = false;
        } else {
            emit frameSent(rawId, frameData, m_channel, false);
        }

        // 帧间延时 2ms，防止总线拥堵
        QThread::msleep(2);
    }

    return success;
}

bool QingjuCanManager::writeRegisters(quint8 destAddr, quint16 startReg, const QVector<quint16> &values, bool forceNoAck, quint8 priority)
{
    if (values.isEmpty() || values.size() > 125) {
        return false;
    }
    quint8 func = forceNoAck ? 0x90 : 0x10;
    QByteArray payload;
    payload.append(static_cast<char>((startReg >> 8) & 0xFF));
    payload.append(static_cast<char>(startReg & 0xFF));
    payload.append(static_cast<char>(values.size() & 0xFF));
    for (quint16 val : values) {
        payload.append(static_cast<char>((val >> 8) & 0xFF));
        payload.append(static_cast<char>(val & 0xFF));
    }
    return sendModbusRequest(destAddr, func, payload, priority);
}

bool QingjuCanManager::readRegisters(quint8 destAddr, quint16 startReg, quint8 regCount, quint8 priority)
{
    if (regCount == 0 || regCount > 125) {
        return false;
    }
    QByteArray payload;
    payload.append(static_cast<char>((startReg >> 8) & 0xFF));
    payload.append(static_cast<char>(startReg & 0xFF));
    payload.append(static_cast<char>(regCount));
    return sendModbusRequest(destAddr, 0x03, payload, priority);
}
