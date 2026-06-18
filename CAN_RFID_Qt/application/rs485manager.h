#ifndef RS485MANAGER_H
#define RS485MANAGER_H

#include <QObject>
#include <QSerialPort>
#include <QByteArray>
#include <QStringList>

class Rs485Manager : public QObject
{
    Q_OBJECT
public:
    explicit Rs485Manager(QObject *parent = nullptr);
    ~Rs485Manager();

    bool openPort(const QString &portName, int baudRate);
    void closePort();
    bool isOpen() const;

    bool sendRawData(const QByteArray &data);
    void setProtocolMode(int mode); // 2: BB, 3: FF

    static QStringList scanPorts();

signals:
    // Emitted when a valid packet is sliced and verified
    void packetReceived(quint8 cmdCode, const QByteArray &payload);
    // Emitted for logging in the main window
    void frameReceived(const QByteArray &data, const QString &decodeText);
    void frameSent(const QByteArray &data, const QString &decodeText);
    // Emitted when the serial port is disconnected due to critical errors
    void portDisconnected();

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

private:
    void processBbBuffer();
    void processFfBuffer();
    
    // Decodes frame into a human-readable string for logging
    QString decodeFrameText(bool isTx, quint8 cmdCode, const QByteArray &payload) const;

    QSerialPort *m_serialPort;
    QByteArray m_rxBuffer;
    int m_protocolMode; // 2: BB, 3: FF
};

#endif // RS485MANAGER_H
