#include "qingjurfidservice.h"
#include <QDateTime>
#include <QDebug>
#include <QStringList>

namespace {

bool isPrintableAscii(const QByteArray &data)
{
    for (char ch : data) {
        const quint8 value = static_cast<quint8>(ch);
        if (value == 0) {
            continue;
        }
        if (value < 0x20 || value > 0x7E) {
            return false;
        }
    }
    return true;
}

QString asciiOrEmpty(const QByteArray &data)
{
    if (!isPrintableAscii(data)) {
        return QString();
    }
    return QString::fromLatin1(data).trimmed();
}

QString asciiOrHex(const QByteArray &data)
{
    const QString asciiText = asciiOrEmpty(data);
    if (!asciiText.isEmpty()) {
        return asciiText;
    }
    return QString::fromLatin1(data.toHex().toUpper());
}

bool isAllZero(const QByteArray &data)
{
    for (char ch : data) {
        if (static_cast<quint8>(ch) != 0) {
            return false;
        }
    }
    return true;
}

QString hexText(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex().toUpper());
}

QString buildAssetDisplayText(const QByteArray &assetData)
{
    const QByteArray usedData = assetData.left(16);
    const QByteArray reservedData = assetData.mid(16, 16);
    const QString usedHex = hexText(usedData);
    const QString assetAscii = asciiOrEmpty(usedData);

    QStringList lines;
    if (assetAscii.isEmpty()) {
        lines << QStringLiteral("HEX: %1").arg(usedHex);
    } else {
        lines << QStringLiteral("ASCII: %1").arg(assetAscii);
        lines << QStringLiteral("HEX: %1").arg(usedHex);
    }

    if (!isAllZero(reservedData)) {
        lines << QStringLiteral("Reserved: %1").arg(hexText(reservedData));
    }
    return lines.join('\n');
}

}

QingjuRfidService::QingjuRfidService(QingjuCanManager *canManager, QObject *parent)
    : QObject(parent)
    , m_canManager(canManager)
    , m_isScanning(false)
    , m_autoWritePassword(false)
    , m_targetAddress(0x0A)
    , m_infoStep(0)
    , m_readMode(1)
    , m_deviceInfoTargetAddress(0x0A)
    , m_resumeScanAfterDeviceInfo(true)
    , m_deviceInfoOnlyMode(false)
    , m_ignoreStatusUntilMs(0)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(500);
    connect(m_pollTimer, &QTimer::timeout, this, &QingjuRfidService::onPollTimeout);

    m_deviceInfoTimer = new QTimer(this);
    m_deviceInfoTimer->setSingleShot(true);
    connect(m_deviceInfoTimer, &QTimer::timeout, this, &QingjuRfidService::onDeviceInfoTimerTimeout);

    connect(m_canManager, &QingjuCanManager::modbusPacketReceived, this, &QingjuRfidService::onModbusPacketReceived);
}

void QingjuRfidService::startScan(int intervalMs, int hostPollIntervalMs, int readMode)
{
    if (m_isScanning) return;
    
    m_isScanning = true;
    m_lastUid.clear();
    m_readMode = readMode;

    // 1. 仅 NPK (0x0A) 支持发送启动读卡指令（0xA900 = readMode, 0xA901 周期设置）
    if (m_targetAddress == 0x0A) {
        quint8 periodVal = static_cast<quint8>(qBound(100, intervalMs, 25500) / 100);
        quint16 configVal = 0x8000 | periodVal;
        QVector<quint16> startVals = { static_cast<quint16>(readMode), configVal };
        m_canManager->writeRegisters(0x0A, 0xA900, startVals);
    }

    // 保存轮询间隔
    m_pollTimer->setInterval(qBound(100, hostPollIntervalMs, 10000));

    // 2. 串行获取设备静态信息（0xA002, 0xA005, 0xA00D, 0xA015, 0xA016, 0xA020）
    queryDeviceInfo(m_targetAddress, true, false);
}

