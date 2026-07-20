#include "rfiddiagnostictransfer.h"

#include <QtGlobal>

#include "rfidprotocol.h"

namespace {
constexpr int SingleFramePayloadLimit = 7;
constexpr int FirstFramePayloadCapacity = 6;
constexpr int ConsecutiveFramePayloadCapacity = 7;
constexpr int FlowControlTimeoutMs = 500;
constexpr int FinalResponseTimeoutMs = 3000;
constexpr int DefaultStMinMs = 20;
constexpr int MaxWaitFrameCount = 3;
constexpr int MaxIsoTpPayloadSize = 0x0FFF;
}

RfidDiagnosticTransfer::RfidDiagnosticTransfer(QObject *parent)
    : QObject(parent)
    , m_state(State::Idle)
    , m_dataId(0)
    , m_nextOffset(0)
    , m_nextSequenceNumber(1)
    , m_blockSize(0)
    , m_blockSentCount(0)
    , m_stMinMs(DefaultStMinMs)
    , m_waitFrameCount(0)
    , m_timeoutTimer(new QTimer(this))
    , m_cfTimer(new QTimer(this))
{
    m_timeoutTimer->setSingleShot(true);
    m_cfTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &RfidDiagnosticTransfer::onTimeout);
    connect(m_cfTimer, &QTimer::timeout, this, &RfidDiagnosticTransfer::sendNextConsecutiveFrame);
}

bool RfidDiagnosticTransfer::isBusy() const
{
    return m_state != State::Idle;
}

bool RfidDiagnosticTransfer::startWriteNonVolatile(quint16 dataId, const QByteArray &data)
{
    if (isBusy()) {
        emit finished(false, QStringLiteral("已有诊断写入正在进行，请等待完成"));
        return false;
    }
    if (data.isEmpty()) {
        emit finished(false, QStringLiteral("写入数据不能为空"));
        return false;
    }

    QByteArray payload;
    payload.append(static_cast<char>(0x2E));
    payload.append(static_cast<char>((dataId >> 8) & 0xFF));
    payload.append(static_cast<char>(dataId & 0xFF));
    payload.append(data);

    if (payload.size() > MaxIsoTpPayloadSize) {
        emit finished(false, QStringLiteral("写入数据过长，ISO-TP 最大载荷为 4095 字节"));
        return false;
    }

    resetTransfer();
    m_payload = payload;
    m_dataId = dataId;

    if (payload.size() <= SingleFramePayloadLimit) {
        QVector<CanFrame> frames;
        if (m_transport.buildRequestFrames(payload, &frames) != IsoTpTransport::Result::Ok || frames.isEmpty()) {
            failTransfer(QStringLiteral("构建单帧写入请求失败"));
            return false;
        }
        emit logMessage(QStringLiteral("0x2E 单帧写入 DID=0x%1 长度=%2")
                            .arg(dataId, 4, 16, QChar('0')).toUpper()
                            .arg(data.size()));
        emit frameReady(frames.first().id, frames.first().data);
        enterWaitingFinalResponse();
        return true;
    }

    const CanFrame firstFrame = m_transport.buildFirstFrame(payload, payload.size());
    m_nextOffset = FirstFramePayloadCapacity;
    m_nextSequenceNumber = 1;
    emit logMessage(QStringLiteral("0x2E 多帧写入首帧 DID=0x%1 总长度=%2 数据长度=%3")
                        .arg(dataId, 4, 16, QChar('0')).toUpper()
                        .arg(payload.size())
                        .arg(data.size()));
    emit frameReady(firstFrame.id, firstFrame.data);
    enterWaitingFlowControl();
    return true;
}

void RfidDiagnosticTransfer::handleResponseFrame(const CanFrame &frame)
{
    if (m_state == State::Idle ||
        frame.id != RfidProtocol::ResponseFrameId ||
        frame.data.size() != RfidProtocol::ClassicCanDlc) {
        return;
    }

    const RfidResponse response = RfidProtocol::parseResponseFrame(frame.data);
    if (response.valid && response.negative && response.originalSid == 0x2E) {
        failTransfer(QStringLiteral("0x2E 写入失败 NRC=0x%1 %2")
                         .arg(response.negativeCode, 2, 16, QChar('0')).toUpper()
                         .arg(nrcText(response.negativeCode)));
        return;
    }

    if (m_state == State::WaitingFlowControl) {
        handleFlowControl(frame.data);
        return;
    }

    if (m_state == State::WaitingFinalResponse) {
        handleFinalResponse(frame.data);
    }
}

void RfidDiagnosticTransfer::abort()
{
    if (m_state == State::Idle) {
        return;
    }
    failTransfer(QStringLiteral("诊断写入已中止"));
}

void RfidDiagnosticTransfer::onTimeout()
{
    switch (m_state) {
    case State::WaitingFlowControl:
        failTransfer(QStringLiteral("等待流控帧超时"));
        break;
    case State::WaitingFinalResponse:
        failTransfer(QStringLiteral("等待 0x2E 写入响应超时"));
        break;
    default:
        failTransfer(QStringLiteral("诊断写入超时"));
        break;
    }
}

