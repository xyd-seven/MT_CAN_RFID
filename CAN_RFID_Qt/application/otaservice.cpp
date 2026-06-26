#include "otaservice.h"
#include "domain/crc16.h"
#include <QDebug>

OtaWorker::OtaWorker(QObject *parent) :
    QThread(parent),
    vendorCode(0x02), // UHF
    hwVersion(0),
    swVersion(0),
    protocolVersion(0x02), // OTA protocol version
    hasResponse(false),
    abortRequested(false),
    queryOnlyMode(false)
{
}

OtaWorker::~OtaWorker()
{
    requestAbort();
    wait();
}

void OtaWorker::setup(const QString &filePath, const IsoTpConfig &cfg, quint8 vendor, quint16 hw, quint16 sw, quint8 proto, const OtaErrorConfig &injectCfg, bool queryOnly)
{
    firmwarePath = filePath;
    config = cfg;
    vendorCode = vendor;
    hwVersion = hw;
    swVersion = sw;
    protocolVersion = proto;
    transport.setConfig(cfg);
    abortRequested.store(false);
    queryOnlyMode = queryOnly;
    lastError.clear();
    injectConfig = injectCfg;
}

void OtaWorker::requestAbort()
{
    abortRequested.store(true);
    QMutexLocker locker(&mutex);
    waitCondition.wakeAll();
}

void OtaWorker::handleIncomingFrame(const CanFrame &frame)
{
    QMutexLocker locker(&mutex);
    responseFrame = frame;
    hasResponse = true;
    waitCondition.wakeAll();
}

void OtaWorker::updateStatus(int stateVal, const QString &msg, int progressPercent)
{
    emit statusUpdated(stateVal, msg, progressPercent);
}

bool OtaWorker::waitForFrame(quint32 expectedId, quint8 firstByteMask, quint8 expectedFirstByte, int timeoutMs, CanFrame &matchedFrame)
{
    QElapsedTimer timer;
    timer.start();

    QMutexLocker locker(&mutex);
    while (true) {
        int remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            return false;
        }

        if (!hasResponse) {
            if (!waitCondition.wait(&mutex, remaining)) {
                return false; // Timeout
            }
        }

        if (abortRequested.load()) {
            return false;
        }

        if (hasResponse) {
            if (responseFrame.id == expectedId && responseFrame.channel == config.channel && !responseFrame.data.isEmpty()) {
                quint8 byte0 = static_cast<quint8>(responseFrame.data[0]);
                if ((byte0 & firstByteMask) == expectedFirstByte) {
                    matchedFrame = responseFrame;
                    return true;
                }
            }
            hasResponse = false; // Not a match, discard and keep waiting
        }
    }
}

bool OtaWorker::waitForFlowControl(int timeoutMs, IsoTpFlowControl &fc)
{
    CanFrame frame;
    if (!waitForFrame(config.responseId, 0xF0, 0x30, timeoutMs, frame)) {
        return false;
    }
    return IsoTpTransport::parseFlowControl(frame.data, fc);
}

bool OtaWorker::waitForResponse(quint8 expectedSid, int timeoutMs, QByteArray &payload)
{
    QElapsedTimer timer;
    timer.start();

    QMutexLocker locker(&mutex);
    while (true) {
        int remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            lastError = "Response wait timeout";
            return false;
        }

        if (!hasResponse) {
            if (!waitCondition.wait(&mutex, remaining)) {
                lastError = "Wait condition timeout";
                return false;
            }
        }

        if (abortRequested.load()) {
            lastError = "Abort requested";
            return false;
        }

        if (hasResponse) {
            if (responseFrame.id == config.responseId && responseFrame.channel == config.channel && responseFrame.data.size() >= 2) {
                quint8 byte0 = static_cast<quint8>(responseFrame.data[0]);
                quint8 byte1 = static_cast<quint8>(responseFrame.data[1]);

                // ISO-TP 鍗曞抚 (SF) 鏍￠獙锛氶珮 4 浣嶄负 0
                if ((byte0 & 0xF0) == 0x00) {
                    if (byte1 == expectedSid) {
                        int len = byte0;
                        if (len > 0 && len <= 7 && responseFrame.data.size() >= len + 1) {
                            payload = responseFrame.data.mid(1, len);
                            return true;
                        }
                    }
                    // 璐熷搷搴旀牎楠?(0x7F)
                    else if (byte1 == 0x7F && responseFrame.data.size() >= 4) {
                        quint8 originalSid = static_cast<quint8>(responseFrame.data[2]);
                        quint8 nrc = static_cast<quint8>(responseFrame.data[3]);
                        if (originalSid == (expectedSid - 0x40)) { // 姣斿姝ｅ搷搴斾负 0xE1 (A1+0x40)锛屽搴斿師璇锋眰涓?0xA1
                            lastError = QString("Negative response: NRC=0x%1").arg(nrc, 2, 16, QChar('0'));
                            return false;
                        }
                    }
                }
            }
            hasResponse = false; // Not a match, discard and keep waiting
        }
    }
}

