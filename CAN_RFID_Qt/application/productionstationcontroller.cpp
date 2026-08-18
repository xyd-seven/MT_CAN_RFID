#include "productionstationcontroller.h"

#include <QThread>
#include <QVector>

#include "application/qingjucanmanager.h"
#include "application/productioncanworkerclient.h"
#include "domain/cantransport.h"
#include "rfidprotocol.h"

namespace {
constexpr quint8 QingjuProductionAddress = 0x0A;
constexpr quint16 QingjuSnRegister = 0xA00D;
constexpr quint16 QingjuHardwareVersionRegister = 0xA004;
constexpr int QingjuSnRegisterCount = 8;
constexpr int QingjuHardwareVersionRegisterCount = 1;
constexpr int QingjuWriteTimeoutMs = 1000;
constexpr int QingjuReadBackTimeoutMs = 1500;
constexpr int QingjuMaxWriteRetries = 2;
constexpr int QingjuReadBackDelayMs = 50;
constexpr int QingjuImmediateSendRetryDelayMs = 50;

QByteArray qingjuProductionSnPayload(const QByteArray &data)
{
    QByteArray actualData = data;
    constexpr int QingjuQrCodeLongSnLength = 25;
    constexpr int QingjuQrCodeWriteTailLength = 11;
    constexpr int LegacyQingjuSnTailLength = 10;
    if (actualData.size() == QingjuQrCodeLongSnLength) {
        return actualData.right(QingjuQrCodeWriteTailLength);
    }
    if (actualData.size() > LegacyQingjuSnTailLength) {
        actualData = actualData.right(LegacyQingjuSnTailLength);
    }
    if (actualData.size() == LegacyQingjuSnTailLength) {
        actualData = actualData.left(5) + "0" + actualData.mid(5);
    }
    return actualData;
}

QString qingjuReadBackSn(const QByteArray &payload)
{
    if (payload.size() != 17 || static_cast<quint8>(payload.at(0)) != 16) {
        return QString();
    }

    QString value;
    const QByteArray rawSn = payload.mid(1);
    for (char character : rawSn) {
        if (character != '\0' && character != ' ' && character != '\r' && character != '\n') {
            value.append(QChar::fromLatin1(character));
        }
    }
    return value;
}
}

ProductionStationController::ProductionStationController(int stationNumber, QObject *parent)
    : QObject(parent)
    , m_stationNumber(stationNumber)
    , m_deviceIndex(0)
    , m_channel(0)
    , m_deviceReady(false)
    , m_protocolMode(0)
    , m_deviceStatus(QStringLiteral("未启动"))
    , m_canWorker(new ProductionCanWorkerClient(stationNumber))
    , m_testService(this)
    , m_diagnosticTransfer(this)
    , m_qingjuCanManager(new QingjuCanManager(m_canWorker, this))
    , m_qingjuRfidService(new QingjuRfidService(m_qingjuCanManager, this))
    , m_writeTimer(new QTimer(this))
    , m_writePending(false)
    , m_qingjuVerifySnPending(false)
    , m_qingjuVerifyHardwarePending(false)
    , m_qingjuWriteRetryCount(0)
    , m_qingjuWritePendingRegister(0)
    , m_qingjuWritePendingRegisterCount(0)
{
    m_writeTimer->setSingleShot(true);

    connect(m_canWorker, &ProductionCanWorkerClient::receivedFrames,
            this, &ProductionStationController::handleReceivedFrames);
    connect(m_canWorker, &ProductionCanWorkerClient::diagnostic,
            this, &ProductionStationController::logMessage);
    connect(m_canWorker, &ProductionCanWorkerClient::workerDisconnected,
            this, [this]() {
        m_deviceReady = false;
        if (m_testService.isRunning()) {
            m_testService.stop(QStringLiteral("CAN工作进程意外退出"));
        }
        setDeviceStatus(QStringLiteral("CAN工作进程已断开"));
    });
    connect(m_writeTimer, &QTimer::timeout,
            this, &ProductionStationController::handleWriteTimeout);

    connect(&m_testService, &ProductionTestService::writeSnRequested,
            this, &ProductionStationController::handleWriteRequested);
    connect(&m_testService, &ProductionTestService::scanControlRequested,
            this, &ProductionStationController::handleScanControlRequested);
    connect(&m_testService, &ProductionTestService::stateChanged,
            this, &ProductionStationController::testStateChanged);
    connect(&m_testService, &ProductionTestService::logMessage,
            this, &ProductionStationController::logMessage);
    connect(&m_testService, &ProductionTestService::finished,
            this, &ProductionStationController::finished);

    connect(&m_diagnosticTransfer, &RfidDiagnosticTransfer::frameReady,
            this, [this](quint32 canId, const QByteArray &payload) {
        if (!sendClassicFrame(canId, payload)) {
            finishPendingWrite(false, QStringLiteral("CAN发送失败"));
        }
    });
    connect(&m_diagnosticTransfer, &RfidDiagnosticTransfer::logMessage,
            this, &ProductionStationController::logMessage);
    connect(&m_diagnosticTransfer, &RfidDiagnosticTransfer::finished,
            this, [this](bool success, const QString &message) {
        if (m_writePending) {
            finishPendingWrite(success, message);
        }
    });

    connect(m_qingjuCanManager, &QingjuCanManager::frameSent,
            this, &ProductionStationController::frameObserved);
    connect(m_qingjuCanManager, &QingjuCanManager::modbusPacketReceived,
            this, &ProductionStationController::handleQingjuPacket);
    connect(m_qingjuRfidService, &QingjuRfidService::stateUpdated,
            this, [this](const QingjuNpkState &state) {
        if (m_protocolMode == 1) {
            m_testService.handleQingjuStatus(state);
            emit qingjuDeviceInfoUpdated(state.hardwareVer, state.devSn);
            }
    });
    connect(m_qingjuRfidService, &QingjuRfidService::pollingDiagnostic,
            this, &ProductionStationController::logMessage);
}

