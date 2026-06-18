#include "rs485manager.h"
#include <QSerialPortInfo>
#include <QDebug>

static quint16 calculateCrc16Xmodem(const char *data, int len)
{
    quint16 wCRCin = 0x0000;
    quint16 wCPoly = 0x1021;
    for (int j = 0; j < len; ++j) {
        quint8 wChar = static_cast<quint8>(data[j]);
        wCRCin ^= (static_cast<quint16>(wChar) << 8);
        for (int i = 0; i < 8; i++) {
            if (wCRCin & 0x8000) {
                wCRCin = (wCRCin << 1) ^ wCPoly;
            } else {
                wCRCin = wCRCin << 1;
            }
        }
    }
    return wCRCin;
}

Rs485Manager::Rs485Manager(QObject *parent)
    : QObject(parent)
    , m_serialPort(nullptr)
    , m_protocolMode(2) // Default BB
{
    m_serialPort = new QSerialPort(this);
    connect(m_serialPort, &QSerialPort::readyRead, this, &Rs485Manager::onReadyRead);
    connect(m_serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
            this, &Rs485Manager::onErrorOccurred);
}

Rs485Manager::~Rs485Manager()
{
    closePort();
}

bool Rs485Manager::openPort(const QString &portName, int baudRate)
{
    closePort();
    m_rxBuffer.clear();
    
    m_serialPort->setPortName(portName);
    m_serialPort->setBaudRate(baudRate);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

    if (m_serialPort->open(QIODevice::ReadWrite)) {
        return true;
    }
    return false;
}

void Rs485Manager::closePort()
{
    if (m_serialPort && m_serialPort->isOpen()) {
        m_serialPort->close();
    }
}

bool Rs485Manager::isOpen() const
{
    return m_serialPort && m_serialPort->isOpen();
}

bool Rs485Manager::sendRawData(const QByteArray &data)
{
    if (!isOpen()) return false;
    
    qint64 bytesWritten = m_serialPort->write(data);
    m_serialPort->flush();
    
    if (bytesWritten == data.size()) {
        // Parse the code and payload for logging decode
        quint8 cmdCode = 0;
        QByteArray payload;
        if (m_protocolMode == 2 && data.size() >= 5) {
            cmdCode = static_cast<quint8>(data.at(2));
            int len = (static_cast<quint8>(data.at(3)) << 8) | static_cast<quint8>(data.at(4));
            if (data.size() >= len + 7) {
                payload = data.mid(5, len);
            }
        } else if (m_protocolMode == 3 && data.size() >= 4) {
            cmdCode = static_cast<quint8>(data.at(2));
            int len = static_cast<quint8>(data.at(3));
            if (data.size() >= len + 6) {
                payload = data.mid(4, len);
            }
        }
        
        QString decodeText = decodeFrameText(true, cmdCode, payload);
        emit frameSent(data, decodeText);
        return true;
    }
    return false;
}

void Rs485Manager::setProtocolMode(int mode)
{
    m_protocolMode = mode;
    m_rxBuffer.clear();
}

QStringList Rs485Manager::scanPorts()
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        ports << info.portName();
    }
    return ports;
}

void Rs485Manager::onReadyRead()
{
    m_rxBuffer.append(m_serialPort->readAll());
    
    if (m_protocolMode == 2) {
        processBbBuffer();
    } else {
        processFfBuffer();
    }
}

void Rs485Manager::processBbBuffer()
{
    while (true) {
        int index = m_rxBuffer.indexOf(static_cast<char>(0xBB));
        if (index == -1) {
            m_rxBuffer.clear();
            break;
        }
        if (index > 0) {
            m_rxBuffer.remove(0, index);
        }
        
        if (m_rxBuffer.size() < 5) {
            break; // Need more bytes to parse length
        }
        
        int len = (static_cast<quint8>(m_rxBuffer.at(3)) << 8) | static_cast<quint8>(m_rxBuffer.at(4));
        int totalLen = len + 7;
        
        if (m_rxBuffer.size() < totalLen) {
            break; // Wait for the whole packet
        }
        
        QByteArray frame = m_rxBuffer.left(totalLen);
        
        // Verify tail
        if (static_cast<quint8>(frame.at(totalLen - 1)) != 0x7E) {
            // Invalid tail, discard this header and try again
            m_rxBuffer.remove(0, 1);
            continue;
        }
        
        // Verify checksum: sum of bytes from Frame Type (index 1) to last param (index totalLen - 3)
        quint8 sum = 0;
        for (int i = 1; i <= totalLen - 3; ++i) {
            sum += static_cast<quint8>(frame.at(i));
        }
        
        quint8 receivedSum = static_cast<quint8>(frame.at(totalLen - 2));
        if (sum != receivedSum) {
            // Checksum error, discard this header and try again
            m_rxBuffer.remove(0, 1);
            continue;
        }
        
        // Valid BB frame! Extract fields
        // quint8 frameType = frame.at(1);
        quint8 cmdCode = frame.at(2);
        QByteArray payload = frame.mid(5, len);
        
        // Log & Emit
        QString decodeText = decodeFrameText(false, cmdCode, payload);
        emit frameReceived(frame, decodeText);
        emit packetReceived(cmdCode, payload);
        
        m_rxBuffer.remove(0, totalLen);
    }
}