bool OtaWorker::sendSingleFrame(quint8 sid, const QByteArray &params)
{
    QByteArray payload;
    payload.append(static_cast<char>(sid));
    payload.append(params);

    if (payload.size() > 7) {
        return false;
    }

    QVector<CanFrame> frames;
    if (transport.buildRequestFrames(payload, &frames) != IsoTpTransport::Result::Ok || frames.isEmpty()) {
        return false;
    }

    {
        QMutexLocker locker(&mutex);
        hasResponse = false;
    }

    emit transmitFrame(frames[0].id, frames[0].data);
    return true;
}

bool OtaWorker::sendMultiFrame(const QByteArray &payload, int timeoutMs)
{
    if (payload.isEmpty()) {
        return false;
    }

    int totalSize = payload.size();
    QByteArray ffPayload = payload.left(6);
    CanFrame ffFrame = transport.buildFirstFrame(ffPayload, totalSize);

    // 鍙戦€侀甯?(FF)
    {
        QMutexLocker locker(&mutex);
        hasResponse = false;
    }
    emit transmitFrame(ffFrame.id, ffFrame.data);

    // 绛夊緟娴佹帶甯?(FC)
    QMutexLocker locker(&mutex);
    IsoTpFlowControl fc;
    bool fcReceived = false;
    QElapsedTimer timer;
    timer.start();

    while (true) {
        int remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            break;
        }

        if (!hasResponse) {
            if (!waitCondition.wait(&mutex, remaining)) {
                break;
            }
        }

        if (abortRequested.load()) {
            break;
        }

        if (hasResponse) {
            if (responseFrame.id == config.responseId && responseFrame.channel == config.channel) {
                if (IsoTpTransport::parseFlowControl(responseFrame.data, fc)) {
                    fcReceived = true;
                    break;
                }
            }
            hasResponse = false; // Not a match, discard and keep waiting
        }
    }

    if (abortRequested.load()) {
        lastError = "Abort requested during Flow Control";
        return false;
    }

    if (!fcReceived) {
        lastError = "Flow Control timeout";
        return false;
    }

    if (fc.flowStatus == 1) { // WAIT
        lastError = "Flow status WAIT not supported";
        return false;
    } else if (fc.flowStatus == 2) { // OVERFLOW
        lastError = "Flow status OVERFLOW";
        return false;
    }

    // 鍙戦€佽繛缁抚 (CF)
    locker.unlock();
    int offset = 6;
    quint8 seq = 1;
    int bsCount = 0;

    while (offset < totalSize) {
        if (abortRequested.load()) {
            lastError = "Abort requested during CF transmission";
            return false;
        }

        // 甯ч棿闅旀帶鍒?(STmin)
        int stMin = fc.stMin;
        qint64 targetNsecs = 0;
        if (stMin == 0) {
            targetNsecs = static_cast<qint64>(config.consecutiveFrameIntervalMs) * 1000000;
        } else if (stMin <= 0x7F) {
            targetNsecs = static_cast<qint64>(stMin) * 1000000;
        } else if (stMin >= 0xF1 && stMin <= 0xF9) {
            targetNsecs = static_cast<qint64>(stMin - 0xF0) * 100000;
        } else {
            targetNsecs = 1000000; // Default 1ms
        }

        if (injectConfig.enabled && injectConfig.ignoreFcInterval) {
            targetNsecs = 0;
        }

        if (targetNsecs > 0 && !abortRequested.load()) {
            if (targetNsecs >= 1000000) {
                QThread::msleep(static_cast<unsigned long>(targetNsecs / 1000000));
            } else {
                QThread::usleep(static_cast<unsigned long>((targetNsecs + 999) / 1000));
            }
        }

        int len = qMin(7, totalSize - offset);
        QByteArray cfPayload = payload.mid(offset, len);
        CanFrame cfFrame = transport.buildConsecutiveFrame(cfPayload, seq);

        if (injectConfig.enabled && injectConfig.isoTpSnError && seq == 3) {
            if (!cfFrame.data.isEmpty()) {
                // 鏁呮剰灏嗗簭鍙蜂负 3 鐨勮繛缁抚鐨?SN 瀛楄妭绡℃敼涓?5 (璺冲寘)
                cfFrame.data[0] = static_cast<char>((cfFrame.data[0] & 0xF0) | 0x05);
            }
        }

        offset += len;
        seq = (seq + 1) & 0x0F;

        // 鍙戦€佽繛缁抚
        locker.relock();
        hasResponse = false;
        locker.unlock();
        emit transmitFrame(cfFrame.id, cfFrame.data);

        bsCount++;
        if (fc.blockSize > 0 && bsCount >= fc.blockSize && offset < totalSize) {

            bsCount = 0;
            fcReceived = false;
            timer.restart();
            locker.relock();
            hasResponse = false;

            while (true) {
                int remaining = timeoutMs - timer.elapsed();
                if (remaining <= 0) {
                    break;
                }

                if (!hasResponse) {
                    if (!waitCondition.wait(&mutex, remaining)) {
                        break;
                    }
                }

                if (abortRequested.load()) {
                    break;
                }

                if (hasResponse) {
                    if (responseFrame.id == config.responseId && responseFrame.channel == config.channel) {
                        if (IsoTpTransport::parseFlowControl(responseFrame.data, fc)) {
                            fcReceived = true;
                            break;
                        }
                    }
                    hasResponse = false; // Not a match, discard and keep waiting
                }
            }

            if (abortRequested.load()) {
                return false;
            }

            if (!fcReceived) {
                lastError = "Consecutive Flow Control timeout";
                return false;
            }

            if (fc.flowStatus != 0) { // CTS
                lastError = QString("Flow status error: %1").arg(fc.flowStatus);
                return false;
            }
            locker.unlock();
        }
    }

    return true;
}