ProductionStationController::~ProductionStationController()
{
    stopDevice();
    delete m_canWorker;
    m_canWorker = nullptr;
}

bool ProductionStationController::startDevice(quint32 deviceType,
                                              quint32 deviceIndex,
                                              quint32 channel,
                                              bool resistanceEnabled,
                                              QString *error)
{
    if (m_deviceReady) {
        return true;
    }

    m_deviceIndex = deviceIndex;
    m_channel = channel;
    m_qingjuCanManager->setSendChannel(static_cast<int>(channel));
    setDeviceStatus(QStringLiteral("正在打开设备%1").arg(deviceIndex));

    if (!m_canWorker->startDevice(
            deviceType, deviceIndex, channel, resistanceEnabled, error)) {
        setDeviceStatus(QStringLiteral("设备%1工作进程启动失败").arg(deviceIndex));
        return false;
    }

    m_deviceReady = true;
    resetProtocolState();
    setDeviceStatus(QStringLiteral("设备%1/CAN%2 已启动").arg(deviceIndex).arg(channel));
    if (m_protocolMode == 1) {
        QTimer::singleShot(100, this, [this]() {
            if (m_deviceReady && m_protocolMode == 1 && !m_testService.isRunning()) {
                m_qingjuRfidService->queryDeviceInfo(QingjuProductionAddress, false, true);
            }
        });
    }
    return true;
}

void ProductionStationController::stopDevice()
{
    if (m_testService.isRunning()) {
        m_testService.stop(QStringLiteral("CAN设备已关闭"));
    }
    m_writeTimer->stop();
    if (m_qingjuRfidService != nullptr) {
        m_qingjuRfidService->stopScan();
    }
    if (m_diagnosticTransfer.isBusy()) {
        m_diagnosticTransfer.abort();
    }
    m_writePending = false;
    m_qingjuVerifySnPending = false;
    m_qingjuVerifyHardwarePending = false;

    if (m_canWorker != nullptr) {
        m_canWorker->setRfidControlPeriodicEnabled(false, m_channel, false);
        m_canWorker->stopDevice();
    }
    m_deviceReady = false;
    setDeviceStatus(QStringLiteral("未启动"));
}

