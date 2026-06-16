#include "qingjurfidservice.h"
#include <QDebug>

QingjuRfidService::QingjuRfidService(QingjuCanManager *canManager, QObject *parent)
    : QObject(parent)
    , m_canManager(canManager)
    , m_isScanning(false)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(500);
    connect(m_pollTimer, &QTimer::timeout, this, &QingjuRfidService::onPollTimeout);

    connect(m_canManager, &QingjuCanManager::modbusPacketReceived, this, &QingjuRfidService::onModbusPacketReceived);
}

void QingjuRfidService::startScan(int intervalMs)
{
    if (m_isScanning) return;
    
    m_isScanning = true;
    m_lastUid.clear();

    // 1. 发送启动指令：写寄存器 0xA900 = 1, 0xA901 = 0x8000 | (intervalMs / 100) (高字节使能 bit7=1，低字节为周期，单位100ms)
    quint8 periodVal = static_cast<quint8>(qBound(100, intervalMs, 25500) / 100);
    quint16 configVal = 0x8000 | periodVal;
    QVector<quint16> startVals = { 1, configVal };
    m_canManager->writeRegisters(0x0A, 0xA900, startVals);

    // 2. 发送读取版本和硬件版本指令（从 0xA002 读取 3 个寄存器：0xA002, 0xA003, 0xA004）
    m_canManager->readRegisters(0x0A, 0xA002, 3);

    // 3. 发送读取 SN 指令（从 0xA00D 读取 8 个寄存器）
    m_canManager->readRegisters(0x0A, 0xA00D, 8);

    // 4. 开启 500ms 周期轮询
    m_pollTimer->start();
    onPollTimeout(); // 立即执行一次轮询
}

void QingjuRfidService::stopScan()
{
    if (!m_isScanning) return;

    m_isScanning = false;
    m_pollTimer->stop();

    // 发送停止指令：写寄存器 0xA900 = 1, 0xA901 = 0x0000 (高字节 bit7=0 关闭)
    QVector<quint16> stopVals = { 1, 0x0000 };
    m_canManager->writeRegisters(0x0A, 0xA900, stopVals);
}

void QingjuRfidService::reset()
{
    stopScan();
    m_state = QingjuNpkState();
    m_lastUid.clear();
    emit stateUpdated(m_state);
}

void QingjuRfidService::onPollTimeout()
{
    if (!m_isScanning) return;

    // 周期查询：NPK 状态寄存器 (0xA904 至 0xA919，共 22 个寄存器)
    m_canManager->readRegisters(0x0A, 0xA904, 22);

    // 周期查询：从机程序状态 (0xA02A，共 1 个寄存器)
    m_canManager->readRegisters(0x0A, 0xA02A, 1);
}

void QingjuRfidService::onModbusPacketReceived(quint8 srcAddr, quint8 destAddr, quint8 funcCode, const QByteArray &payload)
{
    // 只处理来自 NPK 设备 (0x0A) 且发送给中控 (0x01) 的读响应 (0x03)
    if (srcAddr != 0x0A || destAddr != 0x01 || funcCode != 0x03) {
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

    switch (byteCount) {
    case 44: // NPK 状态数据 (0xA904 ~ 0xA919)
        parseStatusData(data);
        break;
    case 16: // 设备 SN (0xA00D)
        parseSnData(data);
        break;
    case 6:  // 版本信息 (0xA002 ~ 0xA004)
        parseVersionData(data);
        break;
    case 2:  // 程序状态 (0xA02A)
        parseAppStatusData(data);
        break;
    default:
        break;
    }
}

void QingjuRfidService::parseStatusData(const QByteArray &data)
{
    if (data.size() < 44) return;

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

    // 检查 UID 是否全 0 或空 (即是否有卡)
    bool hasCard = false;
    for (int i = 0; i < 8; ++i) {
        if (static_cast<quint8>(uid.at(i)) != 0) {
            hasCard = true;
            break;
        }
    }

    if (hasCard) {
        m_state.uidText = uid.toHex().toUpper();
        
        // 0xA909 ~ 0xA918: 32 字节加密数据
        QByteArray assetData = data.mid(10, 32);
        m_state.assetData = assetData;

        // 资产信息 ASCII 解析 (前16字节为产品型号(4字节)+供应商(2字节)+流水号(10字节))
        m_state.assetModel = QString::fromLatin1(assetData.left(4)).trimmed();
        m_state.assetSupplier = QString::fromLatin1(assetData.mid(4, 2)).trimmed();
        m_state.assetSerial = QString::fromLatin1(assetData.mid(6, 10)).trimmed();

        // 触发一机一密密钥计算与下发
        calculatePassword(uid);
    } else {
        m_state.uidText.clear();
        m_state.assetModel.clear();
        m_state.assetSupplier.clear();
        m_state.assetSerial.clear();
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
    if (data.size() < 16) return;

    m_state.devSn = QString::fromLatin1(data.left(16)).trimmed();
    emit stateUpdated(m_state);
}

void QingjuRfidService::parseAppStatusData(const QByteArray &data)
{
    if (data.size() < 2) return;

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
    QVector<quint16> pwdVals = { pwd_high, pwd_low };
    m_canManager->writeRegisters(0x0A, 0xA902, pwdVals);
}