void OtaWorker::run()
{
    if (queryOnlyMode) {
        updateStatus(static_cast<int>(OtaService::State::QueryProgram), QStringLiteral("\u6b63\u5728\u67e5\u8be2\u7a0b\u5e8f\u4f4d\u7f6e..."), 0);
        for (int retry = 0; retry < 3; ++retry) {
            if (sendSingleFrame(0xA4)) {
                QByteArray responsePayload;
                if (waitForResponse(0xE4, 3000, responsePayload) && responsePayload.size() >= 2) {
                    const quint8 location = static_cast<quint8>(responsePayload[1]);
                    const QString locationText = (location == 0x00) ? QStringLiteral("BOOT") : QStringLiteral("APP");
                    updateStatus(static_cast<int>(OtaService::State::Completed),
                                 QStringLiteral("\u7a0b\u5e8f\u4f4d\u7f6e\uff1a%1").arg(locationText), 100);
                    return;
                }
            }
            msleep(100);
        }
        updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("\u67e5\u8be2\u7a0b\u5e8f\u4f4d\u7f6e\u5931\u8d25"), 0);
        return;
    }

    // --- 鎻愬墠璇诲彇鍥轰欢鏂囦欢 (浠ユ敮鎸佽秺鏉冪洿鎺ュ彂鍖呮祴璇? ---
    QFile file(firmwarePath);
    if (!file.open(QIODevice::ReadOnly)) {
        updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("无法打开固件文件"), 5);
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    if (fileData.isEmpty()) {
        updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("固件文件为空"), 5);
        return;
    }
    quint32 fileSize = fileData.size();
    int chunkSize = 128; // 榛樿鍒嗗寘瀛楄妭鏁帮紝浣滀负鍑芥暟浣滅敤鍩熷彉閲忎互閬垮厤 goto 浜ゅ弶鍒濆鍖?
    // 瓒婃潈鐩存帴鍙戝寘娴嬭瘯娉ㄥ叆
    if (injectConfig.enabled && injectConfig.outOfOrderState) {
        updateStatus(static_cast<int>(OtaService::State::SendData), QStringLiteral("[注入] 越权发包：跳过握手直接下发固件包..."), 15);
        chunkSize = 240;
        goto send_data_phase;
    }

    { // 鎻℃墜闃舵灞€閮ㄤ綔鐢ㄥ煙锛岄槻姝?goto 浜ゅ弶鍒濆鍖栨湰鍦板彉閲?        // --- Phase 1: Query (鏌ヨ) ---
        updateStatus(static_cast<int>(OtaService::State::QueryProgram), QStringLiteral("正在查询程序位置..."), 0);
        bool queryOk = false;
        for (int retry = 0; retry < 3; ++retry) {
            if (sendSingleFrame(0xA4)) {
                QByteArray e4Resp;
                if (waitForResponse(0xE4, 3000, e4Resp)) {
                    if (e4Resp.size() >= 2) {
                        quint8 location = static_cast<quint8>(e4Resp[1]);
                        QString locText = (location == 0x00) ? QStringLiteral("BOOT") : QStringLiteral("APP");
                        updateStatus(static_cast<int>(OtaService::State::QueryProgram), QStringLiteral("程序位置：%1").arg(locText), 5);
                        queryOk = true;
                        break;
                    }
                }
            }
            msleep(100);
        }
        if (!queryOk) {
            // 鏌ヨ澶辫触閫氬父闈炶嚧鍛斤紝浠呮墦鍗版棩蹇楀苟缁х画
            qWarning() << "Query program location timeout or failed: " << lastError;
            updateStatus(static_cast<int>(OtaService::State::QueryProgram), QStringLiteral("查询程序位置失败，继续尝试升级"), 5);
        }

        if (abortRequested.load()) {
            updateStatus(static_cast<int>(OtaService::State::Abort), QStringLiteral("升级已被用户中止"), 5);
            return;
        }

        // --- Phase 2: Start Upgrade (寮€濮? ---
        updateStatus(static_cast<int>(OtaService::State::StartUpgrade), QStringLiteral("正在启动固件升级，包大小：%1 字节...").arg(fileSize), 10);
        
        QByteArray a1Payload;
        a1Payload.append(static_cast<char>(0xA1));
        a1Payload.append(static_cast<char>(protocolVersion));
        quint8 sendVendorCode = vendorCode;
        if (injectConfig.enabled && injectConfig.vendorMismatch) {
            sendVendorCode = vendorCode == 0xFF ? 0x00 : static_cast<quint8>(vendorCode ^ 0xFF);
            updateStatus(static_cast<int>(OtaService::State::StartUpgrade),
                         QStringLiteral("[注入] A1 厂商代码不匹配：0x%1 -> 0x%2")
                             .arg(vendorCode, 2, 16, QChar('0'))
                             .arg(sendVendorCode, 2, 16, QChar('0'))
                             .toUpper(),
                         10);
        }
        a1Payload.append(static_cast<char>(sendVendorCode));
        if (injectConfig.enabled && injectConfig.hwMismatch) {
            // 鏁呮剰鍙戦€侀敊璇殑纭欢鐗堟湰锛屾ā鎷熺‖浠剁増鏈笉鍖归厤
            a1Payload.append(static_cast<char>(0xFF));
            a1Payload.append(static_cast<char>(0xFF));
        } else {
            a1Payload.append(static_cast<char>((hwVersion >> 8) & 0xFF));
            a1Payload.append(static_cast<char>(hwVersion & 0xFF));
        }
        a1Payload.append(static_cast<char>((swVersion >> 8) & 0xFF));
        a1Payload.append(static_cast<char>(swVersion & 0xFF));
        a1Payload.append(static_cast<char>((fileSize >> 24) & 0xFF));
        a1Payload.append(static_cast<char>((fileSize >> 16) & 0xFF));
        a1Payload.append(static_cast<char>((fileSize >> 8) & 0xFF));
        a1Payload.append(static_cast<char>(fileSize & 0xFF));

        bool startOk = false;
        int negotiatedChunkSize = 128; // 榛樿鍒嗗寘瀛楄妭鏁颁负128
        for (int retry = 0; retry < 3; ++retry) {
            if (sendMultiFrame(a1Payload, 3000)) {
                QByteArray e1Resp;
                if (waitForResponse(0xE1, 3000, e1Resp)) {
                    if (e1Resp.size() >= 2 && static_cast<quint8>(e1Resp[1]) == 0x00) {
                        startOk = true;
                        if (e1Resp.size() >= 4) {
                            quint16 parsedSize = (static_cast<quint8>(e1Resp[2]) << 8) | static_cast<quint8>(e1Resp[3]);
                            if (parsedSize >= 64 && parsedSize <= 1024) {
                                negotiatedChunkSize = parsedSize;
                            }
                        }
                        QString sysStatusText = QStringLiteral("Unknown");
                        if (e1Resp.size() >= 7) {
                            quint8 sysStatus = static_cast<quint8>(e1Resp[6]);
                            if (sysStatus == 0x01) {
                                sysStatusText = QStringLiteral("APP");
                            } else if (sysStatus == 0x02) {
                                sysStatusText = QStringLiteral("Bootloader");
                            }
                        }
                        qDebug() << "OTA start accepted. Negotiated chunk size:" << negotiatedChunkSize << "Device status:" << sysStatusText;
                        break;
                    } else if (e1Resp.size() >= 2) {
                        lastError = QString("Refused by device: 0x%1").arg(static_cast<quint8>(e1Resp[1]), 2, 16, QChar('0'));
                    }
                }
            }
            if (abortRequested.load()) {
                updateStatus(static_cast<int>(OtaService::State::Abort), QStringLiteral("升级已被用户中止"), 10);
                return;
            }
            msleep(100);
        }

        if (!startOk) {
            updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("升级启动请求失败: ") + lastError, 10);
            return;
        }
        chunkSize = negotiatedChunkSize; // 瀵煎嚭鍗忓晢鍖呭ぇ灏忓埌澶栭儴鍙橀噺
    } // 缁撴潫浣滅敤鍩?
    // --- Phase 3: Send Data (鍙戝寘闃舵) ---