bool ProductionStationController::startTest(int protocolMode,
                                            const QString &sn,
                                            const QString &hardwareVersion,
                                            const QString &materialChange,
                                            const ProductionTestConfig &config,
                                            QString *error)
{
    if (!m_deviceReady) {
        if (error != nullptr) {
            *error = QStringLiteral("工位%1的CAN设备未启动").arg(m_stationNumber);
        }
        return false;
    }
    if (protocolMode != 0 && protocolMode != 1) {
        if (error != nullptr) {
            *error = QStringLiteral("双工位产线检测仅支持美团和青桔协议");
        }
        return false;
    }

    m_protocolMode = protocolMode;
    resetProtocolState();
    return m_testService.start(protocolMode,
                               sn,
                               hardwareVersion,
                               materialChange,
                               config,
                               error);
}

bool ProductionStationController::setProtocolMode(int protocolMode)
{
    if ((protocolMode != 0 && protocolMode != 1) || m_testService.isRunning()) {
        return false;
    }
    if (m_protocolMode != protocolMode) {
        m_protocolMode = protocolMode;
        resetProtocolState();
    }
    return true;
}

void ProductionStationController::stopTest(const QString &reason)
{
    m_testService.stop(reason);
    m_writeTimer->stop();
    if (m_diagnosticTransfer.isBusy()) {
        m_diagnosticTransfer.abort();
    }
    m_writePending = false;
    m_qingjuVerifySnPending = false;
    m_qingjuVerifyHardwarePending = false;
}

void ProductionStationController::resetTest()
{
    if (m_testService.isRunning()) {
        return;
    }
    m_testService.reset();
    resetProtocolState();
}

int ProductionStationController::stationNumber() const
{
    return m_stationNumber;
}

quint32 ProductionStationController::deviceIndex() const
{
    return m_deviceIndex;
}

quint32 ProductionStationController::channel() const
{
    return m_channel;
}

bool ProductionStationController::isDeviceReady() const
{
    return m_deviceReady;
}

bool ProductionStationController::isRunning() const
{
    return m_testService.isRunning();
}

int ProductionStationController::protocolMode() const
{
    return m_protocolMode;
}

ProductionTestState ProductionStationController::state() const
{
    return m_testService.state();
}

QString ProductionStationController::deviceStatusText() const
{
    return m_deviceStatus;
}

void ProductionStationController::handleReceivedFrames(const QVector<CanFrame> &frames)
{
    for (const CanFrame &frame : frames) {
        if (frame.channel != m_channel) {
            continue;
        }
        emit frameObserved(frame);
        if (m_protocolMode == 1) {
            m_qingjuCanManager->handleIncomingFrame(frame);
        } else {
            handleMeituanFrame(frame);
        }
    }
}

void ProductionStationController::handleMeituanFrame(const CanFrame &frame)
{
    if (frame.protocol != CanFrameProtocol::ClassicCan) {
        return;
    }
    if (frame.id == RfidProtocol::ResponseFrameId) {
        m_diagnosticTransfer.handleResponseFrame(frame);
    }
    if (!m_rfidService.handleFrame(frame)) {
        return;
    }

    const RfidState currentState = m_rfidService.state();
    if (frame.id == RfidProtocol::StatusFrameId) {
        m_testService.handleRfidStatus(currentState);
    } else if (frame.id == RfidProtocol::TagPart1FrameId ||
               frame.id == RfidProtocol::TagPart2FrameId ||
               frame.id == RfidProtocol::TagPart3FrameId) {
        m_testService.handleRfidTagUpdate(currentState.tag);
    }
}