void QingjuRfidService::stopScan()
{
    if (!m_isScanning) return;

    m_isScanning = false;
    m_pollTimer->stop();
    if (m_deviceInfoTimer != nullptr) {
        m_deviceInfoTimer->stop();
    }
    m_infoStep = 0;

    // 仅 NPK (0x0A) 发送停止读卡指令
    if (m_targetAddress == 0x0A) {
        QVector<quint16> stopVals = { 0x0000, 0x0001 };
        m_canManager->writeRegisters(0x0A, 0xA900, stopVals);
    }
}

void QingjuRfidService::reset()
{
    stopScan();
    m_state = QingjuNpkState();
    m_lastUid.clear();
    emit stateUpdated(m_state);
}

void QingjuRfidService::triggerSingleQuery()
{
    if (!m_isScanning) return;

    if (m_targetAddress == 0x0A) {
        m_canManager->readRegisters(0x0A, 0xA904, 22);
    } else {
        m_canManager->readRegisters(m_targetAddress, 0xA02A, 1);
    }
}

void QingjuRfidService::startPollingOrSingleQuery()
{
    if (!m_isScanning) return;

    if (m_readMode == 1) {
        if (!m_pollTimer->isActive()) {
            m_pollTimer->start();
            onPollTimeout(); // 立即执行一次轮询
        }
    } else {
        triggerSingleQuery();
    }
}

void QingjuRfidService::onPollTimeout()
{
    if (!m_isScanning) return;

    if (m_targetAddress == 0x0A) {
        // NPK 高频轮询只读取读卡状态，避免 0xA02A 程序状态在读卡过程中跳变干扰观察。
        m_canManager->readRegisters(0x0A, 0xA904, 22);
    } else {
        // RFR 轮询：仅轮询程序状态 (0xA02A)
        m_canManager->readRegisters(m_targetAddress, 0xA02A, 1);
    }
}

void QingjuRfidService::onModbusPacketReceived(quint8 srcAddr, quint8 destAddr, quint8 funcCode, const QByteArray &payload)
{
    // 只处理发送给中控 (0x01) 的读响应 (0x03)。
    if (destAddr != 0x01 || funcCode != 0x03) {
        return;
    }

    if (payload.isEmpty()) {
        return;
    }

    quint8 byteCount = static_cast<quint8>(payload.at(0));
    QByteArray data = payload.mid(1);

    if (data.size() < byteCount) {
        return; // 数据不全
    }

    const bool deviceInfoResponse = (m_infoStep > 0 && srcAddr == m_deviceInfoTargetAddress);
    const bool targetStatusResponse = (srcAddr == m_targetAddress);
    if (!deviceInfoResponse && !targetStatusResponse) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // 处理周期轮询的响应包。只读设备信息期间忽略状态残留包，避免冲掉设备信息面板。
    if (targetStatusResponse && byteCount == 44) {
        if (m_deviceInfoOnlyMode || nowMs < m_ignoreStatusUntilMs) {
            return;
        }
        parseStatusData(data);
        return;
    }

    // 根据串行请求步骤匹配并解析对应的响应包
    bool stepHandled = false;
    if (deviceInfoResponse) {
        switch (m_infoStep) {
        case 1: // 期待读取版本 (0xA002 ~ 0xA004) -> 6 字节
            if (byteCount == 6) {
                parseVersionData(data);
                stepHandled = true;
            }
            break;
        case 2: // 期待读取制造厂信息 (0xA005) -> 16 字节
            if (byteCount == 16) {
                parseVendorData(data);
                stepHandled = true;
            }
            break;
        case 3: // 期待读取设备 SN (0xA00D) -> 支持 16 或 26 字节
            if (byteCount == 16 || byteCount == 26) {
                parseSnData(data);
                stepHandled = true;
            }
            break;
        case 4: // 期待读取型号编码 (0xA015) -> 2 字节
            if (byteCount == 2) {
                parseModelData(data);
                stepHandled = true;
            }
            break;
        case 5: // 期待读取固件标识串 (0xA016) -> 20 字节
            if (byteCount == 20) {
                parseFwStrData(data);
                stepHandled = true;
            }
            break;
        case 6: // 期待读取硬件标识串 (0xA020) -> 20 字节
            if (byteCount == 20) {
                parseHwStrData(data);
                stepHandled = true;
            }
            break;
        default:
            break;
        }
    }

    // 如果是由串行单次查询触发并成功处理的，立即步进到下一步
    if (stepHandled) {
        m_infoStep++;
        sendDeviceInfoRequest();
        if (m_infoStep > 0 && m_infoStep <= 6 && m_deviceInfoTimer != nullptr) {
            m_deviceInfoTimer->start(200); // 重新启动单次超时定时器
        }
        return;
    }

    // 处理其余未被步骤绑定的包 (比如定时器单独周期轮询的 0xA02A 程序状态)
    if (targetStatusResponse && byteCount == 2) {
        parseAppStatusData(data);
    }
}

