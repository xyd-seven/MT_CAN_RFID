#include "rs485worker.h"

Rs485Worker::Rs485Worker(QObject *parent)
    : QObject(parent)
    , m_manager(nullptr)
    , m_rfidService(nullptr)
    , m_hlOtaService(nullptr)
    , m_bbFfOtaService(nullptr)
    , m_protocolMode(2)
{
}

Rs485Worker::~Rs485Worker()
{
    shutdown();
}

void Rs485Worker::shutdown()
{
    if (m_hlOtaService != nullptr) {
        m_hlOtaService->abortUpgrade();
    }
    if (m_bbFfOtaService != nullptr) {
        m_bbFfOtaService->abortUpgrade();
    }
    if (m_rfidService != nullptr) {
        m_rfidService->stopScan();
    }
    if (m_manager != nullptr) {
        m_manager->closePort();
    }
}

void Rs485Worker::initialize()
{
    ensureInitialized();
}

void Rs485Worker::ensureInitialized()
{
    if (m_manager != nullptr) {
        return;
    }

    m_manager = new Rs485Manager(this);
    m_rfidService = new Rs485RfidService(m_manager, this);
    m_hlOtaService = new HlOtaService(m_manager, this);
    m_bbFfOtaService = new BbFfOtaService(m_manager, this);
    m_manager->setProtocolMode(m_protocolMode);
    m_rfidService->setProtocolMode(m_protocolMode);

    connect(m_manager, &Rs485Manager::frameReceived, this, &Rs485Worker::frameReceived);
    connect(m_manager, &Rs485Manager::frameSent, this, &Rs485Worker::frameSent);
    connect(m_manager, &Rs485Manager::portDisconnected, this, [this]() {
        if (m_rfidService != nullptr) {
            m_rfidService->stopScan();
        }
        emit scanStateChanged(false);
        emit portDisconnected();
    });
    connect(m_rfidService, &Rs485RfidService::stateUpdated, this, &Rs485Worker::stateUpdated);
    connect(m_rfidService, &Rs485RfidService::pollSkipped, this, &Rs485Worker::pollSkipped);
    connect(m_rfidService, &Rs485RfidService::commandFinished, this, &Rs485Worker::commandFinished);
    connect(m_hlOtaService, &HlOtaService::otaStateChanged, this, &Rs485Worker::hlOtaStateChanged);
    connect(m_hlOtaService, &HlOtaService::otaProgress, this, &Rs485Worker::hlOtaProgress);
    connect(m_bbFfOtaService, &BbFfOtaService::otaStateChanged, this, &Rs485Worker::bbFfOtaStateChanged);
    connect(m_bbFfOtaService, &BbFfOtaService::otaProgress, this, &Rs485Worker::bbFfOtaProgress);
}

void Rs485Worker::openPort(const QString &portName, int baudRate)
{
    ensureInitialized();
    m_rfidService->stopScan();
    emit scanStateChanged(false);
    const bool success = m_manager->openPort(portName, baudRate);
    emit portOpened(success, success ? QString() : QStringLiteral("open failed"));
}

void Rs485Worker::closePort()
{
    ensureInitialized();
    m_rfidService->stopScan();
    m_manager->closePort();
    emit scanStateChanged(false);
    emit portClosed();
}

void Rs485Worker::setProtocolMode(int mode)
{
    m_protocolMode = mode;
    ensureInitialized();
    m_rfidService->stopScan();
    m_rfidService->setProtocolMode(mode);
    m_manager->setProtocolMode(mode);
    emit scanStateChanged(false);
}

void Rs485Worker::startScan(int hostPollIntervalMs, int readMode)
{
    ensureInitialized();
    m_rfidService->startScan(hostPollIntervalMs, readMode);
    emit scanStateChanged(m_rfidService->isScanning());
}

void Rs485Worker::stopScan()
{
    ensureInitialized();
    m_rfidService->stopScan();
    emit scanStateChanged(false);
}

void Rs485Worker::queryDeviceInfo()
{
    ensureInitialized();
    m_rfidService->queryDeviceInfo();
}

void Rs485Worker::triggerSingleQuery()
{
    ensureInitialized();
    m_rfidService->triggerSingleQuery();
}

void Rs485Worker::setPower(int powerRaw01Dbm)
{
    ensureInitialized();
    m_rfidService->setPower(powerRaw01Dbm);
}

void Rs485Worker::queryPower()
{
    ensureInitialized();
    m_rfidService->queryPower();
}

void Rs485Worker::ffReboot()
{
    ensureInitialized();
    m_rfidService->ffReboot();
}

void Rs485Worker::ffSetDemodulatorParams(int mixer, int ifAmp, int thrd)
{
    ensureInitialized();
    m_rfidService->ffSetDemodulatorParams(mixer, ifAmp, thrd);
}

void Rs485Worker::ffQueryDemodulatorParams()
{
    ensureInitialized();
    m_rfidService->ffQueryDemodulatorParams();
}

void Rs485Worker::ffQueryCardSwitch()
{
    ensureInitialized();
    m_rfidService->ffQueryCardSwitch();
}

void Rs485Worker::setHlConfig(quint32 timeMs, int intervalMs, int savedCount, int clearAfter, int decrypt)
{
    ensureInitialized();
    m_rfidService->setHlConfig(timeMs, intervalMs, savedCount, clearAfter, decrypt);
}

void Rs485Worker::hlWriteScanControl(int startStop, quint32 timeMs, int intervalMs, int savedCount, int clearAfter, int decrypt)
{
    ensureInitialized();
    m_rfidService->hlWriteScanControl(startStop, timeMs, intervalMs, savedCount, clearAfter, decrypt);
}

void Rs485Worker::hlRebootDevice()
{
    ensureInitialized();
    m_rfidService->hlRebootDevice();
}

void Rs485Worker::sendRawData(const QByteArray &data)
{
    ensureInitialized();
    const bool success = (m_manager != nullptr && m_manager->sendRawData(data));
    emit rawDataSent(success, success ? QString() : QStringLiteral("send failed"));
}

void Rs485Worker::hlOtaQueryProgramStatus()
{
    ensureInitialized();
    m_hlOtaService->queryProgramStatus();
}

void Rs485Worker::hlOtaStartUpgrade(const QString &firmwarePath)
{
    ensureInitialized();
    m_hlOtaService->startUpgrade(firmwarePath);
}

void Rs485Worker::hlOtaAbortUpgrade()
{
    ensureInitialized();
    m_hlOtaService->abortUpgrade();
}

void Rs485Worker::bbFfOtaStartUpgrade(const QString &firmwarePath, const QString &versionStr)
{
    ensureInitialized();
    m_bbFfOtaService->startUpgrade(firmwarePath, versionStr);
}

void Rs485Worker::bbFfOtaAbortUpgrade()
{
    ensureInitialized();
    m_bbFfOtaService->abortUpgrade();
}