void ProductionStationController::handleQingjuPacket(quint8 sourceAddress,
                                                     quint8 destinationAddress,
                                                     quint8 functionCode,
                                                     const QByteArray &payload)
{
    if (!m_writePending ||
        m_protocolMode != 1 ||
        sourceAddress != QingjuProductionAddress ||
        destinationAddress != 0x01) {
        return;
    }

    if (m_qingjuVerifySnPending) {
        if (functionCode != 0x03) {
            return;
        }
        const QString actualSn = qingjuReadBackSn(payload);
        if (actualSn.isEmpty()) {
            return;
        }

        const QString expectedSn = QString::fromLatin1(
            qingjuProductionSnPayload(m_qingjuWritePendingData));
        m_writeTimer->stop();
        m_qingjuVerifySnPending = false;
        if (actualSn == expectedSn) {
            emit logMessage(QStringLiteral("SN回读校验一致（回读值：%1）").arg(actualSn));
            emit qingjuDeviceInfoUpdated(QString(), actualSn);
            finishPendingWrite(true, QString());
        } else {
            emit logMessage(QStringLiteral("SN回读不一致：期望=%1，实际=%2")
                                .arg(expectedSn, actualSn));
            finishPendingWrite(false, QStringLiteral("SN校验不一致"));
        }
        return;
    }

    if (m_qingjuVerifyHardwarePending) {
        if (functionCode != 0x03) {
            return;
        }
        if (payload.size() != 3 || static_cast<quint8>(payload.at(0)) != 2) {
            finishPendingWrite(false, QStringLiteral("硬件版本回读响应格式错误"));
            return;
        }

        const quint16 actualVersion =
            (static_cast<quint8>(payload.at(1)) << 8) |
            static_cast<quint8>(payload.at(2));
        const quint16 expectedVersion =
            m_qingjuWritePendingData.size() >= 2
                ? (static_cast<quint8>(m_qingjuWritePendingData.at(0)) << 8) |
                      static_cast<quint8>(m_qingjuWritePendingData.at(1))
                : 0;
        m_writeTimer->stop();
        m_qingjuVerifyHardwarePending = false;
        if (actualVersion == expectedVersion) {
            const QString hardwareVersionText =
                QStringLiteral("v%1.%2")
                    .arg((actualVersion >> 8) & 0xFF)
                    .arg(actualVersion & 0xFF);
            emit logMessage(
                QStringLiteral("硬件版本回读校验一致（回读值：0x%1）")
                    .arg(actualVersion, 4, 16, QLatin1Char('0'))
                    .toUpper());
            emit qingjuDeviceInfoUpdated(hardwareVersionText, QString());
            finishPendingWrite(true, QString());
        } else {
            emit logMessage(
                QStringLiteral("硬件版本回读不一致：期望=0x%1，实际=0x%2")
                    .arg(expectedVersion, 4, 16, QLatin1Char('0'))
                    .arg(actualVersion, 4, 16, QLatin1Char('0'))
                    .toUpper());
            finishPendingWrite(false, QStringLiteral("硬件版本回读校验不一致"));
        }
        return;
    }

    const bool writingSn = m_qingjuWritePendingRegister == QingjuSnRegister;
    const bool writingHardwareVersion =
        m_qingjuWritePendingRegister == QingjuHardwareVersionRegister;
    bool writeAccepted = false;
    if (functionCode == 0x10 && payload.size() >= 3) {
        const quint16 startRegister =
            (static_cast<quint8>(payload.at(0)) << 8) |
            static_cast<quint8>(payload.at(1));
        const quint16 registerCount = static_cast<quint8>(payload.at(2));
        writeAccepted = startRegister == m_qingjuWritePendingRegister &&
                        registerCount == m_qingjuWritePendingRegisterCount;
    } else if (functionCode == 0x90 && writingSn) {
        writeAccepted = true;
        const QString exceptionCode = payload.isEmpty()
                                          ? QStringLiteral("未知")
                                          : QStringLiteral("0x%1")
                                                .arg(static_cast<quint8>(payload.at(0)),
                                                     2,
                                                     16,
                                                     QLatin1Char('0'))
                                                .toUpper();
        emit logMessage(QStringLiteral("写入SN收到异常应答（异常码：%1），以SN回读结果为准")
                            .arg(exceptionCode));
    }

    if (!writeAccepted) {
        return;
    }

    m_writeTimer->stop();
    if (writingSn) {
        beginQingjuSnVerification();
    } else if (writingHardwareVersion) {
        beginQingjuHardwareVersionVerification();
    } else {
        finishPendingWrite(true, QString());
    }
}