void QingjuRfidService::parseStatusData(const QByteArray &data)
{
    if (data.size() < 44) return;
    m_state.statusSample = true;

    // 0xA904: 读取结果
    quint16 result = (static_cast<quint8>(data.at(0)) << 8) | static_cast<quint8>(data.at(1));
    m_state.result = result;

    switch (result) {
    case 0: m_state.statusText = "未进行检测"; break;
    case 1: m_state.statusText = "读取成功"; break;
    case 2: m_state.statusText = "未检测到标签"; break;
    case 3: m_state.statusText = "检测到标签，读取UID失败"; break;
    case 4: m_state.statusText = "指定模块被锁定(秘钥错误)"; break;
    case 5: m_state.statusText = "不支持该指令(标签读操作被禁止)"; break;
    case 6: m_state.statusText = "指定块不可用(区域损坏)"; break;
    case 7: m_state.statusText = "其他未知错误"; break;
    default: m_state.statusText = QString("未知结果 (%1)").arg(result); break;
    }

    // 0xA905 ~ 0xA908: 64位UID (8 字节)
    QByteArray uid = data.mid(2, 8);
    m_state.uid = uid;

    if (result == 1) {
        m_state.uidText = uid.toHex().toUpper();
        
        // 0xA909 ~ 0xA918: 32 字节加密数据
        QByteArray assetData = data.mid(10, 32);
        m_state.assetData = assetData;

        // 资产信息 ASCII 解析 (前16字节为产品型号(4字节)+供应商(2字节)+流水号(10字节))
        const QByteArray assetUsedData = assetData.left(16);
        m_state.assetModel = asciiOrHex(assetUsedData.left(4));
        m_state.assetSupplier = asciiOrHex(assetUsedData.mid(4, 2));
        m_state.assetSerial = asciiOrHex(assetUsedData.mid(6, 10));
        m_state.assetFullText = buildAssetDisplayText(assetData);

        // 触发一机一密密钥计算与下发
        calculatePassword(uid);
    } else {
        m_state.uidText.clear();
        m_state.assetModel.clear();
        m_state.assetSupplier.clear();
        m_state.assetSerial.clear();
        m_state.assetFullText.clear();
        m_state.password = 0;
        m_lastUid.clear();
    }

    // 0xA919: 读卡器告警
    quint16 alarm = (static_cast<quint8>(data.at(42)) << 8) | static_cast<quint8>(data.at(43));
    m_state.alarm = alarm;

    switch (alarm) {
    case 0: m_state.alarmText = "无异常"; break;
    case 1: m_state.alarmText = "芯片读ID错误"; break;
    case 2: m_state.alarmText = "晶振不稳定"; break;
    case 3: m_state.alarmText = "配置超时"; break;
    default: m_state.alarmText = QString("未知原因 (%1)").arg(alarm); break;
    }

    emit stateUpdated(m_state);
}