send_data_phase:
    updateStatus(static_cast<int>(OtaService::State::SendData), QStringLiteral("开始下发固件包..."), 15);
    int totalChunks = (fileData.size() + chunkSize - 1) / chunkSize;
    bool firstA2DataErrorInjected = false;

    for (int i = 0; i < totalChunks; ++i) {
        if (abortRequested.load()) {
            updateStatus(static_cast<int>(OtaService::State::Abort), QStringLiteral("升级已被用户中止"), 15);
            sendSingleFrame(0xA3, QByteArray(1, 0x02)); // 涓鍗囩骇鍛戒护
            return;
        }

        if (injectConfig.enabled && injectConfig.silentTimeout && i >= totalChunks / 2) {
            updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("[注入] 中途静默超时已触发"), 50);
            return;
        }

        int offset = i * chunkSize;
        int len = qMin(chunkSize, fileData.size() - offset);
        QByteArray chunkData = fileData.mid(offset, len);
        quint16 chunkId = i + 1;

        if (injectConfig.enabled &&
            injectConfig.a2FirstFrameDataError &&
            !firstA2DataErrorInjected &&
            i == 0) {
            if (!chunkData.isEmpty()) {
                chunkData[0] = static_cast<char>(static_cast<quint8>(chunkData[0]) ^ 0xFF);
                firstA2DataErrorInjected = true;
                updateStatus(static_cast<int>(OtaService::State::SendData),
                             QStringLiteral("[注入] A2 首帧数据错误：已篡改第 1 个 A2 包的数据内容，包号保持 0x0001。"),
                             15);
            } else {
                updateStatus(static_cast<int>(OtaService::State::SendData),
                             QStringLiteral("[注入] A2 首帧数据错误未执行：首包数据为空。"),
                             15);
            }
        }

        if (injectConfig.enabled && injectConfig.seqError && chunkId == 3) {
            // 鏁呮剰绡℃敼绗?3 鍖呯殑鍖呭彿涓?99锛屽埗閫犲寘鍙蜂笉杩炵画
            chunkId = 99;
        }

        QByteArray a2Payload;
        a2Payload.append(static_cast<char>(0xA2));
        a2Payload.append(static_cast<char>((chunkId >> 8) & 0xFF));
        a2Payload.append(static_cast<char>(chunkId & 0xFF));
        a2Payload.append(chunkData);

        bool chunkOk = false;
        for (int retry = 0; retry < 3; ++retry) {
            if (sendMultiFrame(a2Payload, 3000)) {
                QByteArray e2Resp;
                if (waitForResponse(0xE2, 3000, e2Resp)) {
                    if (e2Resp.size() >= 4) {
                        quint16 respChunkId = (static_cast<quint8>(e2Resp[1]) << 8) | static_cast<quint8>(e2Resp[2]);
                        quint8 writeStatus = static_cast<quint8>(e2Resp[3]);
                        if (respChunkId == chunkId && (writeStatus == 0x00 || writeStatus == 0x02)) {
                            chunkOk = true;
                            break;
                        } else {
                            lastError = QString("Chunk ID mismatch or failure: chunkId=%1 state=0x%2").arg(respChunkId).arg(writeStatus, 2, 16, QChar('0'));
                        }
                    }
                }
            }
            if (abortRequested.load()) {
                updateStatus(static_cast<int>(OtaService::State::Abort), QStringLiteral("升级已被用户中止"), 15);
                sendSingleFrame(0xA3, QByteArray(1, 0x02)); // 涓鍗囩骇鍛戒护
                return;
            }
            msleep(10);
        }

        if (!chunkOk) {
            updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("发送数据包失败，包号 %1，错误: ").arg(chunkId) + lastError);
            sendSingleFrame(0xA3, QByteArray(1, 0x02)); // 涓鍗囩骇鍛戒护
            return;
        }

        int progressPercent = 15 + ((i + 1) * 80 / totalChunks); // 15% 鍒?95%
        updateStatus(static_cast<int>(OtaService::State::SendData), QStringLiteral("已写入包 %1/%2").arg(chunkId).arg(totalChunks), progressPercent);
    }

    // --- Phase 4: Finish & Verify (缁撴潫鏍￠獙) ---
    updateStatus(static_cast<int>(OtaService::State::FinishUpgrade), QStringLiteral("固件下发完成，正在计算校验码..."), 95);
    quint16 finalCrc = calculateCrc16(reinterpret_cast<const quint8*>(fileData.constData()), fileData.size());
    
    if (injectConfig.enabled && injectConfig.crcError) {
        // 鏍￠獙鐮佸彇鍙嶏紝鍒堕€?CRC16 鏍￠獙閿欒
        finalCrc ^= 0xFFFF;
    }

    QByteArray a3Params;
    a3Params.append(static_cast<char>(0x01)); // 鎵ц鍗囩骇
    a3Params.append(static_cast<char>(finalCrc & 0xFF)); // 灏忕妯″紡 CRC
    a3Params.append(static_cast<char>((finalCrc >> 8) & 0xFF));

    bool finishOk = false;
    for (int retry = 0; retry < 3; ++retry) {
        if (sendSingleFrame(0xA3, a3Params)) {
            QByteArray e3Resp;
            if (waitForResponse(0xE3, 3000, e3Resp)) {
                if (e3Resp.size() >= 2) {
                    quint8 status = static_cast<quint8>(e3Resp[1]);
                    if (status == 0x00) {
                        finishOk = true;
                        break;
                    } else if (status == 0x01) {
                        lastError = QStringLiteral("固件校验错误 (CRC16 校验失败)");
                        break; // 鏍￠獙澶辫触閲嶈瘯鏃犵敤
                    } else if (status == 0x02) {
                        lastError = QStringLiteral("设备端升级已中止");
                        break;
                    }
                }
            }
        }
        if (abortRequested.load()) {
            updateStatus(static_cast<int>(OtaService::State::Abort), QStringLiteral("升级已被用户中止"), 95);
            return;
        }
        msleep(100);
    }

    if (finishOk) {
        updateStatus(static_cast<int>(OtaService::State::Completed), QStringLiteral("固件校验成功，升级完成，设备重启中..."), 100);
    } else {
        updateStatus(static_cast<int>(OtaService::State::Failed), QStringLiteral("升级校验请求失败: ") + lastError, 95);
    }
}