void ProductionStationController::handleWriteRequested(quint16 dataId, const QByteArray &data)
{
    if (!m_deviceReady) {
        m_testService.handleWriteFinished(false, QStringLiteral("CAN设备未启动"));
        return;
    }

    m_writePending = true;
    if (m_protocolMode == 0) {
        if (!m_diagnosticTransfer.startWriteNonVolatile(dataId, data)) {
            finishPendingWrite(false, QStringLiteral("0x2E写入通道忙或请求无效"));
        }
        return;
    }

    m_qingjuWriteRetryCount = 0;
    m_qingjuVerifySnPending = false;
    m_qingjuVerifyHardwarePending = false;
    m_qingjuWritePendingData = data;
    if (!performQingjuWrite(dataId, data)) {
        emit logMessage(
            QStringLiteral("写入寄存器0x%1首次发送失败，%2ms后自动重试")
                .arg(dataId, 4, 16, QLatin1Char('0'))
                .arg(QingjuImmediateSendRetryDelayMs)
                .toUpper());
        m_writeTimer->start(QingjuImmediateSendRetryDelayMs);
        return;
    }
    m_writeTimer->start(QingjuWriteTimeoutMs);
}

void ProductionStationController::handleScanControlRequested(bool enabled)
{
    if (!m_deviceReady) {
        return;
    }

    if (m_protocolMode == 0) {
        CanFrame sentFrame;
        const CanTransport::RfidControlSendResult result =
            m_canWorker->sendManualRfidControlFrame(m_channel, enabled, &sentFrame);
        if (result == CanTransport::RfidControlSendResult::Sent) {
            emit frameObserved(sentFrame);
        } else if (result == CanTransport::RfidControlSendResult::Failed) {
            emit logMessage(QStringLiteral("0x207控制帧发送失败"));
        }
        m_canWorker->setRfidControlPeriodicEnabled(true, m_channel, enabled);
        return;
    }

    if (enabled) {
        m_qingjuRfidService->startScan(100, 100, 1, false);
    } else {
        m_qingjuRfidService->stopScan();
    }
}

void ProductionStationController::handleWriteTimeout()
{
    if (!m_writePending || m_protocolMode != 1) {
        return;
    }
    if (m_qingjuVerifySnPending) {
        finishPendingWrite(false, QStringLiteral("读取SN回读校验超时"));
        return;
    }
    if (m_qingjuVerifyHardwarePending) {
        finishPendingWrite(false, QStringLiteral("读取硬件版本回读校验超时"));
        return;
    }
    if (m_qingjuWriteRetryCount < QingjuMaxWriteRetries) {
        ++m_qingjuWriteRetryCount;
        emit logMessage(QStringLiteral("写入寄存器0x%1未完成，重试%2/%3")
                            .arg(m_qingjuWritePendingRegister, 4, 16, QLatin1Char('0'))
                            .arg(m_qingjuWriteRetryCount)
                            .arg(QingjuMaxWriteRetries)
                            .toUpper());
        if (performQingjuWrite(m_qingjuWritePendingRegister, m_qingjuWritePendingData)) {
            m_writeTimer->start(QingjuWriteTimeoutMs);
            return;
        }
        emit logMessage(QStringLiteral("写入寄存器0x%1第%2次重试发送失败")
                            .arg(m_qingjuWritePendingRegister, 4, 16, QLatin1Char('0'))
                            .arg(m_qingjuWriteRetryCount)
                            .toUpper());
        if (m_qingjuWriteRetryCount < QingjuMaxWriteRetries) {
            m_writeTimer->start(QingjuImmediateSendRetryDelayMs);
            return;
        }
        if (m_qingjuWritePendingRegister == QingjuSnRegister) {
            emit logMessage(QStringLiteral("SN写指令连续发送失败，尝试回读确认终端实际SN"));
            beginQingjuSnVerification();
            return;
        }
        finishPendingWrite(false, QStringLiteral("CAN连续3次发送写指令失败"));
        return;
    }
    if (m_qingjuWritePendingRegister == QingjuSnRegister) {
        emit logMessage(QStringLiteral("SN写应答超时，尝试回读确认终端实际SN"));
        beginQingjuSnVerification();
        return;
    }
    finishPendingWrite(false, QStringLiteral("写入设备超时"));
}

void ProductionStationController::finishPendingWrite(bool success, const QString &message)
{
    if (!m_writePending) {
        return;
    }
    m_writeTimer->stop();
    m_writePending = false;
    m_qingjuVerifySnPending = false;
    m_qingjuVerifyHardwarePending = false;
    m_testService.handleWriteFinished(success, message);
}