void QingjuRfidService::parseVersionData(const QByteArray &data)
{
    if (data.size() < 6) return;
    m_state.statusSample = false;

    // 0xA002 (4 字节, uint32) - 软件版本号
    quint8 swMajor = static_cast<quint8>(data.at(0));
    quint8 swMinor = static_cast<quint8>(data.at(1));
    quint8 swSmall = static_cast<quint8>(data.at(2));
    m_state.firmwareVer = QString("v%1.%2.%3").arg(swMajor).arg(swMinor).arg(swSmall);

    // 0xA004 (2 字节, uint16) - 硬件版本号
    quint8 hwMajor = static_cast<quint8>(data.at(4));
    quint8 hwMinor = static_cast<quint8>(data.at(5));
    m_state.hardwareVer = QString("v%1.%2").arg(hwMajor).arg(hwMinor);

    emit stateUpdated(m_state);
}

void QingjuRfidService::parseSnData(const QByteArray &data)
{
    m_state.statusSample = false;
    QString cleanSn;
    for (char c : data) {
        if (c != '\0' && c != ' ' && c != '\r' && c != '\n') {
            cleanSn.append(QChar::fromLatin1(c));
        }
    }
    m_state.devSn = cleanSn;
    emit stateUpdated(m_state);
}

void QingjuRfidService::parseAppStatusData(const QByteArray &data)
{
    if (data.size() < 2) return;
    m_state.statusSample = false;

    quint16 statusVal = (static_cast<quint8>(data.at(0)) << 8) | static_cast<quint8>(data.at(1));
    if (statusVal == 0) {
        m_state.appStatus = "boot";
    } else if (statusVal == 1 || statusVal == 2) {
        m_state.appStatus = "app";
    } else {
        m_state.appStatus = QString("未知 (%1)").arg(statusVal);
    }

    emit stateUpdated(m_state);
}

void QingjuRfidService::calculatePassword(const QByteArray &uid)
{
    if (uid.size() != 8) return;

    // 如果与上次的 UID 相同且已算过密码，则跳过（避免频繁写入）
    if (uid == m_lastUid && m_state.password != 0) {
        return;
    }

    m_lastUid = uid;

    quint8 uid7 = static_cast<quint8>(uid.at(0));
    quint8 uid6 = static_cast<quint8>(uid.at(1));
    quint8 uid5 = static_cast<quint8>(uid.at(2));
    quint8 uid4 = static_cast<quint8>(uid.at(3));
    quint8 uid3 = static_cast<quint8>(uid.at(4));
    quint8 uid2 = static_cast<quint8>(uid.at(5));
    quint8 uid1 = static_cast<quint8>(uid.at(6));
    quint8 uid0 = static_cast<quint8>(uid.at(7));

    quint8 pwd[4];
    pwd[0] = uid0 ^ uid4 ^ 0x44;
    pwd[1] = uid1 ^ uid5 ^ 0x64;
    pwd[2] = uid2 ^ uid6 ^ 0x54;
    pwd[3] = uid3 ^ uid7 ^ 0x67;

    quint16 pwd_high = (pwd[0] << 8) | pwd[1];
    quint16 pwd_low = (pwd[2] << 8) | pwd[3];

    m_state.password = (static_cast<quint32>(pwd_high) << 16) | pwd_low;

    // 自动下发一机一密解锁：写入 0xA902(高16位), 0xA903(低16位)
    if (m_autoWritePassword) {
        QVector<quint16> pwdVals = { pwd_high, pwd_low };
        m_canManager->writeRegisters(0x0A, 0xA902, pwdVals);
    }
}

void QingjuRfidService::setAutoWritePassword(bool enabled)
{
    m_autoWritePassword = enabled;
}

void QingjuRfidService::setTargetAddress(quint8 addr)
{
    if (m_targetAddress == addr) return;
    m_targetAddress = addr;
    m_lastUid.clear();
    
    // 重置相关状态数据
    QingjuNpkState newState;
    newState.password = m_state.password; // 保留密码缓存
    m_state = newState;
    emit stateUpdated(m_state);
}

void QingjuRfidService::queryDeviceInfo(bool resumeScanAfterInfo)
{
    queryDeviceInfo(m_targetAddress, resumeScanAfterInfo, false);
}