// ==========================================
// OtaService Implementation
// ==========================================

OtaService::OtaService(QObject *parent) :
    QObject(parent),
    currentState(State::Idle),
    currentProgress(0),
    vendorCode(0x02), // 榛樿瓒呴珮棰戝▉绉戝 (0x02)
    hwVersion(0),
    swVersion(0)
{
    worker = new OtaWorker(this);
    connect(worker, &OtaWorker::transmitFrame, this, &OtaService::transmitFrame);
    connect(worker, &OtaWorker::statusUpdated, this, &OtaService::onWorkerStatusUpdated);
}

OtaService::~OtaService()
{
    if (worker->isRunning()) {
        worker->requestAbort();
        worker->wait();
    }
}

OtaService::State OtaService::state() const
{
    return currentState;
}

QString OtaService::stateText() const
{
    switch (currentState) {
    case State::Idle:
        return QStringLiteral("空闲");
    case State::QueryProgram:
        return QStringLiteral("查询程序位置");
    case State::StartUpgrade:
        return QStringLiteral("开始升级");
    case State::SendData:
        return QStringLiteral("发送数据");
    case State::FinishUpgrade:
        return QStringLiteral("升级校验");
    case State::Abort:
        return QStringLiteral("已中止");
    case State::Failed:
        return QStringLiteral("失败");
    case State::Completed:
        return QStringLiteral("完成");
    default:
        return QStringLiteral("未知");
    }
}