void ProductionStationController::beginQingjuSnVerification()
{
    m_qingjuVerifySnPending = true;
    emit logMessage(QStringLiteral("下发读SN指令进行最终写入校验"));
    QTimer::singleShot(QingjuReadBackDelayMs, this, [this]() {
        if (!m_writePending || !m_qingjuVerifySnPending) {
            return;
        }
        if (!m_qingjuCanManager->readRegisters(
                QingjuProductionAddress, QingjuSnRegister, QingjuSnRegisterCount)) {
            finishPendingWrite(false, QStringLiteral("SN回读请求发送失败"));
            return;
        }
        m_writeTimer->start(QingjuReadBackTimeoutMs);
    });
}

void ProductionStationController::beginQingjuHardwareVersionVerification()
{
    m_qingjuVerifyHardwarePending = true;
    emit logMessage(QStringLiteral("下发读取0xA004指令进行硬件版本写入校验"));
    QTimer::singleShot(QingjuReadBackDelayMs, this, [this]() {
        if (!m_writePending || !m_qingjuVerifyHardwarePending) {
            return;
        }
        if (!m_qingjuCanManager->readRegisters(
                QingjuProductionAddress,
                QingjuHardwareVersionRegister,
                QingjuHardwareVersionRegisterCount)) {
            finishPendingWrite(false, QStringLiteral("硬件版本回读请求发送失败"));
            return;
        }
        m_writeTimer->start(QingjuReadBackTimeoutMs);
    });
}

bool ProductionStationController::performQingjuWrite(quint16 dataId, const QByteArray &data)
{
    m_qingjuWritePendingRegister = dataId;
    m_qingjuWritePendingData = data;
    if (dataId == QingjuSnRegister) {
        m_qingjuWritePendingRegisterCount = QingjuSnRegisterCount;
        QByteArray paddedData = qingjuProductionSnPayload(data);
        paddedData = paddedData.leftJustified(QingjuSnRegisterCount * 2, ' ');
        QVector<quint16> registers(QingjuSnRegisterCount);
        for (int index = 0; index < QingjuSnRegisterCount; ++index) {
            registers[index] =
                (static_cast<quint8>(paddedData.at(index * 2)) << 8) |
                static_cast<quint8>(paddedData.at(index * 2 + 1));
        }
        return m_qingjuCanManager->writeRegisters(
            QingjuProductionAddress, QingjuSnRegister, registers);
    }
    if (dataId == QingjuHardwareVersionRegister) {
        m_qingjuWritePendingRegisterCount = QingjuHardwareVersionRegisterCount;
        quint16 hardwareVersion = 0;
        if (data.size() >= 2) {
            hardwareVersion =
                (static_cast<quint8>(data.at(0)) << 8) |
                static_cast<quint8>(data.at(1));
        }
        QVector<quint16> registers;
        registers.append(hardwareVersion);
        return m_qingjuCanManager->writeRegisters(
            QingjuProductionAddress, QingjuHardwareVersionRegister, registers);
    }
    return false;
}

bool ProductionStationController::sendClassicFrame(quint32 canId, const QByteArray &payload)
{
    if (!m_deviceReady) {
        return false;
    }
    CanFrame sentFrame;
    if (!m_canWorker->sendClassicData(canId, m_channel, payload, &sentFrame)) {
        return false;
    }
    emit frameObserved(sentFrame);
    return true;
}

void ProductionStationController::resetProtocolState()
{
    m_rfidService.reset();
    if (m_qingjuRfidService != nullptr) {
        m_qingjuRfidService->reset();
    }
    m_writeTimer->stop();
    m_writePending = false;
    m_qingjuVerifySnPending = false;
    m_qingjuVerifyHardwarePending = false;
    m_qingjuWriteRetryCount = 0;
    m_qingjuWritePendingRegister = 0;
    m_qingjuWritePendingRegisterCount = 0;
    m_qingjuWritePendingData.clear();
}

void ProductionStationController::setDeviceStatus(const QString &status)
{
    m_deviceStatus = status;
    emit deviceStateChanged();
}