void QingjuRfidService::queryDeviceInfo(quint8 targetAddress, bool resumeScanAfterInfo)
{
    queryDeviceInfo(targetAddress, resumeScanAfterInfo, false);
}

void QingjuRfidService::queryDeviceInfo(quint8 targetAddress, bool resumeScanAfterInfo, bool deviceInfoOnly)
{
    if (m_pollTimer != nullptr && m_pollTimer->isActive()) {
        m_pollTimer->stop();
    }
    m_deviceInfoTargetAddress = targetAddress;
    m_resumeScanAfterDeviceInfo = resumeScanAfterInfo;
    m_deviceInfoOnlyMode = deviceInfoOnly;
    m_ignoreStatusUntilMs = deviceInfoOnly ? QDateTime::currentMSecsSinceEpoch() + 1500 : 0;
    m_infoStep = 1;
    sendDeviceInfoRequest();
    if (m_deviceInfoTimer != nullptr) {
        m_deviceInfoTimer->start(200); // 200ms 超时守护
    }
}

void QingjuRfidService::sendDeviceInfoRequest()
{
    switch (m_infoStep) {
    case 1:
        m_canManager->readRegisters(m_deviceInfoTargetAddress, 0xA002, 3);
        break;
    case 2:
        m_canManager->readRegisters(m_deviceInfoTargetAddress, 0xA005, 8);
        break;
    case 3:
        m_canManager->readRegisters(m_deviceInfoTargetAddress, 0xA00D, 13);
        break;
    case 4:
        m_canManager->readRegisters(m_deviceInfoTargetAddress, 0xA015, 1);
        break;
    case 5:
        m_canManager->readRegisters(m_deviceInfoTargetAddress, 0xA016, 10);
        break;
    case 6:
        m_canManager->readRegisters(m_deviceInfoTargetAddress, 0xA020, 10);
        break;
    default:
        if (m_deviceInfoTimer != nullptr) m_deviceInfoTimer->stop();
        m_infoStep = 0;
        if (m_deviceInfoOnlyMode) {
            m_ignoreStatusUntilMs = QDateTime::currentMSecsSinceEpoch() + 800;
            m_deviceInfoOnlyMode = false;
        }
        if (m_isScanning && m_resumeScanAfterDeviceInfo) {
            startPollingOrSingleQuery();
        }
        break;
    }
}

void QingjuRfidService::onDeviceInfoTimerTimeout()
{
    m_infoStep++;
    sendDeviceInfoRequest();
    if (m_infoStep > 0 && m_infoStep <= 6) {
        m_deviceInfoTimer->start(200); // 继续启动下一个 200ms 的超时定时器
    }
}

void QingjuRfidService::parseVendorData(const QByteArray &data)
{
    if (data.size() < 16) return;
    m_state.vendorInfo = QString::fromLatin1(data.left(16)).trimmed();
    emit stateUpdated(m_state);
}

void QingjuRfidService::parseModelData(const QByteArray &data)
{
    if (data.size() < 2) return;
    quint8 hwCode = static_cast<quint8>(data.at(0));
    quint8 custCode = static_cast<quint8>(data.at(1));
    m_state.modelCodeText = QString("HW:0x%1 / Cust:0x%2")
                            .arg(QString("%1").arg(hwCode, 2, 16, QChar('0')).toUpper())
                            .arg(QString("%1").arg(custCode, 2, 16, QChar('0')).toUpper());
    emit stateUpdated(m_state);
}

void QingjuRfidService::parseFwStrData(const QByteArray &data)
{
    if (data.size() < 20) return;
    m_state.fwVersionStr = QString::fromLatin1(data.left(20)).trimmed();
    emit stateUpdated(m_state);
}

void QingjuRfidService::parseHwStrData(const QByteArray &data)
{
    if (data.size() < 20) return;
    m_state.hwVersionStr = QString::fromLatin1(data.left(20)).trimmed();
    emit stateUpdated(m_state);
}