void Rs485Manager::processFfBuffer()
{
    while (true) {
        int index = m_rxBuffer.indexOf(static_cast<char>(0xFF));
        if (index == -1) {
            m_rxBuffer.clear();
            break;
        }
        if (index > 0) {
            m_rxBuffer.remove(0, index);
        }
        
        if (m_rxBuffer.size() < 4) {
            break; // Need more bytes to parse length
        }
        
        int len = static_cast<quint8>(m_rxBuffer.at(3));
        int totalLen = len + 6;
        
        if (m_rxBuffer.size() < totalLen) {
            break; // Wait for the whole packet
        }
        
        QByteArray frame = m_rxBuffer.left(totalLen);
        
        // Verify Address (must be 0x02, or any valid reply address)
        // Wait, for flexibility we don't strictly require address 0x02 but we do verify CRC
        
        // Verify CRC16/XMODEM
        quint16 receivedCrc = (static_cast<quint8>(frame.at(totalLen - 2)) << 8) | 
                              static_cast<quint8>(frame.at(totalLen - 1));
        quint16 calculatedCrc = calculateCrc16Xmodem(frame.constData(), totalLen - 2);
        
        if (receivedCrc != calculatedCrc) {
            // CRC error, discard header byte
            m_rxBuffer.remove(0, 1);
            continue;
        }
        
        // Valid FF frame! Extract fields
        quint8 cmdCode = frame.at(2);
        QByteArray payload = frame.mid(4, len);
        
        // Log & Emit
        QString decodeText = decodeFrameText(false, cmdCode, payload);
        emit frameReceived(frame, decodeText);
        emit packetReceived(cmdCode, payload);
        
        m_rxBuffer.remove(0, totalLen);
    }
}

void Rs485Manager::onErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError) {
        qWarning() << "Serial port critical error:" << error;
        closePort();
        emit portDisconnected();
    }
}

QString Rs485Manager::decodeFrameText(bool isTx, quint8 cmdCode, const QByteArray &payload) const
{
    if (m_protocolMode == 2) { // BB
        switch (cmdCode) {
        case 0x03:
            return isTx ? QStringLiteral("查询版本号") : QStringLiteral("查询版本号应答");
        case 0x11:
            return isTx ? QStringLiteral("设置波特率") : QStringLiteral("设置波特率应答");
        case 0x15:
            return isTx ? QStringLiteral("查询设备ID") : QStringLiteral("查询设备ID应答");
        case 0x22:
            return isTx ? QStringLiteral("查询标签") : QStringLiteral("查询标签上报");
        case 0xB6:
            return isTx ? QStringLiteral("设置发射功率") : QStringLiteral("设置发射功率应答");
        case 0xB7:
            return isTx ? QStringLiteral("查询发射功率") : QStringLiteral("查询发射功率应答");
        case 0xFF:
            return QStringLiteral("读卡器错误提示");
        default:
            return QString("BB 0x%1").arg(cmdCode, 2, 16, QChar('0')).toUpper();
        }
    } else { // FF
        switch (cmdCode) {
        case 0x00:
            return QStringLiteral("查询标签");
        case 0x01:
            return QStringLiteral("查询标签应答");
        case 0x02:
            return QStringLiteral("重启设备");
        case 0x03:
            return QStringLiteral("重启设备应答");
        case 0x04:
            return QStringLiteral("查询版本");
        case 0x05:
            return QStringLiteral("查询版本应答");
        case 0x06:
            return QStringLiteral("开始检测");
        case 0x07:
            return QStringLiteral("开始检测应答");
        case 0x08:
            return QStringLiteral("停止检测");
        case 0x09:
            return QStringLiteral("停止检测应答");
        case 0x0A:
            return QStringLiteral("设置波特率");
        case 0x0B:
            return QStringLiteral("波特率非法值应答");
        case 0x0C:
            return QStringLiteral("查询设备ID");
        case 0x0D:
            return QStringLiteral("查询设备ID应答");
        case 0x0E:
            return QStringLiteral("设置发射功率");
        case 0x0F:
            return QStringLiteral("设置发射功率应答");
        case 0x10:
            return QStringLiteral("查询发射功率");
        case 0x11:
            return QStringLiteral("查询发射功率应答");
        case 0x12:
            return QStringLiteral("设置接收参数");
        case 0x13:
            return QStringLiteral("设置接收参数应答");
        case 0x14:
            return QStringLiteral("查询接收参数");
        case 0x15:
            return QStringLiteral("查询接收参数应答");
        case 0x16:
            return QStringLiteral("查询读卡开关");
        case 0x17:
            return QStringLiteral("查询读卡开关应答");
        default:
            return QString("FF 0x%1").arg(cmdCode, 2, 16, QChar('0')).toUpper();
        }
    }
}