void RfidDiagnosticTransfer::sendNextConsecutiveFrame()
{
    if (m_state != State::SendingConsecutiveFrames) {
        return;
    }

    if (m_nextOffset >= m_payload.size()) {
        enterWaitingFinalResponse();
        return;
    }

    const QByteArray cfPayload = m_payload.mid(m_nextOffset, ConsecutiveFramePayloadCapacity);
    const CanFrame cfFrame = m_transport.buildConsecutiveFrame(cfPayload, m_nextSequenceNumber);
    emit frameReady(cfFrame.id, cfFrame.data);
    emit logMessage(QStringLiteral("0x2E 多帧连续帧 SN=%1 offset=%2")
                        .arg(m_nextSequenceNumber)
                        .arg(m_nextOffset));

    m_nextOffset += cfPayload.size();
    m_nextSequenceNumber = static_cast<quint8>((m_nextSequenceNumber + 1) & 0x0F);
    ++m_blockSentCount;

    if (m_nextOffset >= m_payload.size()) {
        enterWaitingFinalResponse();
        return;
    }

    if (m_blockSize > 0 && m_blockSentCount >= m_blockSize) {
        enterWaitingFlowControl();
        return;
    }

    m_cfTimer->start(m_stMinMs);
}

void RfidDiagnosticTransfer::resetTransfer()
{
    m_timeoutTimer->stop();
    m_cfTimer->stop();
    m_state = State::Idle;
    m_payload.clear();
    m_dataId = 0;
    m_nextOffset = 0;
    m_nextSequenceNumber = 1;
    m_blockSize = 0;
    m_blockSentCount = 0;
    m_stMinMs = DefaultStMinMs;
    m_waitFrameCount = 0;
}

void RfidDiagnosticTransfer::failTransfer(const QString &message)
{
    resetTransfer();
    emit finished(false, message);
}

void RfidDiagnosticTransfer::finishTransfer(const QString &message)
{
    resetTransfer();
    emit finished(true, message);
}

void RfidDiagnosticTransfer::startTimeout(int timeoutMs)
{
    m_timeoutTimer->start(timeoutMs);
}

void RfidDiagnosticTransfer::enterWaitingFlowControl()
{
    m_state = State::WaitingFlowControl;
    m_blockSentCount = 0;
    startTimeout(FlowControlTimeoutMs);
}

void RfidDiagnosticTransfer::enterWaitingFinalResponse()
{
    m_cfTimer->stop();
    m_state = State::WaitingFinalResponse;
    emit payloadSent();
    startTimeout(FinalResponseTimeoutMs);
}

void RfidDiagnosticTransfer::handleFlowControl(const QByteArray &payload)
{
    IsoTpFlowControl fc;
    if (!IsoTpTransport::parseFlowControl(payload, fc)) {
        return;
    }

    if (fc.flowStatus == 1) {
        ++m_waitFrameCount;
        if (m_waitFrameCount > MaxWaitFrameCount) {
            failTransfer(QStringLiteral("设备连续返回 WAIT 流控帧，写入中止"));
            return;
        }
        emit logMessage(QStringLiteral("0x2E 收到 WAIT 流控帧，等待次数=%1").arg(m_waitFrameCount));
        startTimeout(FlowControlTimeoutMs);
        return;
    }

    m_waitFrameCount = 0;
    if (fc.flowStatus == 2) {
        failTransfer(QStringLiteral("设备返回 OVERFLOW 流控帧，写入中止"));
        return;
    }
    if (fc.flowStatus != 0) {
        failTransfer(QStringLiteral("设备返回未知流控状态: %1").arg(fc.flowStatus));
        return;
    }

    m_timeoutTimer->stop();
    m_blockSize = fc.blockSize;
    m_stMinMs = normalizedStMinMs(fc.stMin);
    m_state = State::SendingConsecutiveFrames;
    emit logMessage(QStringLiteral("0x2E 收到 CTS 流控帧 BS=%1 STmin=%2ms")
                        .arg(m_blockSize)
                        .arg(m_stMinMs));
    m_cfTimer->start(m_stMinMs);
}

void RfidDiagnosticTransfer::handleFinalResponse(const QByteArray &payload)
{
    const RfidResponse response = RfidProtocol::parseResponseFrame(payload);
    if (!response.valid) {
        return;
    }

    if (response.positive && response.sid == 0x6E) {
        if (response.data.size() >= 2) {
            const quint16 did = static_cast<quint16>(
                (static_cast<quint8>(response.data.at(0)) << 8) |
                static_cast<quint8>(response.data.at(1)));
            if (did != m_dataId) {
                failTransfer(QStringLiteral("0x2E 响应 DID 不匹配: 0x%1")
                                 .arg(did, 4, 16, QChar('0')).toUpper());
                return;
            }
        }
        finishTransfer(QStringLiteral("0x2E 写入成功 DID=0x%1")
                           .arg(m_dataId, 4, 16, QChar('0')).toUpper());
        return;
    }

    if (response.negative && response.originalSid == 0x2E) {
        failTransfer(QStringLiteral("0x2E 写入失败 NRC=0x%1 %2")
                         .arg(response.negativeCode, 2, 16, QChar('0')).toUpper()
                         .arg(nrcText(response.negativeCode)));
    }
}

int RfidDiagnosticTransfer::normalizedStMinMs(quint8 stMin) const
{
    if (stMin <= 0x7F) {
        return qMax(1, static_cast<int>(stMin));
    }
    if (stMin >= 0xF1 && stMin <= 0xF9) {
        return 1;
    }
    return DefaultStMinMs;
}

QString RfidDiagnosticTransfer::nrcText(quint8 nrc)
{
    switch (nrc) {
    case 0x10: return QStringLiteral("拒绝执行请求动作");
    case 0x11: return QStringLiteral("不支持请求服务");
    case 0x12: return QStringLiteral("不支持请求服务参数");
    case 0x13: return QStringLiteral("报文长度错误或格式非法");
    case 0x22: return QStringLiteral("条件未满足");
    case 0x31: return QStringLiteral("请求超出范围");
    case 0x78: return QStringLiteral("请求动作未完成");
    default: return QStringLiteral("未知否定响应码");
    }
}