QString OtaService::lastMessage() const
{
    return currentMessage;
}

int OtaService::progress() const
{
    return currentProgress;
}

void OtaService::setChannel(quint32 channel)
{
    config.channel = channel;
}

void OtaService::setDeviceVersions(quint8 vendor, quint16 hw, quint16 sw)
{
    vendorCode = vendor;
    hwVersion = hw;
    swVersion = sw;
}

void OtaService::queryProgramLocation()
{
    if (worker->isRunning()) {
        return;
    }
    setState(State::QueryProgram, QStringLiteral("准备查询程序位置..."));
    currentProgress = 0;
    worker->setup(QString(), config, vendorCode, hwVersion, swVersion, 0x02, OtaErrorConfig(), true);
    worker->start();
}

void OtaService::startUpgrade(const QString &firmwarePath, const OtaErrorConfig &injectCfg)
{
    if (worker->isRunning()) {
        return;
    }
    if (firmwarePath.isEmpty() || firmwarePath == "-") {
        setState(State::Failed, QStringLiteral("请先选择正确的固件文件！"));
        return;
    }
    setState(State::StartUpgrade, QStringLiteral("准备启动升级流程..."));
    currentProgress = 0;
    worker->setup(firmwarePath, config, vendorCode, hwVersion, swVersion, 0x02, injectCfg);
    worker->start();
}

void OtaService::abortUpgrade()
{
    if (worker->isRunning()) {
        worker->requestAbort();
        setState(State::Abort, QStringLiteral("正在中止升级..."));
    } else {
        setState(State::Abort, QStringLiteral("升级未运行"));
    }
}

void OtaService::handleIncomingFrame(const CanFrame &frame)
{
    if (worker->isRunning()) {
        worker->handleIncomingFrame(frame);
    }
}

void OtaService::onWorkerStatusUpdated(int stateVal, const QString &message, int progressPercent)
{
    State newState = static_cast<State>(stateVal);
    if (progressPercent >= 0) {
        currentProgress = progressPercent;
        emit otaProgress(currentProgress);
    }
    setState(newState, message);
}

void OtaService::setState(State state, const QString &message)
{
    currentState = state;
    currentMessage = message;
    emit otaStateChanged(currentState, message);
}

