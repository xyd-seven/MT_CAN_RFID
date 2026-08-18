#include "testcasejudge.h"

#include <QDateTime>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <climits>

namespace {
const int kRfidRequestId = 0x007;
const int kRfidResponseId = 0x107;
const int kRfidBroadcastFirstId = 0x2C0;
const int kRfidBroadcastLastId = 0x2C6;
const int kClassicCanDlc = 8;
const int kRequestResponseTimeoutMs = 3000;
const int kBootBroadcastStopObservationMs = 4000;
const int kBootBroadcastStopTransportGraceMs = 100;
const int kOtaBootInactivityTimeoutMs = 5000;

struct EvidenceFrame
{
    QDateTime timestamp;
    qint64 sourceOrder = -1;
    QString line;
    QString direction;
    int canId = -1;
    QStringList bytes;
};

struct PeriodWindowCheck
{
    bool checked = false;
    bool passed = false;
    QString reason;
};

struct PeriodConfigRequest
{
    int targetId = -1;
    int periodMs = -1;
    QString requestCompact;
    QString positiveCompact;
};

QStringList parseCsvLine(const QString &line)
{
    QStringList fields;
    QString current;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                current.append(ch);
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == QLatin1Char(',') && !quoted) {
            fields.append(current);
            current.clear();
        } else {
            current.append(ch);
        }
    }
    fields.append(current);
    return fields;
}

QStringList hexByteTokens(const QString &text)
{
    QStringList tokens;
    QRegularExpression regex(QStringLiteral("\\b[0-9A-Fa-f]{2}\\b"));
    QRegularExpressionMatchIterator it = regex.globalMatch(text);
    while (it.hasNext()) {
        tokens.append(it.next().captured(0).toUpper());
    }
    return tokens;
}

QString compactTokenText(const QStringList &tokens)
{
    QString compact;
    for (const QString &token : tokens) {
        compact.append(token);
    }
    return compact;
}

bool parseCanId(const QString &text, int *canId)
{
    QString value = text.trimmed().toUpper();
    if (value.startsWith(QStringLiteral("0X"))) {
        value.remove(0, 2);
    }
    bool ok = false;
    const int parsed = value.toInt(&ok, 16);
    if (!ok || parsed < 0 || parsed > 0x7FF) {
        return false;
    }
    if (canId != nullptr) {
        *canId = parsed;
    }
    return true;
}

QString idText(int canId)
{
    return QStringLiteral("0x%1").arg(canId, 3, 16, QChar('0')).toUpper();
}

bool isReceiveFrame(const EvidenceFrame &frame)
{
    const QString direction = frame.direction.trimmed();
    return direction == QString::fromUtf8("\xE6\x8E\xA5\xE6\x94\xB6") ||
           direction.compare(QStringLiteral("RX"), Qt::CaseInsensitive) == 0 ||
           direction == QStringLiteral("鎺ユ敹") ||
           direction.contains(QString::fromUtf8("\xE6\x8E\xA5\xE6\x94\xB6"));
}

QVector<EvidenceFrame> parseEvidenceFrames(const QString &evidenceText)
{
    QVector<EvidenceFrame> frames;
    const QStringList lines = evidenceText.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString &line = lines.at(lineIndex);
        const QStringList fields = parseCsvLine(line);
        if (fields.size() < 5) {
            continue;
        }

        EvidenceFrame frame;
        frame.line = line.trimmed();
        frame.sourceOrder = lineIndex;
        frame.timestamp = QDateTime::fromString(fields.at(0).trimmed(), QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
        frame.direction = fields.at(1).trimmed();
        if (!parseCanId(fields.at(3), &frame.canId)) {
            continue;
        }
        frame.bytes = hexByteTokens(fields.at(4));
        frames.append(frame);
    }
    std::stable_sort(frames.begin(), frames.end(), [](const EvidenceFrame &left, const EvidenceFrame &right) {
        if (!left.timestamp.isValid() || !right.timestamp.isValid()) {
            return left.timestamp.isValid() && !right.timestamp.isValid();
        }
        if (left.timestamp != right.timestamp) {
            return left.timestamp < right.timestamp;
        }
        return left.sourceOrder < right.sourceOrder;
    });
    return frames;
}

QVector<EvidenceFrame> framesById(const QVector<EvidenceFrame> &frames, int canId, bool receiveOnly = false)
{
    QVector<EvidenceFrame> matched;
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId != canId) {
            continue;
        }
        if (receiveOnly && !isReceiveFrame(frame)) {
            continue;
        }
        matched.append(frame);
    }
    return matched;
}

bool frameHasData(const EvidenceFrame &frame, const QString &compactSequence)
{
    return compactTokenText(frame.bytes).contains(compactSequence.toUpper());
}

bool evidenceHasFrameData(const QVector<EvidenceFrame> &frames, int canId, const QString &compactSequence, bool receiveOnly = false)
{
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId == canId &&
            (!receiveOnly || isReceiveFrame(frame)) &&
            frameHasData(frame, compactSequence)) {
            return true;
        }
    }
    return false;
}

bool findFirstFrameData(const QVector<EvidenceFrame> &frames, int canId, const QString &compactSequence, EvidenceFrame *matched)
{
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId == canId && frameHasData(frame, compactSequence)) {
            if (matched != nullptr) {
                *matched = frame;
            }
            return true;
        }
    }
    return false;
}

QVector<EvidenceFrame> framesAfter(const QVector<EvidenceFrame> &frames, const QDateTime &baseTime, int canId, int graceMs = 0)
{
    QVector<EvidenceFrame> matched;
    if (!baseTime.isValid()) {
        return matched;
    }
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId != canId || !isReceiveFrame(frame) || !frame.timestamp.isValid()) {
            continue;
        }
        if (baseTime.msecsTo(frame.timestamp) > graceMs) {
            matched.append(frame);
        }
    }
    return matched;
}

QVector<EvidenceFrame> framesBetween(const QVector<EvidenceFrame> &frames, const QDateTime &startTime, const QDateTime &endTime, int canId, int startGraceMs = 0)
{
    QVector<EvidenceFrame> matched;
    if (!startTime.isValid()) {
        return matched;
    }
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId != canId || !isReceiveFrame(frame) || !frame.timestamp.isValid()) {
            continue;
        }
        const qint64 afterStart = startTime.msecsTo(frame.timestamp);
        if (afterStart <= startGraceMs) {
            continue;
        }
        if (endTime.isValid() && frame.timestamp > endTime) {
            continue;
        }
        matched.append(frame);
    }
    return matched;
}

QDateTime latestFrameTimestamp(const QVector<EvidenceFrame> &frames)
{
    QDateTime latest;
    for (const EvidenceFrame &frame : frames) {
        if (frame.timestamp.isValid() && (!latest.isValid() || frame.timestamp > latest)) {
            latest = frame.timestamp;
        }
    }
    return latest;
}

bool evidenceHasFrameByte(const QVector<EvidenceFrame> &frames, int canId, int byteIndex, const QString &expectedHex)
{
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId != canId || !isReceiveFrame(frame)) {
            continue;
        }
        if (byteIndex >= 0 && byteIndex < frame.bytes.size() &&
            frame.bytes.at(byteIndex).compare(expectedHex, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QStringList keyFrameLines(const QVector<EvidenceFrame> &frames, const QList<int> &ids, int limit = 16)
{
    QVector<const EvidenceFrame *> selectedFrames;
    QStringList lines;
    for (const int canId : ids) {
        for (const EvidenceFrame &frame : frames) {
            if (frame.canId == canId) {
                selectedFrames.append(&frame);
                break;
            }
        }
    }

    for (const EvidenceFrame &frame : frames) {
        if (ids.contains(frame.canId)) {
            if (selectedFrames.contains(&frame)) {
                continue;
            }
            selectedFrames.append(&frame);
        }
    }

    std::stable_sort(selectedFrames.begin(), selectedFrames.end(), [](const EvidenceFrame *left, const EvidenceFrame *right) {
        if (!left->timestamp.isValid() || !right->timestamp.isValid()) {
            return left->timestamp.isValid() && !right->timestamp.isValid();
        }
        if (left->timestamp != right->timestamp) {
            return left->timestamp < right->timestamp;
        }
        return left->sourceOrder < right->sourceOrder;
    });
    for (const EvidenceFrame *frame : qAsConst(selectedFrames)) {
        lines.append(frame->line);
        if (lines.size() >= limit) {
            break;
        }
    }
    return lines;
}

QList<int> configuredKeyFrameIds(const TestCase &testCase, const QList<int> &fallback)
{
    QList<int> ids;
    for (const QString &idTextValue : testCase.keyFrameIds) {
        int canId = -1;
        if (parseCanId(idTextValue, &canId) && !ids.contains(canId)) {
            ids.append(canId);
        }
    }
    return ids.isEmpty() ? fallback : ids;
}

QStringList keyFrameLinesForCase(const TestCase &testCase,
                                 const QVector<EvidenceFrame> &frames,
                                 const QList<int> &fallback,
                                 int limit = 12)
{
    return keyFrameLines(frames, configuredKeyFrameIds(testCase, fallback), limit);
}

QString asciiFromFrame(const EvidenceFrame &frame)
{
    QString text;
    for (const QString &byteText : frame.bytes) {
        bool ok = false;
        const int value = byteText.toInt(&ok, 16);
        if (!ok || value == 0x00 || value == 0x55) {
            continue;
        }
        if (value >= 0x20 && value <= 0x7E) {
            text.append(QChar(value));
        }
    }
    return text;
}

bool findFirstFrameDataAfter(const QVector<EvidenceFrame> &frames,
                             int canId,
                             const QString &compactSequence,
                             const QDateTime &baseTime,
                             EvidenceFrame *matched)
{
    if (!baseTime.isValid()) {
        return false;
    }
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId == canId &&
            frame.timestamp.isValid() &&
            frame.timestamp > baseTime &&
            frameHasData(frame, compactSequence)) {
            if (canId == kRfidResponseId &&
                baseTime.msecsTo(frame.timestamp) > kRequestResponseTimeoutMs) {
                continue;
            }
            if (matched != nullptr) {
                *matched = frame;
            }
            return true;
        }
    }
    return false;
}

int hexByteValue(const QString &byteText)
{
    bool ok = false;
    const int value = byteText.toInt(&ok, 16);
    return ok ? value : -1;
}

QVector<int> frameByteValues(const EvidenceFrame &frame)
{
    QVector<int> values;
    for (const QString &byteText : frame.bytes) {
        const int value = hexByteValue(byteText);
        if (value < 0 || value > 0xFF) {
            return QVector<int>();
        }
        values.append(value);
    }
    return values;
}

bool findIsoTpPayload(const QVector<EvidenceFrame> &frames, int canId, int sid, QVector<int> *payload)
{
    const QVector<EvidenceFrame> idFrames = framesById(frames, canId, true);
    for (int index = 0; index < idFrames.size(); ++index) {
        const QVector<int> bytes = frameByteValues(idFrames.at(index));
        if (bytes.size() < 2) {
            continue;
        }

        const int pciType = (bytes.at(0) >> 4) & 0x0F;
        if (pciType == 0x0) {
            const int payloadLength = bytes.at(0) & 0x0F;
            if (payloadLength <= 0 || bytes.size() < payloadLength + 1 || bytes.at(1) != sid) {
                continue;
            }
            QVector<int> currentPayload;
            for (int byteIndex = 1; byteIndex <= payloadLength; ++byteIndex) {
                currentPayload.append(bytes.at(byteIndex));
            }
            if (payload != nullptr) {
                *payload = currentPayload;
            }
            return true;
        }

        if (pciType != 0x1 || bytes.size() < 3 || bytes.at(2) != sid) {
            continue;
        }

        const int payloadLength = ((bytes.at(0) & 0x0F) << 8) | bytes.at(1);
        if (payloadLength <= 0) {
            continue;
        }
        QVector<int> currentPayload;
        for (int byteIndex = 2; byteIndex < bytes.size() && currentPayload.size() < payloadLength; ++byteIndex) {
            currentPayload.append(bytes.at(byteIndex));
        }
        int expectedSequence = 1;
        for (int nextIndex = index + 1; nextIndex < idFrames.size() && currentPayload.size() < payloadLength; ++nextIndex) {
            const QVector<int> cfBytes = frameByteValues(idFrames.at(nextIndex));
            if (cfBytes.isEmpty() || ((cfBytes.at(0) >> 4) & 0x0F) != 0x2) {
                continue;
            }
            const int sequence = cfBytes.at(0) & 0x0F;
            if (sequence != (expectedSequence & 0x0F)) {
                break;
            }
            ++expectedSequence;
            for (int byteIndex = 1; byteIndex < cfBytes.size() && currentPayload.size() < payloadLength; ++byteIndex) {
                currentPayload.append(cfBytes.at(byteIndex));
            }
        }
        if (currentPayload.size() >= payloadLength) {
            if (payload != nullptr) {
                *payload = currentPayload;
            }
            return true;
        }
    }
    return false;
}

QVector<QVector<int>> collectIsoTpPayloads(const QVector<EvidenceFrame> &frames, int canId, int sid)
{
    QVector<QVector<int>> payloads;
    const QVector<EvidenceFrame> idFrames = framesById(frames, canId, canId == kRfidResponseId);
    for (int index = 0; index < idFrames.size(); ++index) {
        const QVector<int> bytes = frameByteValues(idFrames.at(index));
        if (bytes.size() < 2) {
            continue;
        }

        const int pciType = (bytes.at(0) >> 4) & 0x0F;
        if (pciType == 0x0) {
            const int payloadLength = bytes.at(0) & 0x0F;
            if (payloadLength <= 0 || bytes.size() < payloadLength + 1 || bytes.at(1) != sid) {
                continue;
            }
            QVector<int> payload;
            for (int byteIndex = 1; byteIndex <= payloadLength; ++byteIndex) {
                payload.append(bytes.at(byteIndex));
            }
            payloads.append(payload);
            continue;
        }

        if (pciType != 0x1 || bytes.size() < 3 || bytes.at(2) != sid) {
            continue;
        }

        const int payloadLength = ((bytes.at(0) & 0x0F) << 8) | bytes.at(1);
        if (payloadLength <= 0) {
            continue;
        }
        QVector<int> payload;
        for (int byteIndex = 2; byteIndex < bytes.size() && payload.size() < payloadLength; ++byteIndex) {
            payload.append(bytes.at(byteIndex));
        }

        int expectedSequence = 1;
        for (int nextIndex = index + 1; nextIndex < idFrames.size() && payload.size() < payloadLength; ++nextIndex) {
            const QVector<int> cfBytes = frameByteValues(idFrames.at(nextIndex));
            if (cfBytes.isEmpty() || ((cfBytes.at(0) >> 4) & 0x0F) != 0x2) {
                continue;
            }
            const int sequence = cfBytes.at(0) & 0x0F;
            if (sequence != (expectedSequence & 0x0F)) {
                break;
            }
            ++expectedSequence;
            for (int byteIndex = 1; byteIndex < cfBytes.size() && payload.size() < payloadLength; ++byteIndex) {
                payload.append(cfBytes.at(byteIndex));
            }
        }

        if (payload.size() >= payloadLength) {
            payloads.append(payload);
        }
    }
    return payloads;
}

bool payloadContainsDidAndData(const QVector<int> &payload, quint16 did, const QByteArray &data)
{
    if (payload.size() < 3 + data.size() ||
        payload.at(0) != 0x2E ||
        payload.at(1) != ((did >> 8) & 0xFF) ||
        payload.at(2) != (did & 0xFF)) {
        return false;
    }
    for (int index = 0; index < data.size(); ++index) {
        if (payload.at(index + 3) != static_cast<quint8>(data.at(index))) {
            return false;
        }
    }
    return true;
}

QString otaSystemStatusText(int status)
{
    if (status == 0x01) {
        return QStringLiteral("APP");
    }
    if (status == 0x02) {
        return QStringLiteral("BOOT");
    }
    return QStringLiteral("未知(0x%1)").arg(status, 2, 16, QChar('0')).toUpper();
}

QString firstAsciiFromId(const QVector<EvidenceFrame> &frames, int canId)
{
    for (const EvidenceFrame &frame : framesById(frames, canId, true)) {
        const QString text = asciiFromFrame(frame);
        if (!text.trimmed().isEmpty()) {
            return text.trimmed();
        }
    }
    return QString();
}

QString collectedDeviceId(const QVector<EvidenceFrame> &frames)
{
    return firstAsciiFromId(frames, 0x2C4) + firstAsciiFromId(frames, 0x2C5);
}

bool isBcdByte(int value)
{
    return value >= 0 && value <= 0x99 &&
        ((value >> 4) & 0x0F) <= 9 && (value & 0x0F) <= 9;
}

bool validateVersionBroadcastContent(const QVector<EvidenceFrame> &frames,
                                     QString *summary,
                                     QString *failureReason)
{
    const QVector<EvidenceFrame> versionFrames = framesById(frames, 0x2C3, true);
    if (versionFrames.isEmpty()) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("未采集到0x2C3版本广播。");
        }
        return false;
    }
    const EvidenceFrame &frame = versionFrames.first();
    const QVector<int> bytes = frameByteValues(frame);
    if (bytes.size() != kClassicCanDlc) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("0x2C3数据长度不是8字节，实际=%1。").arg(bytes.size());
        }
        return false;
    }
    const int bootVersion = bytes.at(0);
    const int vendorCode = bytes.at(1);
    const int hardwareHigh = bytes.at(2);
    const int hardwareLow = bytes.at(3);
    const int softwareHigh = bytes.at(4);
    const int softwareLow = bytes.at(5);
    if (bootVersion <= 0 || bootVersion == 0xFF || vendorCode <= 0 || vendorCode == 0xFF) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("0x2C3 BOOT版本或厂商代码为空/非法：BOOT=0x%1，厂商=0x%2。")
                .arg(bootVersion, 2, 16, QChar('0'))
                .arg(vendorCode, 2, 16, QChar('0')).toUpper();
        }
        return false;
    }
    if (!isBcdByte(hardwareHigh) || !isBcdByte(hardwareLow) ||
        !isBcdByte(softwareHigh) || !isBcdByte(softwareLow) ||
        (hardwareHigh == 0 && hardwareLow == 0) ||
        (softwareHigh == 0xFF && softwareLow == 0xFF)) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("0x2C3硬件/软件版本字段不是合法的大端BCD或为空：HW=0x%1%2，SW=0x%3%4。")
                .arg(hardwareHigh, 2, 16, QChar('0'))
                .arg(hardwareLow, 2, 16, QChar('0'))
                .arg(softwareHigh, 2, 16, QChar('0'))
                .arg(softwareLow, 2, 16, QChar('0')).toUpper();
        }
        return false;
    }
    if (summary != nullptr) {
        *summary = QStringLiteral("0x2C3格式有效：BOOT=0x%1，厂商=0x%2，HW=0x%3%4，SW=0x%5%6，物料=0x%7%8")
            .arg(bootVersion, 2, 16, QChar('0'))
            .arg(vendorCode, 2, 16, QChar('0'))
            .arg(hardwareHigh, 2, 16, QChar('0'))
            .arg(hardwareLow, 2, 16, QChar('0'))
            .arg(softwareHigh, 2, 16, QChar('0'))
            .arg(softwareLow, 2, 16, QChar('0'))
            .arg(bytes.at(6), 2, 16, QChar('0'))
            .arg(bytes.at(7), 2, 16, QChar('0')).toUpper();
    }
    return true;
}

bool validateCompleteDeviceId(const QVector<EvidenceFrame> &frames,
                              QString *deviceId,
                              QString *failureReason)
{
    const QString value = collectedDeviceId(frames);
    if (value.size() != 16) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("0x2C4/0x2C5未拼接成完整16字节设备ID，实际=%1。")
                .arg(value.isEmpty() ? QStringLiteral("空") : value);
        }
        return false;
    }
    bool printable = true;
    for (const QChar ch : value) {
        if (ch.unicode() < 0x20 || ch.unicode() > 0x7E) {
            printable = false;
            break;
        }
    }
    const QString compact = value.trimmed();
    const bool placeholder = compact.isEmpty() ||
        compact == QString(16, QChar('0')) || compact == QString(16, QChar('F'));
    if (!printable || placeholder) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral("设备ID包含非打印字符或占位值：%1。").arg(value);
        }
        return false;
    }
    if (deviceId != nullptr) {
        *deviceId = value;
    }
    return true;
}

bool findFrameDataAfter(const QVector<EvidenceFrame> &frames,
                        int canId,
                        const QString &compactSequence,
                        const QDateTime &baseTime,
                        EvidenceFrame *matched,
                        int graceMs = 0)
{
    if (!baseTime.isValid()) {
        return false;
    }
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId != canId || !frame.timestamp.isValid()) {
            continue;
        }
        if (baseTime.msecsTo(frame.timestamp) <= graceMs) {
            continue;
        }
        if (frameHasData(frame, compactSequence)) {
            if (matched != nullptr) {
                *matched = frame;
            }
            return true;
        }
    }
    return false;
}

QVector<EvidenceFrame> framesAfterAnyId(const QVector<EvidenceFrame> &frames,
                                        const QDateTime &baseTime,
                                        const QList<int> &ids,
                                        int graceMs = 0)
{
    QVector<EvidenceFrame> matched;
    if (!baseTime.isValid()) {
        return matched;
    }
    for (const EvidenceFrame &frame : frames) {
        if (!ids.contains(frame.canId) || !isReceiveFrame(frame) || !frame.timestamp.isValid()) {
            continue;
        }
        if (baseTime.msecsTo(frame.timestamp) > graceMs) {
            matched.append(frame);
        }
    }
    return matched;
}

bool findWritePositiveResponse(const QVector<EvidenceFrame> &frames,
                               const QString &didLowByte,
                               EvidenceFrame *matched)
{
    return findFirstFrameData(frames,
                              kRfidResponseId,
                              QStringLiteral("036EE7") + didLowByte.toUpper(),
                              matched);
}

bool parseScanPeriod(const TestCase &testCase, QString *periodHex)
{
    QRegularExpression payloadRegex(QStringLiteral("\\b02\\s+01\\s+([0-9A-Fa-f]{2})\\b"));
    QRegularExpressionMatch match = payloadRegex.match(testCase.testData);
    if (!match.hasMatch()) {
        QRegularExpression valueRegex(QStringLiteral("周期\\s*[=:：]?\\s*(\\d{1,3})"));
        match = valueRegex.match(testCase.testData);
        if (!match.hasMatch()) {
            return false;
        }
        bool ok = false;
        const int value = match.captured(1).toInt(&ok);
        if (!ok || value < 0 || value > 255) {
            return false;
        }
        if (periodHex != nullptr) {
            *periodHex = QStringLiteral("%1").arg(value, 2, 16, QChar('0')).toUpper();
        }
        return true;
    }
    if (periodHex != nullptr) {
        *periodHex = match.captured(1).toUpper();
    }
    return true;
}

QVector<PeriodConfigRequest> parsePeriodConfigs(const TestCase &testCase)
{
    QVector<PeriodConfigRequest> configs;
    const QString text = testCase.testData;
    QRegularExpression payloadRegex(QStringLiteral("\\b05\\s+29\\s+([0-9A-Fa-f]{2})\\s+([0-9A-Fa-f]{2})\\s+([0-9A-Fa-f]{2})\\s+([0-9A-Fa-f]{2})\\b"));
    QRegularExpressionMatchIterator it = payloadRegex.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        bool ok = false;
        const int parsedId = match.captured(1).toInt(&ok, 16) << 8;
        if (!ok) continue;
        const int parsedIdLow = match.captured(2).toInt(&ok, 16);
        if (!ok) continue;
        const int parsedPeriod = match.captured(3).toInt(&ok, 16) << 8;
        if (!ok) continue;
        const int parsedPeriodLow = match.captured(4).toInt(&ok, 16);
        if (!ok) continue;

        PeriodConfigRequest config;
        config.targetId = parsedId | parsedIdLow;
        config.periodMs = parsedPeriod | parsedPeriodLow;
        config.requestCompact = QStringLiteral("0529%1%2%3%4")
            .arg(match.captured(1).toUpper(),
                 match.captured(2).toUpper(),
                 match.captured(3).toUpper(),
                 match.captured(4).toUpper());
        config.positiveCompact = QStringLiteral("69") + config.requestCompact.mid(4);
        bool exists = false;
        for (const PeriodConfigRequest &existing : configs) {
            if (existing.requestCompact == config.requestCompact) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            configs.append(config);
        }
    }
    return configs;
}

QVector<QPair<int, QString>> expectedBroadcastBytes(const QString &caseText)
{
    QVector<QPair<int, QString>> expected;
    QRegularExpression regex(QStringLiteral("Byte\\s*(\\d)[^0-9A-Fa-f]{0,24}(?:0x)?([0-9A-Fa-f]{2})"));
    QRegularExpressionMatchIterator it = regex.globalMatch(caseText);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        bool ok = false;
        const int byteNumber = match.captured(1).toInt(&ok);
        if (ok && byteNumber >= 1) {
            expected.append(qMakePair(byteNumber - 1, match.captured(2).toUpper()));
        }
    }
    return expected;
}

bool hasNegativeResponse(const QVector<EvidenceFrame> &frames, QString sidHex)
{
    sidHex = sidHex.toUpper();
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId != kRfidResponseId || frame.bytes.size() < 4) {
            continue;
        }
        if (frame.bytes.at(1) == QStringLiteral("7F") && frame.bytes.at(2) == sidHex) {
            return true;
        }
    }
    return false;
}

bool findNegativeResponse(const QVector<EvidenceFrame> &frames, QString sidHex, EvidenceFrame *matched)
{
    sidHex = sidHex.toUpper();
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId != kRfidResponseId || frame.bytes.size() < 4) {
            continue;
        }
        if (frame.bytes.at(1) == QStringLiteral("7F") && frame.bytes.at(2) == sidHex) {
            if (matched != nullptr) {
                *matched = frame;
            }
            return true;
        }
    }
    return false;
}

int countNegativeResponses(const QVector<EvidenceFrame> &frames, QString sidHex, const QStringList &allowedNrc, QString *badNrc)
{
    sidHex = sidHex.toUpper();
    int count = 0;
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId != kRfidResponseId || frame.bytes.size() < 4) {
            continue;
        }
        if (frame.bytes.at(1) == QStringLiteral("7F") && frame.bytes.at(2) == sidHex) {
            const QString nrc = frame.bytes.at(3).toUpper();
            if (!allowedNrc.isEmpty() && !allowedNrc.contains(nrc)) {
                if (badNrc != nullptr) {
                    *badNrc = nrc;
                }
                return -1;
            }
            ++count;
        }
    }
    return count;
}

int countFrameFirstByte(const QVector<EvidenceFrame> &frames, int canId, const QString &firstByte)
{
    int count = 0;
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId == canId && !frame.bytes.isEmpty() &&
            frame.bytes.at(0).compare(firstByte, Qt::CaseInsensitive) == 0) {
            ++count;
        }
    }
    return count;
}

bool hasFrameFirstByte(const QVector<EvidenceFrame> &frames, int canId, const QString &firstByte)
{
    return countFrameFirstByte(frames, canId, firstByte) > 0;
}

bool frameBytesAllValue(const EvidenceFrame &frame, const QString &expected)
{
    for (const QString &byte : frame.bytes) {
        if (byte.compare(expected, Qt::CaseInsensitive) != 0) {
            return false;
        }
    }
    return !frame.bytes.isEmpty();
}

bool hasUnrecognizedTagPlaceholderFrame(const QVector<EvidenceFrame> &frames, int canId)
{
    for (const EvidenceFrame &frame : framesById(frames, canId, true)) {
        if (frame.bytes.size() == kClassicCanDlc &&
            frameBytesAllValue(frame, QStringLiteral("30"))) {
            return true;
        }
    }
    return false;
}

bool frameBytesAreValidTagSegment(const EvidenceFrame &frame)
{
    if (frame.bytes.size() != kClassicCanDlc ||
        frameBytesAllValue(frame, QStringLiteral("30"))) {
        return false;
    }
    for (const QString &byteText : frame.bytes) {
        bool ok = false;
        const int value = byteText.toInt(&ok, 16);
        if (!ok) {
            return false;
        }
        if (value < 0x20 || value > 0x7E) {
            return false;
        }
    }
    return true;
}

bool hasValidTagSegmentFrame(const QVector<EvidenceFrame> &frames, int canId)
{
    for (const EvidenceFrame &frame : framesById(frames, canId, true)) {
        if (frameBytesAreValidTagSegment(frame)) {
            return true;
        }
    }
    return false;
}

bool allBytesEqual(const QStringList &bytes, int firstIndex, int lastIndex, const QString &expected)
{
    if (bytes.size() <= lastIndex) {
        return false;
    }
    for (int index = firstIndex; index <= lastIndex; ++index) {
        if (bytes.at(index).compare(expected, Qt::CaseInsensitive) != 0) {
            return false;
        }
    }
    return true;
}

QVector<EvidenceFrame> tagSegmentsInStatusCycle(const QVector<EvidenceFrame> &frames,
                                                const EvidenceFrame &status,
                                                int canId)
{
    int statusIndex = -1;
    for (int index = 0; index < frames.size(); ++index) {
        const EvidenceFrame &candidate = frames.at(index);
        if (candidate.canId == 0x2C0 && isReceiveFrame(candidate) &&
            candidate.timestamp == status.timestamp && candidate.line == status.line) {
            statusIndex = index;
            break;
        }
    }
    if (statusIndex < 0) {
        return QVector<EvidenceFrame>();
    }

    QVector<EvidenceFrame> segments;
    for (int index = statusIndex + 1; index < frames.size(); ++index) {
        const EvidenceFrame &frame = frames.at(index);
        if (frame.canId == 0x2C0 && isReceiveFrame(frame)) {
            break;
        }
        if (frame.canId == canId && isReceiveFrame(frame)) {
            segments.append(frame);
        }
    }
    return segments;
}

enum class TagFragmentCheck {
    Valid,
    Missing,
    Invalid
};

TagFragmentCheck checkRecognizedTagFragments(const QVector<EvidenceFrame> &frames,
                                             const EvidenceFrame &status,
                                             QString *detail)
{
    const QList<int> requiredIds = QList<int>() << 0x2C1 << 0x2C2;
    for (const int canId : requiredIds) {
        const QVector<EvidenceFrame> segments = tagSegmentsInStatusCycle(frames, status, canId);
        if (segments.isEmpty()) {
            if (detail != nullptr) {
                *detail = QStringLiteral("%1 未采集到").arg(idText(canId));
            }
            return TagFragmentCheck::Missing;
        }
        for (const EvidenceFrame &segment : segments) {
            if (allBytesEqual(segment.bytes, 0, 7, QStringLiteral("00")) ||
                allBytesEqual(segment.bytes, 0, 7, QStringLiteral("30"))) {
                if (detail != nullptr) {
                    *detail = QStringLiteral("%1=%2").arg(idText(canId), compactTokenText(segment.bytes));
                }
                return TagFragmentCheck::Invalid;
            }
        }
    }

    const QVector<EvidenceFrame> optionalSegment = tagSegmentsInStatusCycle(frames, status, 0x2C6);
    for (const EvidenceFrame &segment : optionalSegment) {
        if (allBytesEqual(segment.bytes, 0, 7, QStringLiteral("00"))) {
            if (detail != nullptr) {
                *detail = QStringLiteral("0x2C6=%1").arg(compactTokenText(segment.bytes));
            }
            return TagFragmentCheck::Invalid;
        }
    }
    return TagFragmentCheck::Valid;
}

bool statusHasStoppedNoTag(const EvidenceFrame &status)
{
    return status.bytes.size() >= 3 &&
           status.bytes.at(0) == QStringLiteral("00") &&
           status.bytes.at(1) == QStringLiteral("00") &&
           status.bytes.at(2) == QStringLiteral("00");
}

int stopStabilizationMs(const QVector<EvidenceFrame> &frames, const QDateTime &commandTime)
{
    int scanPeriodMs = 0;
    for (const EvidenceFrame &status : framesAfter(frames, commandTime, 0x2C0)) {
        if (status.bytes.size() < 4) {
            continue;
        }
        bool ok = false;
        const int periodUnit = status.bytes.at(3).toInt(&ok, 16);
        if (ok && periodUnit > 0) {
            scanPeriodMs = periodUnit * 10;
            break;
        }
    }
    return qMax(600, scanPeriodMs * 2);
}

bool hasClassicDlcMismatch(const QVector<EvidenceFrame> &frames, int firstId, int lastId, QString *badId)
{
    for (int canId = firstId; canId <= lastId; ++canId) {
        for (const EvidenceFrame &frame : framesById(frames, canId, true)) {
            if (frame.bytes.size() != kClassicCanDlc) {
                if (badId != nullptr) {
                    *badId = idText(canId);
                }
                return true;
            }
        }
    }
    return false;
}

QList<int> startupPeriodTargetIds(const QString &caseText)
{
    QList<int> ids;
    for (int canId = 0x2C3; canId <= 0x2C6; ++canId) {
        if (caseText.contains(idText(canId), Qt::CaseInsensitive) && !ids.contains(canId)) {
            ids.append(canId);
        }
    }
    QRegularExpression rangeRegex(QStringLiteral("0x(2C[3-6])\\s*[~\\-～至到]\\s*(?:0x)?(2C[3-6])"),
                                  QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator ranges = rangeRegex.globalMatch(caseText);
    while (ranges.hasNext()) {
        const QRegularExpressionMatch match = ranges.next();
        bool startOk = false;
        bool endOk = false;
        const int startId = match.captured(1).toInt(&startOk, 16);
        const int endId = match.captured(2).toInt(&endOk, 16);
        if (!startOk || !endOk) {
            continue;
        }
        for (int canId = qMin(startId, endId); canId <= qMax(startId, endId); ++canId) {
            if (canId >= 0x2C3 && canId <= 0x2C6 && !ids.contains(canId)) {
                ids.append(canId);
            }
        }
    }
    if (ids.isEmpty()) {
        ids << 0x2C3 << 0x2C4 << 0x2C5;
    }
    return ids;
}

QString startupCheckText(const TestCase &testCase)
{
    return QStringList{
        testCase.id,
        testCase.module,
        testCase.basis,
        testCase.precondition,
        testCase.testData,
        testCase.steps,
        testCase.expectedResult,
        testCase.commandTemplate,
        testCase.judgeTemplate,
        testCase.semiJudgeTemplate,
        testCase.keyFrameIds.join(QLatin1Char(' '))
    }.join(QLatin1Char(' '));
}

int startupBurstExpectedMs(const QString &caseText, int canId)
{
    QString compact = caseText.toUpper();
    compact.remove(QRegularExpression(QStringLiteral("\\s+")));
    if (canId == 0x2C3) {
        return compact.contains(QStringLiteral("100MS")) && !compact.contains(QStringLiteral("200MS")) ? 100 : 200;
    }
    if (canId == 0x2C4 || canId == 0x2C5 || canId == 0x2C6) {
        return compact.contains(QStringLiteral("200MS")) && !compact.contains(QStringLiteral("100MS")) ? 200 : 100;
    }
    return compact.contains(QStringLiteral("100MS")) ? 100 : 200;
}

bool requiresStartupBroadcastPeriodCheck(const QString &caseText)
{
    QString compact = caseText.toUpper();
    compact.remove(QRegularExpression(QStringLiteral("\\s+")));
    return (compact.contains(QStringLiteral("200MS")) || compact.contains(QStringLiteral("100MS"))) &&
           (caseText.contains(QStringLiteral("20次")) ||
            caseText.contains(QStringLiteral("20 次")) ||
            caseText.contains(QStringLiteral("20帧")) ||
            caseText.contains(QStringLiteral("20 帧"))) &&
           (compact.contains(QStringLiteral("10S")) || compact.contains(QStringLiteral("10000MS")));
}

bool intervalsMatchExpected(const QVector<EvidenceFrame> &frames,
                            int startIndex,
                            int endIndex,
                            int expectedMs,
                            int toleranceMs,
                            QString *summary)
{
    if (startIndex < 0 || endIndex >= frames.size() || endIndex - startIndex < 1) {
        return false;
    }

    qint64 totalMs = 0;
    qint64 minMs = LLONG_MAX;
    qint64 maxMs = 0;
    int count = 0;
    for (int index = startIndex + 1; index <= endIndex; ++index) {
        if (!frames.at(index - 1).timestamp.isValid() || !frames.at(index).timestamp.isValid()) {
            if (summary != nullptr) {
                *summary = QStringLiteral("存在无效时间戳");
            }
            return false;
        }
        const qint64 interval = frames.at(index - 1).timestamp.msecsTo(frames.at(index).timestamp);
        if (interval <= 0) {
            if (summary != nullptr) {
                *summary = QStringLiteral("存在无效时间间隔");
            }
            return false;
        }
        minMs = qMin(minMs, interval);
        maxMs = qMax(maxMs, interval);
        totalMs += interval;
        ++count;
    }

    const double averageMs = count > 0 ? static_cast<double>(totalMs) / count : 0.0;
    const bool passed = qAbs(averageMs - expectedMs) <= toleranceMs &&
                        qAbs(static_cast<double>(minMs) - expectedMs) <= toleranceMs &&
                        qAbs(static_cast<double>(maxMs) - expectedMs) <= toleranceMs;
    if (summary != nullptr) {
        *summary = QStringLiteral("帧数=%1，间隔数=%2，平均=%3 ms，最小=%4 ms，最大=%5 ms，期望=%6 ms，容差=±%7 ms")
            .arg(count + 1)
            .arg(count)
            .arg(averageMs, 0, 'f', 1)
            .arg(minMs)
            .arg(maxMs)
            .arg(expectedMs)
            .arg(toleranceMs);
    }
    return passed;
}

PeriodWindowCheck checkStartupBroadcastPeriod(const QVector<EvidenceFrame> &frames,
                                              const QList<int> &targetIds,
                                              const QString &caseText)
{
    PeriodWindowCheck result;
    result.checked = true;

    QStringList details;
    for (const int canId : targetIds) {
        const QVector<EvidenceFrame> idFrames = framesById(frames, canId, true);
        if (idFrames.size() < 20) {
            result.reason = QStringLiteral("%1 快发阶段样本不足：需要至少 20 帧，当前 %2 帧。")
                .arg(idText(canId))
                .arg(idFrames.size());
            return result;
        }

        QString burstSummary;
        const int burstExpectedMs = startupBurstExpectedMs(caseText, canId);
        const int burstToleranceMs = burstExpectedMs <= 100 ? 50 : 80;
        if (!intervalsMatchExpected(idFrames, 0, 19, burstExpectedMs, burstToleranceMs, &burstSummary)) {
            result.reason = QStringLiteral("%1 上电前 20 帧 %2ms 周期不满足要求：%3。")
                .arg(idText(canId))
                .arg(burstExpectedMs)
                .arg(burstSummary);
            return result;
        }
        details << QStringLiteral("%1 前20帧快发周期通过：%2").arg(idText(canId), burstSummary);

        if (idFrames.size() < 22) {
            result.reason = QStringLiteral("%1 前20帧快发周期通过，但第20帧后 10s 周期样本不足：需要至少 2 帧，当前 %2 帧。")
                .arg(idText(canId))
                .arg(qMax(0, idFrames.size() - 20));
            return result;
        }

        QString normalSummary;
        if (!intervalsMatchExpected(idFrames, 20, idFrames.size() - 1, 10000, 2500, &normalSummary)) {
            result.reason = QStringLiteral("%1 第20帧后 10s 周期不满足要求：%2。")
                .arg(idText(canId), normalSummary);
            return result;
        }
        details << QStringLiteral("%1 第20帧后 10s 周期通过：%2").arg(idText(canId), normalSummary);
    }

    result.passed = true;
    result.reason = details.join(QStringLiteral("；"));
    return result;
}

QStringList appearedBroadcastIds(const QVector<EvidenceFrame> &frames)
{
    QStringList appeared;
    for (int canId = kRfidBroadcastFirstId; canId <= kRfidBroadcastLastId; ++canId) {
        if (!framesById(frames, canId, true).isEmpty()) {
            appeared.append(idText(canId));
        }
    }
    return appeared;
}

QStringList missingBroadcastIds(const QVector<EvidenceFrame> &frames)
{
    QStringList missing;
    for (int canId = kRfidBroadcastFirstId; canId <= kRfidBroadcastLastId; ++canId) {
        if (framesById(frames, canId, true).isEmpty()) {
            missing.append(idText(canId));
        }
    }
    return missing;
}

TestJudgeResult makeBlocked(const QString &reason, const QString &category, const QStringList &keyFrames = QStringList())
{
    TestJudgeResult result;
    result.status = TestResultStatus::Blocked;
    result.reason = reason;
    result.failureCategory = category;
    result.keyFrames = keyFrames;
    return result;
}

TestJudgeResult makeFailed(const QString &reason, const QString &category, const QStringList &keyFrames = QStringList())
{
    TestJudgeResult result;
    result.status = TestResultStatus::Failed;
    result.reason = reason;
    result.failureCategory = category;
    result.keyFrames = keyFrames;
    return result;
}

TestJudgeResult makePassed(const QString &reason, const QStringList &keyFrames = QStringList())
{
    TestJudgeResult result;
    result.status = TestResultStatus::Passed;
    result.reason = reason;
    result.keyFrames = keyFrames;
    return result;
}

TestJudgeResult judgeBroadcastAllAfterReboot(const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId
        << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C3 << 0x2C4 << 0x2C5 << 0x2C6);
    const bool hasRequest = evidenceHasFrameData(frames, kRfidRequestId, QStringLiteral("0102"));
    const bool hasPositive = evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("0142"), true);
    if (!hasRequest) {
        return makeBlocked(QStringLiteral("未发现自动重启请求 01 02，无法确认本用例已按重启后广播完整性流程执行。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    if (hasNegativeResponse(frames, QStringLiteral("02"))) {
        return makeFailed(QStringLiteral("收到重启服务否定响应 7F 02，重启后广播完整性用例失败。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }
    if (!hasPositive) {
        return makeBlocked(QStringLiteral("已发送重启请求，但未发现 01 42 肯定响应，需确认设备是否在线或响应是否丢失。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }

    QString badDlcId;
    if (hasClassicDlcMismatch(frames, kRfidBroadcastFirstId, kRfidBroadcastLastId, &badDlcId)) {
        return makeFailed(QStringLiteral("%1 广播帧数据长度不是 8 字节，不符合美团 CAN 周期帧要求。").arg(badDlcId),
                          QStringLiteral("dlc_mismatch"),
                          keyFrames);
    }

    const QStringList missing = missingBroadcastIds(frames);
    if (!missing.isEmpty()) {
        return makeFailed(QStringLiteral("重启后未采集到全部 0x2C0~0x2C6 广播帧；已出现：%1；缺失：%2。")
                              .arg(appearedBroadcastIds(frames).join(QStringLiteral(", ")),
                                   missing.join(QStringLiteral(", "))),
                          QStringLiteral("broadcast_missing"),
                          keyFrames);
    }

    for (const EvidenceFrame &frame : framesById(frames, 0x2C0, true)) {
        if (!allBytesEqual(frame.bytes, 4, 7, QStringLiteral("55"))) {
            return makeFailed(QStringLiteral("0x2C0 保留字节 Byte5~Byte8 不是 55 55 55 55。"),
                              QStringLiteral("expected_mismatch"),
                              keyFrames);
        }
    }

    return makePassed(QStringLiteral("已发送重启并收到 01 42 肯定响应，重启后 0x2C0~0x2C6 全部出现且广播帧长度为 8 字节。"),
                      keyFrames);
}

TestJudgeResult judgeRebootPreservesDeviceConfig(const TestCase &testCase,
                                                 const QVector<EvidenceFrame> &frames,
                                                 const QString &evidenceText)
{
    const QStringList keyFrames = keyFrameLinesForCase(
        testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C0 << 0x2C4 << 0x2C5, 24);
    const QRegularExpression baselineRegex(
        QStringLiteral("SVC-005配置基线[^\\r\\n]*扫描周期=0[xX]([0-9A-Fa-f]{2})；设备ID=([^\\r\\n；]+)"));
    const QRegularExpressionMatch baselineMatch = baselineRegex.match(evidenceText);
    if (!baselineMatch.hasMatch()) {
        return makeBlocked(QStringLiteral("缺少SVC-005执行前扫描周期/设备ID基线。"),
                           QStringLiteral("baseline_missing"), keyFrames);
    }
    EvidenceFrame rebootResponse;
    if (!findFirstFrameData(frames, kRfidResponseId, QStringLiteral("0142"), &rebootResponse) ||
        !rebootResponse.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到SID=0x02对应的01 42复位响应。"),
                           QStringLiteral("response_missing"), keyFrames);
    }
    const QVector<EvidenceFrame> rebootFrames = framesAfterAnyId(
        frames, rebootResponse.timestamp, QList<int>() << 0x2C0 << 0x2C4 << 0x2C5, -1);
    const QString expectedPeriod = baselineMatch.captured(1).toUpper();
    bool periodMatched = false;
    for (const EvidenceFrame &frame : rebootFrames) {
        if (frame.canId == 0x2C0 && frame.bytes.size() >= 4 &&
            frame.bytes.at(3).compare(expectedPeriod, Qt::CaseInsensitive) == 0) {
            periodMatched = true;
            break;
        }
    }
    if (!periodMatched) {
        return makeFailed(QStringLiteral("软件复位后扫描周期未保持为基线0x%1。").arg(expectedPeriod),
                          QStringLiteral("scan_period_persistence_mismatch"), keyFrames);
    }
    const QString expectedDeviceId = baselineMatch.captured(2).trimmed();
    const QString actualDeviceId = collectedDeviceId(rebootFrames);
    if (actualDeviceId != expectedDeviceId) {
        return makeFailed(QStringLiteral("软件复位后设备ID与基线不一致：基线=%1，实际=%2。")
                              .arg(expectedDeviceId, actualDeviceId.isEmpty() ? QStringLiteral("空") : actualDeviceId),
                          QStringLiteral("device_id_persistence_mismatch"), keyFrames);
    }
    return makePassed(QStringLiteral("SID=0x02复位后扫描周期0x%1和设备ID=%2均与执行前基线一致。")
                          .arg(expectedPeriod, actualDeviceId), keyFrames);
}

TestJudgeResult judgeControlStatus(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << 0x207 << 0x2C0);
    const bool expectStart = testCase.commandTemplate == QStringLiteral("mt.control_0x207_start");
    const QString expectedMode = expectStart ? QStringLiteral("01") : QStringLiteral("00");
    const QString expectedCommand = expectStart ? QStringLiteral("01") : QStringLiteral("00");
    if (!evidenceHasFrameData(frames, 0x207, expectedCommand)) {
        return makeBlocked(QStringLiteral("未发现 0x207 控制帧发送证据，无法自动判定工作模式跟随。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    const QVector<EvidenceFrame> statusFrames = framesById(frames, 0x2C0, true);
    if (statusFrames.isEmpty()) {
        return makeBlocked(QStringLiteral("已发送 0x207 控制帧，但未收到 0x2C0 状态帧。"),
                           QStringLiteral("evidence_missing"),
                           keyFrames);
    }
    if (!evidenceHasFrameByte(frames, 0x2C0, 0, expectedMode)) {
        return makeFailed(QStringLiteral("0x2C0 Byte1 未跟随 0x207 期望工作模式 0x%1。").arg(expectedMode),
                          QStringLiteral("expected_mismatch"),
                          keyFrames);
    }
    if (testCase.judgeTemplate == QStringLiteral("mt.status_2c0_no_tag") &&
        !evidenceHasFrameByte(frames, 0x2C0, 1, QStringLiteral("00"))) {
        return makeFailed(QStringLiteral("停止检测场景下 0x2C0 Byte2 未上报 0x00 未识别 TAG。"),
                          QStringLiteral("expected_mismatch"),
                          keyFrames);
    }
    for (const EvidenceFrame &frame : statusFrames) {
        if (frame.bytes.size() == kClassicCanDlc && !allBytesEqual(frame.bytes, 4, 7, QStringLiteral("55"))) {
            return makeFailed(QStringLiteral("0x2C0 保留字节 Byte5~Byte8 不是 55 55 55 55。"),
                              QStringLiteral("expected_mismatch"),
                              keyFrames);
        }
    }
    return makePassed(QStringLiteral("0x207 控制帧已发送，0x2C0 工作模式字段与期望一致。"), keyFrames);
}

TestJudgeResult judgeScanPeriod(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C0);
    QString periodHex;
    if (!parseScanPeriod(testCase, &periodHex)) {
        return makeBlocked(QStringLiteral("用例测试数据未提供明确的 02 01 XX 扫描周期命令，无法自动判定。"),
                           QStringLiteral("manual_required"),
                           keyFrames);
    }
    if (hasNegativeResponse(frames, QStringLiteral("01"))) {
        return makeFailed(QStringLiteral("发现扫描周期服务否定响应 7F 01，正向用例自动判定失败。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }
    EvidenceFrame positiveResponse;
    if (!findFirstFrameData(frames, kRfidResponseId, QStringLiteral("0241") + periodHex, &positiveResponse)) {
        return makeBlocked(QStringLiteral("未发现 02 41 %1 肯定响应，扫描周期配置证据不足。").arg(periodHex),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    QDateTime readbackStart = positiveResponse.timestamp;
    if (testCase.postCommandTemplate == QStringLiteral("mt.sid_0x01_restore_saved")) {
        EvidenceFrame rebootRequest;
        EvidenceFrame rebootResponse;
        if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("0102"),
                                     positiveResponse.timestamp, &rebootRequest) ||
            !findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("0142"),
                                     rebootRequest.timestamp, &rebootResponse)) {
            return makeBlocked(QStringLiteral("已配置扫描周期，但未采集到后续SID=0x02/0x42复位边界，无法验证Flash保持。"),
                               QStringLiteral("reboot_evidence_missing"), keyFrames);
        }
        readbackStart = rebootResponse.timestamp;
    }
    bool matchingReadback = false;
    for (const EvidenceFrame &status : framesAfter(frames, readbackStart, 0x2C0, -1)) {
        if (status.bytes.size() >= 4 && status.bytes.at(3).compare(periodHex, Qt::CaseInsensitive) == 0) {
            matchingReadback = true;
            break;
        }
    }
    if (!matchingReadback) {
        return makeFailed(QStringLiteral("已收到 02 41 %1，但未发现 0x2C0 Byte4 更新为 0x%1。").arg(periodHex),
                          QStringLiteral("expected_mismatch"),
                          keyFrames);
    }
    return makePassed(testCase.postCommandTemplate == QStringLiteral("mt.sid_0x01_restore_saved")
                          ? QStringLiteral("收到02 41 %1；软件复位后0x2C0 Byte4仍为0x%1，Flash保持验证通过。").arg(periodHex)
                          : QStringLiteral("收到 02 41 %1 肯定响应，且 0x2C0 Byte4 与扫描周期配置一致。").arg(periodHex),
                      keyFrames);
}

TestJudgeResult judgePositiveResponseAndBroadcast(const QVector<EvidenceFrame> &frames, const QString &sidHex, const QString &positiveHex)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId
        << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C3 << 0x2C4 << 0x2C5 << 0x2C6);
    if (hasNegativeResponse(frames, sidHex)) {
        return makeFailed(QStringLiteral("收到服务 0x%1 的否定响应，正向用例失败。").arg(sidHex),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }
    if (!evidenceHasFrameData(frames, kRfidResponseId, positiveHex, true)) {
        return makeBlocked(QStringLiteral("未发现服务 0x%1 的肯定响应证据。").arg(sidHex),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    if (missingBroadcastIds(frames).isEmpty()) {
        return makePassed(QStringLiteral("已收到肯定响应，且 0x2C0~0x2C6 广播恢复出现。"), keyFrames);
    }
    return makeBlocked(QStringLiteral("已收到肯定响应，但广播恢复证据不完整；已出现：%1；缺失：%2。")
                           .arg(appearedBroadcastIds(frames).join(QStringLiteral(", ")),
                                missingBroadcastIds(frames).join(QStringLiteral(", "))),
                       QStringLiteral("broadcast_missing"),
                       keyFrames);
}

TestJudgeResult judgeBroadcastRecovered(const QVector<EvidenceFrame> &frames)
{
    return judgePositiveResponseAndBroadcast(frames, QStringLiteral("28"), QStringLiteral("026801"));
}

TestJudgeResult judgeBroadcastRecoveredAfterDisableEnable(const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId
        << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C3 << 0x2C4 << 0x2C5 << 0x2C6);
    if (hasNegativeResponse(frames, QStringLiteral("28"))) {
        return makeFailed(QStringLiteral("收到通信控制 0x28 否定响应，使能广播用例失败。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }

    EvidenceFrame disableResponse;
    if (!findFirstFrameData(frames, kRfidResponseId, QStringLiteral("026800"), &disableResponse) ||
        !disableResponse.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未发现先置条件 02 68 00 肯定响应，无法证明已先禁用广播。"),
                           QStringLiteral("disable_response_missing"),
                           keyFrames);
    }

    EvidenceFrame enableResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("026801"), disableResponse.timestamp, &enableResponse) ||
        !enableResponse.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未发现禁用后的 02 68 01 肯定响应，无法确认使能广播命令生效。"),
                           QStringLiteral("enable_response_missing"),
                           keyFrames);
    }

    QStringList missingAfterEnable;
    for (int canId = kRfidBroadcastFirstId; canId <= kRfidBroadcastLastId; ++canId) {
        if (framesAfter(frames, enableResponse.timestamp, canId, 0).isEmpty()) {
            missingAfterEnable.append(idText(canId));
        }
    }
    if (!missingAfterEnable.isEmpty()) {
        return makeBlocked(QStringLiteral("使能后广播恢复证据不完整，缺失：%1。")
                               .arg(missingAfterEnable.join(QStringLiteral(", "))),
                           QStringLiteral("broadcast_missing"),
                           keyFrames);
    }

    return makePassed(QStringLiteral("已先收到 02 68 00 禁用响应，再收到 02 68 01 使能响应，且 0x2C0~0x2C6 已恢复广播。"),
                      keyFrames);
}

TestJudgeResult judgeBroadcastRecoveredAfterDisableReboot(const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId
        << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C3 << 0x2C4 << 0x2C5 << 0x2C6);
    if (hasNegativeResponse(frames, QStringLiteral("28")) || hasNegativeResponse(frames, QStringLiteral("02"))) {
        return makeFailed(QStringLiteral("禁用广播或重启请求收到否定响应，无法确认禁用后重启恢复。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }

    EvidenceFrame disableResponse;
    if (!findFirstFrameData(frames, kRfidResponseId, QStringLiteral("026800"), &disableResponse) ||
        !disableResponse.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未发现 02 68 00 肯定响应，无法确认禁用广播命令生效。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }

    EvidenceFrame rebootRequest;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("0102"), disableResponse.timestamp, &rebootRequest) ||
        !rebootRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("已禁用广播，但未发现后续 SID=0x02 重启请求，无法验证重启后恢复广播。"),
                           QStringLiteral("reboot_missing"),
                           keyFrames);
    }

    QStringList stillBroadcastingBeforeReboot;
    for (int canId = kRfidBroadcastFirstId; canId <= kRfidBroadcastLastId; ++canId) {
        if (!framesBetween(frames, disableResponse.timestamp, rebootRequest.timestamp, canId, 200).isEmpty()) {
            stillBroadcastingBeforeReboot.append(idText(canId));
        }
    }
    if (!stillBroadcastingBeforeReboot.isEmpty()) {
        return makeFailed(QStringLiteral("收到 02 68 00 后、重启前仍有广播帧出现：%1。")
                              .arg(stillBroadcastingBeforeReboot.join(QStringLiteral(", "))),
                          QStringLiteral("broadcast_not_disabled"),
                          keyFrames);
    }

    if (!evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("0142"), true)) {
        return makeBlocked(QStringLiteral("已发送 SID=0x02 重启请求，但未发现 01 42 肯定响应。"),
                           QStringLiteral("reboot_response_missing"),
                           keyFrames);
    }

    QStringList missingAfterReboot;
    for (int canId = kRfidBroadcastFirstId; canId <= kRfidBroadcastLastId; ++canId) {
        if (framesAfter(frames, rebootRequest.timestamp, canId, 0).isEmpty()) {
            missingAfterReboot.append(idText(canId));
        }
    }
    if (!missingAfterReboot.isEmpty()) {
        return makeBlocked(QStringLiteral("重启后广播恢复证据不完整，缺失：%1。")
                               .arg(missingAfterReboot.join(QStringLiteral(", "))),
                           QStringLiteral("broadcast_missing"),
                           keyFrames);
    }

    return makePassed(QStringLiteral("禁用广播后重启前未再出现 0x2C0~0x2C6，发送 SID=0x02 重启后 0x2C0~0x2C6 已恢复广播。"),
                      keyFrames);
}

TestJudgeResult judgeBroadcastDisabled(const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId
        << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C3 << 0x2C4 << 0x2C5 << 0x2C6);
    if (hasNegativeResponse(frames, QStringLiteral("28"))) {
        return makeFailed(QStringLiteral("收到通信控制 0x28 否定响应，禁用广播用例失败。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }
    if (!evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("026800"), true)) {
        return makeBlocked(QStringLiteral("未发现 02 68 00 肯定响应，无法确认禁用广播命令生效。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }

    EvidenceFrame responseFrame;
    if (!findFirstFrameData(frames, kRfidResponseId, QStringLiteral("026800"), &responseFrame) ||
        !responseFrame.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("已收到 02 68 00 肯定响应，但无法解析响应时间，不能按响应后窗口判断广播是否停止。"),
                            QStringLiteral("timestamp_invalid"),
                            keyFrames);
    }

    EvidenceFrame rebootRequest;
    if (findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("0102"), responseFrame.timestamp, &rebootRequest)) {
        return judgeBroadcastRecoveredAfterDisableReboot(frames);
    }

    QStringList stillBroadcasting;
    for (int canId = kRfidBroadcastFirstId; canId <= kRfidBroadcastLastId; ++canId) {
        if (!framesAfter(frames, responseFrame.timestamp, canId, 200).isEmpty()) {
            stillBroadcasting.append(idText(canId));
        }
    }
    if (!stillBroadcasting.isEmpty()) {
        return makeFailed(QStringLiteral("收到 02 68 00 后 200ms 之外仍有广播帧出现：%1。").arg(stillBroadcasting.join(QStringLiteral(", "))),
                          QStringLiteral("broadcast_not_disabled"),
                          keyFrames);
    }
    return makePassed(QStringLiteral("已收到 02 68 00 肯定响应，且响应后 200ms 之外未再发现 0x2C0~0x2C6 广播。"), keyFrames);
}

TestJudgeResult judgePeriodConfig(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId
        << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C3 << 0x2C4 << 0x2C5 << 0x2C6);
    const QVector<PeriodConfigRequest> configs = parsePeriodConfigs(testCase);
    if (configs.isEmpty()) {
        return makeBlocked(QStringLiteral("0x29 用例测试数据格式无效，无法解析目标 ID 和周期。"),
                           QStringLiteral("manual_required"),
                           keyFrames);
    }
    if (hasNegativeResponse(frames, QStringLiteral("29"))) {
        return makeFailed(QStringLiteral("收到 0x29 周期配置否定响应，正向用例失败。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }

    const bool requiresRebootPersistenceCheck =
        testCase.commandTemplate == QStringLiteral("mt.sid_0x29_period_config_and_reboot");
    QStringList summaries;
    QDateTime lastConfigResponseTime;
    for (const PeriodConfigRequest &config : configs) {
        EvidenceFrame requestFrame;
        if (!findFirstFrameData(frames, kRfidRequestId, config.requestCompact, &requestFrame) ||
            !requestFrame.timestamp.isValid()) {
            return makeBlocked(QStringLiteral("未发现目标 %1 周期 0x%2 的 0x29 请求，无法关联响应。")
                                   .arg(idText(config.targetId))
                                   .arg(config.periodMs, 4, 16, QChar('0')).toUpper(),
                               QStringLiteral("request_missing"),
                               keyFrames);
        }

        EvidenceFrame responseFrame;
        if (!findFirstFrameDataAfter(frames, kRfidResponseId, config.positiveCompact,
                                     requestFrame.timestamp, &responseFrame) ||
            !responseFrame.timestamp.isValid()) {
            return makeBlocked(QStringLiteral("目标 %1 的 0x29 肯定响应未在请求后 3 秒内收到，或响应 ID/周期不一致。")
                                   .arg(idText(config.targetId)),
                               QStringLiteral("response_timeout"),
                               keyFrames);
        }
        if (!lastConfigResponseTime.isValid() || responseFrame.timestamp > lastConfigResponseTime) {
            lastConfigResponseTime = responseFrame.timestamp;
        }

        // 对“配置并重启”用例，配置即时生效只检查到重启请求为止；
        // 重启后的广播由下方的持久化检查单独判定，避免混淆失败原因。
        EvidenceFrame rebootBoundary;
        const bool hasRebootBoundary = requiresRebootPersistenceCheck &&
            findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("0102"),
                                    responseFrame.timestamp, &rebootBoundary) &&
            rebootBoundary.timestamp.isValid();
        const QVector<EvidenceFrame> targetFrames = hasRebootBoundary
            ? framesBetween(frames, responseFrame.timestamp, rebootBoundary.timestamp, config.targetId, 0)
            : framesAfter(frames, responseFrame.timestamp, config.targetId, 0);
        if (config.periodMs == 0xFFFF) {
            const QVector<EvidenceFrame> afterGraceFrames = hasRebootBoundary
                ? framesBetween(frames, responseFrame.timestamp, rebootBoundary.timestamp, config.targetId, 200)
                : framesAfter(frames, responseFrame.timestamp, config.targetId, 200);
            if (!afterGraceFrames.isEmpty()) {
                return makeFailed(QStringLiteral("目标 %1 配置 0xFFFF 后仍在响应后 200ms 之外出现广播。")
                                      .arg(idText(config.targetId)),
                                  QStringLiteral("broadcast_not_disabled"),
                                  keyFrames);
            }
            summaries << QStringLiteral("%1 已停止广播").arg(idText(config.targetId));
            continue;
        }

        if (targetFrames.size() < 3) {
            return makeBlocked(QStringLiteral("已收到 0x29 肯定响应，但目标 %1 周期采样不足，至少需要 3 帧。")
                                   .arg(idText(config.targetId)),
                               QStringLiteral("insufficient_samples"),
                               keyFrames);
        }

        qint64 totalInterval = 0;
        int intervalCount = 0;
        for (int index = 1; index < targetFrames.size(); ++index) {
            if (!targetFrames.at(index - 1).timestamp.isValid() || !targetFrames.at(index).timestamp.isValid()) {
                return makeBlocked(QStringLiteral("目标 %1 帧时间戳无效，无法统计周期。").arg(idText(config.targetId)),
                                   QStringLiteral("timestamp_invalid"),
                                   keyFrames);
            }
            const qint64 interval = targetFrames.at(index - 1).timestamp.msecsTo(targetFrames.at(index).timestamp);
            if (interval > 0) {
                totalInterval += interval;
                ++intervalCount;
            }
        }
        if (intervalCount == 0) {
            return makeBlocked(QStringLiteral("目标 %1 帧时间间隔无效，无法统计周期。").arg(idText(config.targetId)),
                               QStringLiteral("timestamp_invalid"),
                               keyFrames);
        }
        const double averageMs = static_cast<double>(totalInterval) / intervalCount;
        const double tolerance = config.periodMs <= 100 ? 30.0 : (config.periodMs <= 1000 ? config.periodMs * 0.2 : config.periodMs * 0.25);
        if (qAbs(averageMs - config.periodMs) > tolerance) {
            return makeFailed(QStringLiteral("目标 %1 周期配置响应正确，但实测平均周期 %2 ms 与期望 %3 ms 超出容差。")
                                  .arg(idText(config.targetId))
                                  .arg(averageMs, 0, 'f', 1)
                                  .arg(config.periodMs),
                              QStringLiteral("period_mismatch"),
                              keyFrames);
        }
        summaries << QStringLiteral("%1 平均周期 %2 ms").arg(idText(config.targetId)).arg(averageMs, 0, 'f', 1);
    }

    if (requiresRebootPersistenceCheck) {
        EvidenceFrame rebootRequest;
        if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("0102"), lastConfigResponseTime, &rebootRequest) ||
            !rebootRequest.timestamp.isValid()) {
            return makeBlocked(QStringLiteral("已完成 0x29 配置响应检查，但未发现后续 SID=0x02 重启请求，无法验证配置掉电不丢失。"),
                               QStringLiteral("reboot_missing"),
                               keyFrames);
        }
        EvidenceFrame rebootResponse;
        if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("0142"), rebootRequest.timestamp, &rebootResponse)) {
            return makeBlocked(QStringLiteral("已发送 SID=0x02 重启请求，但未发现 01 42 肯定响应，无法验证重启后的配置保持。"),
                               QStringLiteral("reboot_response_missing"),
                               keyFrames);
        }

        // 自动判定后可能会下发 0x29 默认周期恢复命令。恢复阶段不属于本次
        // 重启保持验证，复判完整证据日志时也必须从观察窗口中排除。
        EvidenceFrame postTestPeriodRequest;
        const bool hasPostTestPeriodRequest = findFirstFrameDataAfter(
            frames, kRfidRequestId, QStringLiteral("0529"), rebootRequest.timestamp, &postTestPeriodRequest) &&
            postTestPeriodRequest.timestamp.isValid();
        const QDateTime rebootObservationEndTime = hasPostTestPeriodRequest
            ? postTestPeriodRequest.timestamp
            : latestFrameTimestamp(frames);

        QStringList rebootSummaries;
        for (const PeriodConfigRequest &config : configs) {
            // 0xFFFF 是持久化的完全禁用配置；重启不会触发上电快速广播。
            // 从重启请求开始出现任何目标广播，都说明禁用配置未保持。
            const int rebootGraceMs = config.periodMs == 0xFFFF ? 0 : 500;
            const QVector<EvidenceFrame> rebootFrames = framesBetween(
                frames, rebootRequest.timestamp, rebootObservationEndTime, config.targetId, rebootGraceMs);
            if (config.periodMs == 0xFFFF) {
                if (!rebootFrames.isEmpty()) {
                    return makeFailed(QStringLiteral("目标 %1 配置 0xFFFF 后，重启观察窗口内仍出现广播帧，配置未保持。")
                                          .arg(idText(config.targetId)),
                                      QStringLiteral("broadcast_not_disabled_after_reboot"),
                                      keyFrames);
                }
                rebootSummaries << QStringLiteral("%1 重启后保持禁止广播").arg(idText(config.targetId));
                continue;
            }
            if (rebootFrames.size() < 3) {
                return makeBlocked(QStringLiteral("目标 %1 重启后周期样本不足，至少需要 3 帧，当前 %2 帧。")
                                       .arg(idText(config.targetId))
                                       .arg(rebootFrames.size()),
                                   QStringLiteral("insufficient_reboot_samples"),
                                   keyFrames);
            }
            qint64 totalInterval = 0;
            int intervalCount = 0;
            for (int index = 1; index < rebootFrames.size(); ++index) {
                if (!rebootFrames.at(index - 1).timestamp.isValid() || !rebootFrames.at(index).timestamp.isValid()) {
                    return makeBlocked(QStringLiteral("目标 %1 重启后帧时间戳无效，无法统计周期。")
                                           .arg(idText(config.targetId)),
                                       QStringLiteral("timestamp_invalid"),
                                       keyFrames);
                }
                const qint64 interval = rebootFrames.at(index - 1).timestamp.msecsTo(rebootFrames.at(index).timestamp);
                if (interval > 0) {
                    totalInterval += interval;
                    ++intervalCount;
                }
            }
            if (intervalCount == 0) {
                return makeBlocked(QStringLiteral("目标 %1 重启后帧时间间隔无效，无法统计周期。")
                                       .arg(idText(config.targetId)),
                                   QStringLiteral("timestamp_invalid"),
                                   keyFrames);
            }
            const double rebootAverageMs = static_cast<double>(totalInterval) / intervalCount;
            const double tolerance = config.periodMs <= 100 ? 30.0 : (config.periodMs <= 1000 ? config.periodMs * 0.2 : config.periodMs * 0.25);
            if (qAbs(rebootAverageMs - config.periodMs) > tolerance) {
                return makeFailed(QStringLiteral("目标 %1 重启后平均周期 %2 ms 与期望 %3 ms 超出容差，配置保持验证失败。")
                                      .arg(idText(config.targetId))
                                      .arg(rebootAverageMs, 0, 'f', 1)
                                      .arg(config.periodMs),
                                  QStringLiteral("period_mismatch_after_reboot"),
                                  keyFrames);
            }
            rebootSummaries << QStringLiteral("%1 重启后平均周期 %2 ms").arg(idText(config.targetId)).arg(rebootAverageMs, 0, 'f', 1);
        }
        summaries << QStringLiteral("重启保持验证：%1").arg(rebootSummaries.join(QStringLiteral("；")));
    }

    const QString caseText = QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8")
        .arg(testCase.id,
             testCase.module,
             testCase.basis,
             testCase.testData,
             testCase.steps,
             testCase.expectedResult,
              testCase.commandTemplate,
              testCase.judgeTemplate);
    if (requiresStartupBroadcastPeriodCheck(caseText)) {
        const QList<int> startupIds = startupPeriodTargetIds(caseText);
        QList<int> periodFrameIds;
        periodFrameIds << kRfidRequestId << kRfidResponseId;
        for (const PeriodConfigRequest &config : configs) {
            periodFrameIds << config.targetId;
        }
        for (const int canId : startupIds) {
            if (!periodFrameIds.contains(canId)) {
                periodFrameIds.append(canId);
            }
        }
        const QStringList periodKeyFrames = keyFrameLinesForCase(testCase,
            frames,
            periodFrameIds,
            24);
        const PeriodWindowCheck startupCheck = checkStartupBroadcastPeriod(frames, startupIds, caseText);
        if (!startupCheck.passed) {
            return makeBlocked(startupCheck.reason,
                               QStringLiteral("insufficient_period_evidence"),
                               periodKeyFrames);
        }
        return makePassed(QStringLiteral("收到 %1 条 0x29 肯定响应，周期验证通过：%2；%3")
                              .arg(configs.size())
                              .arg(summaries.join(QStringLiteral("；")))
                              .arg(startupCheck.reason),
                           periodKeyFrames);
    }

    return makePassed(QStringLiteral("收到 %1 条 0x29 肯定响应，周期验证通过：%2。")
                          .arg(configs.size())
                          .arg(summaries.join(QStringLiteral("；"))),
                      keyFrames);
}

TestJudgeResult judgeInvalidControlRejected(const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << 0x207 << 0x2C0);
    QVector<EvidenceFrame> invalidCommands;
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId == 0x207 && !frame.bytes.isEmpty() &&
            (frame.bytes.at(0) == QStringLiteral("02") || frame.bytes.at(0) == QStringLiteral("FF"))) {
            invalidCommands.append(frame);
        }
    }
    if (!hasFrameFirstByte(invalidCommands, 0x207, QStringLiteral("02")) ||
        !hasFrameFirstByte(invalidCommands, 0x207, QStringLiteral("FF"))) {
        return makeBlocked(QStringLiteral("未同时发现 0x207 非法工作模式 0x02 和 0xFF 的发送证据。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    for (const EvidenceFrame &command : invalidCommands) {
        QDateTime nextCommandTime;
        for (const EvidenceFrame &frame : frames) {
            if (frame.canId == 0x207 && frame.timestamp.isValid() && command.timestamp.isValid() &&
                command.timestamp < frame.timestamp) {
                nextCommandTime = frame.timestamp;
                break;
            }
        }
        const QVector<EvidenceFrame> statusWindow = framesBetween(frames, command.timestamp, nextCommandTime, 0x2C0, 0);
        if (statusWindow.isEmpty()) {
            return makeBlocked(QStringLiteral("已发送非法 0x207=0x%1，但在恢复或下一条控制帧前未采集到 0x2C0 状态。")
                                   .arg(command.bytes.at(0)),
                               QStringLiteral("evidence_missing"),
                               keyFrames);
        }
        bool legalStatusFound = false;
        for (const EvidenceFrame &status : statusWindow) {
            if (status.bytes.isEmpty()) {
                continue;
            }
            if (status.bytes.at(0) == QStringLiteral("02") || status.bytes.at(0) == QStringLiteral("FF")) {
                return makeFailed(QStringLiteral("非法 0x207=0x%1 后，0x2C0 Byte1 跟随为非法工作模式 0x%2。")
                                      .arg(command.bytes.at(0), status.bytes.at(0)),
                                  QStringLiteral("expected_mismatch"),
                                  keyFrames);
            }
            if (status.bytes.at(0) == QStringLiteral("00") || status.bytes.at(0) == QStringLiteral("01")) {
                legalStatusFound = true;
            }
        }
        if (!legalStatusFound) {
            return makeBlocked(QStringLiteral("非法 0x207=0x%1 后采集到 0x2C0，但未看到合法工作模式 0x00/0x01。")
                                   .arg(command.bytes.at(0)),
                               QStringLiteral("expected_missing"),
                               keyFrames);
        }
    }
    return makePassed(QStringLiteral("非法 0x207 工作模式已发送，恢复前的 0x2C0 Byte1 均保持在合法值 0x00/0x01。"),
                      keyFrames);
}

TestJudgeResult judgeControlToggleFollowed(const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << 0x207 << 0x2C0);
    QVector<EvidenceFrame> commands;
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId == 0x207 && !isReceiveFrame(frame) && frame.bytes.size() == kClassicCanDlc &&
            (frame.bytes.at(0) == QStringLiteral("01") || frame.bytes.at(0) == QStringLiteral("00"))) {
            commands.append(frame);
        }
    }
    const int startCommands = countFrameFirstByte(commands, 0x207, QStringLiteral("01"));
    const int stopCommands = countFrameFirstByte(commands, 0x207, QStringLiteral("00"));
    if (startCommands < 10 || stopCommands < 10) {
        return makeBlocked(QStringLiteral("0x207 开始/停止 10 轮切换命令不完整：开始 %1 条，停止 %2 条。")
                               .arg(startCommands)
                               .arg(stopCommands),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }

    const int commandsToCheck = qMin(commands.size(), 20);
    QStringList failedRounds;
    QStringList latencies;
    qint64 totalLatencyMs = 0;
    for (int index = 0; index < commandsToCheck; ++index) {
        const EvidenceFrame command = commands.at(index);
        const QString expectedMode = command.bytes.at(0);
        const QDateTime endTime = (index + 1 < commands.size() && commands.at(index + 1).timestamp.isValid())
            ? commands.at(index + 1).timestamp
            : command.timestamp.addMSecs(700);
        EvidenceFrame matchedStatus;
        for (const EvidenceFrame &status : framesBetween(frames, command.timestamp, endTime, 0x2C0, 0)) {
            if (!status.bytes.isEmpty() && status.bytes.at(0) == expectedMode) {
                matchedStatus = status;
                break;
            }
        }
        if (!matchedStatus.timestamp.isValid()) {
            failedRounds.append(QStringLiteral("第%1条命令期望0x%2").arg(index + 1).arg(expectedMode));
            continue;
        }

        const qint64 latencyMs = command.timestamp.msecsTo(matchedStatus.timestamp);
        totalLatencyMs += latencyMs;
        latencies.append(QString::number(latencyMs));
        if (matchedStatus.bytes.size() >= 2 && matchedStatus.bytes.at(1) == QStringLiteral("01")) {
            QString tagDetail;
            const TagFragmentCheck tagCheck = checkRecognizedTagFragments(frames, matchedStatus, &tagDetail);
            if (tagCheck == TagFragmentCheck::Missing) {
                return makeBlocked(QStringLiteral("0x2C0 上报已识别 TAG，但同一状态轮次的 TAG 分片证据不足：%1。")
                                       .arg(tagDetail),
                                   QStringLiteral("tag_evidence_missing"), keyFrames);
            }
            if (tagCheck == TagFragmentCheck::Invalid) {
                return makeFailed(QStringLiteral("0x2C0 上报已识别 TAG，但同一状态轮次的 TAG 分片为占位值：%1。")
                                      .arg(tagDetail),
                                  QStringLiteral("tag_status_data_inconsistent"), keyFrames);
            }
        }
    }
    if (!failedRounds.isEmpty()) {
        return makeFailed(QStringLiteral("0x207 切换后 0x2C0 未逐次跟随：%1。").arg(failedRounds.join(QStringLiteral("；"))),
                          QStringLiteral("expected_mismatch"),
                          keyFrames);
    }
    return makePassed(QStringLiteral("0x207 开始/停止 10 轮切换完整，0x2C0 在每次命令后均跟随对应工作模式。首帧匹配时延（毫秒）：%1；平均 %2。")
                          .arg(latencies.join(QStringLiteral(", ")))
                          .arg(commandsToCheck > 0 ? totalLatencyMs / commandsToCheck : 0),
                      keyFrames);
}

TestJudgeResult judgeBroadcastStartAfterReboot(const TestCase &testCase,
                                               const QVector<EvidenceFrame> &frames)
{
    constexpr int kMaximumFirstBroadcastDelayMs = 200;
    const QList<int> ids = QList<int>() << kRfidRequestId << kRfidResponseId
        << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C3 << 0x2C4 << 0x2C5 << 0x2C6;
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, ids, 24);
    EvidenceFrame rebootRequest;
    if (!findFirstFrameData(frames, kRfidRequestId, QStringLiteral("0102"), &rebootRequest) ||
        !rebootRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到SID=0x02重启请求。"),
                           QStringLiteral("request_missing"), keyFrames);
    }
    EvidenceFrame rebootResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("0142"),
                                 rebootRequest.timestamp, &rebootResponse) ||
        !rebootResponse.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("重启请求后未采集到01 42肯定响应，无法建立首帧计时边界。"),
                           QStringLiteral("response_missing"), keyFrames);
    }

    QStringList delays;
    for (int canId = 0x2C0; canId <= 0x2C6; ++canId) {
        EvidenceFrame firstBroadcast;
        bool found = false;
        for (const EvidenceFrame &frame : framesAfter(frames, rebootResponse.timestamp, canId, -1)) {
            firstBroadcast = frame;
            found = true;
            break;
        }
        if (!found || !firstBroadcast.timestamp.isValid()) {
            return makeBlocked(QStringLiteral("重启肯定响应后未采集到%1首帧。").arg(idText(canId)),
                               QStringLiteral("broadcast_missing"), keyFrames);
        }
        const qint64 delayMs = rebootResponse.timestamp.msecsTo(firstBroadcast.timestamp);
        delays.append(QStringLiteral("%1=%2ms").arg(idText(canId)).arg(delayMs));
        if (delayMs < 0 || delayMs > kMaximumFirstBroadcastDelayMs) {
            return makeFailed(QStringLiteral("%1首帧时延%2ms，超过%3ms；各ID：%4。")
                                  .arg(idText(canId)).arg(delayMs)
                                  .arg(kMaximumFirstBroadcastDelayMs)
                                  .arg(delays.join(QStringLiteral("，"))),
                              QStringLiteral("first_broadcast_timeout"), keyFrames);
        }
    }
    return makePassed(QStringLiteral("以01 42重启肯定响应为T0，全部广播首帧均不超过200ms：%1。")
                          .arg(delays.join(QStringLiteral("，"))),
                      keyFrames);
}

TestJudgeResult judgeDefaultPeriodRestoreResponses(const TestCase &testCase,
                                                   const QVector<EvidenceFrame> &frames)
{
    const QList<int> keyIds = QList<int>() << kRfidRequestId << kRfidResponseId;
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, keyIds, 24);
    QStringList restoredIds;
    const int lastCanId = testCase.judgeTemplate == QStringLiteral("mt.period_restore_2c0_response")
        ? 0x2C0
        : 0x2C6;
    for (int canId = 0x2C0; canId <= lastCanId; ++canId) {
        const int periodMs = canId >= 0x2C3 && canId <= 0x2C5 ? 0x2710 : 0x0064;
        const QString body = QStringLiteral("%1%2")
            .arg(canId, 4, 16, QChar('0'))
            .arg(periodMs, 4, 16, QChar('0')).toUpper();
        const QString requestData = QStringLiteral("0529") + body;
        const QString responseData = QStringLiteral("0569") + body;
        EvidenceFrame restoreRequest;
        for (const EvidenceFrame &frame : framesById(frames, kRfidRequestId, false)) {
            if (frameHasData(frame, requestData)) {
                restoreRequest = frame;
            }
        }
        if (!restoreRequest.timestamp.isValid()) {
            return makeBlocked(QStringLiteral("默认周期恢复缺少%1请求：周期=0x%2。")
                                   .arg(idText(canId))
                                   .arg(periodMs, 4, 16, QChar('0')).toUpper(),
                               QStringLiteral("restore_request_missing"), keyFrames);
        }
        if (!findFirstFrameDataAfter(frames, kRfidResponseId, responseData,
                                     restoreRequest.timestamp, nullptr)) {
            return makeBlocked(QStringLiteral("默认周期恢复未收到%1对应的0x69肯定响应：周期=0x%2。")
                                   .arg(idText(canId))
                                   .arg(periodMs, 4, 16, QChar('0')).toUpper(),
                               QStringLiteral("restore_response_missing"), keyFrames);
        }
        restoredIds.append(idText(canId));
    }
    const QString summary = lastCanId == 0x2C0
        ? QStringLiteral("0x2C0默认周期恢复已确认0x69响应：100ms。")
        : QStringLiteral("默认周期恢复已逐条确认0x69响应：%1；0x2C0/1/2/6=100ms，0x2C3/4/5=10s。")
              .arg(restoredIds.join(QStringLiteral("、")));
    return makePassed(summary, keyFrames);
}

TestJudgeResult judgeScanPeriodRestore(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    QRegularExpression valueRegex(QStringLiteral("restore=([0-9A-Fa-f]{2})"));
    const QRegularExpressionMatch match = valueRegex.match(testCase.testData);
    if (!match.hasMatch()) {
        return makeBlocked(QStringLiteral("扫描周期恢复判定缺少期望值。"),
                           QStringLiteral("restore_value_missing"));
    }
    const QString periodHex = match.captured(1).toUpper();
    const QStringList keyFrames = keyFrameLinesForCase(
        testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C0, 16);
    EvidenceFrame restoreRequest;
    for (const EvidenceFrame &frame : framesById(frames, kRfidRequestId, false)) {
        if (frameHasData(frame, QStringLiteral("0201") + periodHex)) {
            restoreRequest = frame;
        }
    }
    if (!restoreRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到恢复扫描周期0x%1的SID=0x01请求。").arg(periodHex),
                           QStringLiteral("restore_request_missing"), keyFrames);
    }
    EvidenceFrame restoreResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("0241") + periodHex,
                                 restoreRequest.timestamp, &restoreResponse)) {
        return makeBlocked(QStringLiteral("恢复扫描周期0x%1后未收到对应0x41肯定响应。").arg(periodHex),
                           QStringLiteral("restore_response_missing"), keyFrames);
    }
    for (const EvidenceFrame &status : framesAfter(frames, restoreResponse.timestamp, 0x2C0, -1)) {
        if (status.bytes.size() >= 4 && status.bytes.at(3).compare(periodHex, Qt::CaseInsensitive) == 0) {
            return makePassed(QStringLiteral("扫描周期已恢复为0x%1，并由0x2C0回读确认。").arg(periodHex),
                              keyFrames);
        }
    }
    return makeBlocked(QStringLiteral("已收到扫描周期恢复响应，但未采集到0x2C0周期字段=0x%1。").arg(periodHex),
                       QStringLiteral("restore_readback_missing"), keyFrames);
}

TestJudgeResult judgeAppRestoreAfterTest(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(
        testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId, 16);
    EvidenceFrame restoreRequest;
    for (const EvidenceFrame &frame : framesById(frames, kRfidRequestId, false)) {
        if (frameHasData(frame, QStringLiteral("021001"))) {
            restoreRequest = frame;
        }
    }
    if (!restoreRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到结束阶段0x10 01跳转APP请求。"),
                           QStringLiteral("restore_request_missing"), keyFrames);
    }
    EvidenceFrame jumpResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("025001"),
                                 restoreRequest.timestamp, &jumpResponse)) {
        return makeBlocked(QStringLiteral("跳转APP恢复请求后未收到50 01肯定响应。"),
                           QStringLiteral("restore_response_missing"), keyFrames);
    }
    EvidenceFrame queryRequest;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("01A4"),
                                 jumpResponse.timestamp, &queryRequest) ||
        !findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("02E401"),
                                 queryRequest.timestamp, nullptr)) {
        return makeBlocked(QStringLiteral("跳转APP后未通过本次A4/E4 01确认结束状态。"),
                           QStringLiteral("restore_readback_missing"), keyFrames);
    }
    return makePassed(QStringLiteral("结束阶段已收到50 01，并通过A4/E4 01确认设备恢复APP。"), keyFrames);
}

TestJudgeResult judgeFaultControlToggleRepeated(const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << 0x207 << 0x2C0);
    QVector<EvidenceFrame> commands;
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId == 0x207 && !frame.bytes.isEmpty() &&
            (frame.bytes.at(0) == QStringLiteral("01") || frame.bytes.at(0) == QStringLiteral("00"))) {
            commands.append(frame);
        }
    }

    const int startCommands = countFrameFirstByte(commands, 0x207, QStringLiteral("01"));
    const int stopCommands = countFrameFirstByte(commands, 0x207, QStringLiteral("00"));
    if (startCommands < 3 || stopCommands < 3) {
        return makeBlocked(QStringLiteral("故障保持测试命令不完整：开始 %1 条，停止 %2 条；至少需要开始/停止各 3 条。")
                               .arg(startCommands)
                               .arg(stopCommands),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }

    const QStringList expectedSequence = QStringList()
        << QStringLiteral("01") << QStringLiteral("00")
        << QStringLiteral("01") << QStringLiteral("00")
        << QStringLiteral("01") << QStringLiteral("00");
    QVector<EvidenceFrame> testCommands;
    for (int start = 0; start + expectedSequence.size() <= commands.size(); ++start) {
        bool sequenceMatched = true;
        for (int offset = 0; offset < expectedSequence.size(); ++offset) {
            if (commands.at(start + offset).bytes.at(0) != expectedSequence.at(offset)) {
                sequenceMatched = false;
                break;
            }
        }
        if (sequenceMatched) {
            for (int offset = 0; offset < expectedSequence.size(); ++offset) {
                testCommands.append(commands.at(start + offset));
            }
            break;
        }
    }
    if (testCommands.size() != expectedSequence.size()) {
        return makeBlocked(QStringLiteral("未找到完整的 3 轮 0x207 开始/停止交替序列。"),
                           QStringLiteral("request_sequence_missing"),
                           keyFrames);
    }

    QStringList failedCommands;
    int checkedCommands = 0;
    bool hasFaultStatus = false;
    for (int index = 0; index < testCommands.size(); ++index) {
        const EvidenceFrame command = testCommands.at(index);
        const QString expectedMode = command.bytes.at(0);
        ++checkedCommands;
        QDateTime nextCommandTime;
        if (index + 1 < testCommands.size() && testCommands.at(index + 1).timestamp.isValid()) {
            nextCommandTime = testCommands.at(index + 1).timestamp;
        } else if (command.timestamp.isValid()) {
            nextCommandTime = command.timestamp.addMSecs(800);
        }

        bool matchedState = false;
        for (const EvidenceFrame &status : framesBetween(frames, command.timestamp, nextCommandTime, 0x2C0, 0)) {
            if (status.bytes.size() < 3) {
                continue;
            }
            if (status.bytes.at(2).compare(QStringLiteral("00"), Qt::CaseInsensitive) != 0) {
                hasFaultStatus = true;
            }
            if (status.bytes.at(0) == expectedMode &&
                status.bytes.at(1) == QStringLiteral("00")) {
                matchedState = true;
                break;
            }
        }
        if (!matchedState) {
            failedCommands.append(QStringLiteral("第%1条0x207=%2后未采集到 Byte1=%2/Byte2=00")
                                      .arg(checkedCommands)
                                      .arg(expectedMode));
        }
    }

    if (checkedCommands < 6) {
        return makeBlocked(QStringLiteral("开始/停止交替命令采样不足：仅检查到 %1 条有效命令。").arg(checkedCommands),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    if (!failedCommands.isEmpty()) {
        return makeFailed(QStringLiteral("故障期间 0x207 开始/停止切换后 0x2C0 未逐次跟随：%1。")
                              .arg(failedCommands.join(QStringLiteral("；"))),
                          QStringLiteral("expected_mismatch"),
                          keyFrames);
    }
    if (!hasFaultStatus) {
        return makeBlocked(QStringLiteral("控制切换证据完整，但故障阶段未采集到任何0x2C0 Byte3非0状态；请确认模块故障是否成功注入。"),
                           QStringLiteral("fault_injection_not_observed"), keyFrames);
    }

    const QDateTime lastCommandTime = testCommands.last().timestamp;
    const QVector<EvidenceFrame> recoveryStatuses = framesAfter(frames, lastCommandTime, 0x2C0, 0);
    int consecutiveNormal = 0;
    for (const EvidenceFrame &status : recoveryStatuses) {
        if (status.bytes.size() < 3) {
            continue;
        }
        if (status.bytes.at(2).compare(QStringLiteral("00"), Qt::CaseInsensitive) == 0) {
            ++consecutiveNormal;
            if (consecutiveNormal >= 3) {
                break;
            }
        } else {
            consecutiveNormal = 0;
        }
    }
    if (consecutiveNormal < 3) {
        return makeFailed(QStringLiteral("已观察到故障状态，但解除故障后未采集到连续3帧0x2C0 Byte3=00。"),
                          QStringLiteral("fault_recovery_failed"), keyFrames);
    }
    return makePassed(QStringLiteral("故障期间%1条控制命令均正确跟随，已观察Byte3非0故障状态，解除后连续3帧Byte3=00。")
                          .arg(checkedCommands), keyFrames);
}

TestJudgeResult judgeStoppedNoTag(const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << 0x207 << 0x2C0);
    EvidenceFrame stopCommand;
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId == 0x207 && !isReceiveFrame(frame) && frame.bytes.size() == kClassicCanDlc &&
            frame.bytes.at(0) == QStringLiteral("00")) {
            stopCommand = frame;
            break;
        }
    }
    if (!stopCommand.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未发现完整 DLC 的 0x207 停止检测命令。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }

    const int stabilizationMs = stopStabilizationMs(frames, stopCommand.timestamp);
    const QVector<EvidenceFrame> statuses = framesAfter(frames, stopCommand.timestamp, 0x2C0, stabilizationMs);
    if (statuses.size() < 3) {
        return makeBlocked(QStringLiteral("停止检测命令后稳定等待 %1ms，仅采集到 %2 条 0x2C0，无法完成连续3帧判定。")
                               .arg(stabilizationMs)
                               .arg(statuses.size()),
                           QStringLiteral("evidence_missing"), keyFrames);
    }

    int consecutiveCount = 0;
    for (const EvidenceFrame &status : statuses) {
        if (!statusHasStoppedNoTag(status)) {
            consecutiveCount = 0;
            continue;
        }

        const QList<int> tagIds = QList<int>() << 0x2C1 << 0x2C2 << 0x2C6;
        for (const int canId : tagIds) {
            const QVector<EvidenceFrame> tagSegments = tagSegmentsInStatusCycle(frames, status, canId);
            if (tagSegments.isEmpty()) {
                return makeBlocked(QStringLiteral("停止检测后的 0x2C0 无 TAG 状态缺少对应 %1 分片。")
                                       .arg(idText(canId)),
                                   QStringLiteral("tag_evidence_missing"), keyFrames);
            }
            for (const EvidenceFrame &segment : tagSegments) {
                if (!allBytesEqual(segment.bytes, 0, 7, QStringLiteral("30"))) {
                    return makeFailed(QStringLiteral("停止检测后的无 TAG 状态，%1 应全为 0x30，实际为 %2。")
                                          .arg(idText(canId), compactTokenText(segment.bytes)),
                                      QStringLiteral("tag_placeholder_mismatch"), keyFrames);
                }
            }
        }

        ++consecutiveCount;
        if (consecutiveCount >= 3) {
            return makePassed(QStringLiteral("停止检测后等待 %1ms，连续3帧 0x2C0 为停止/无TAG/无故障，且关联 TAG 分片均为 0x30。")
                                  .arg(stabilizationMs),
                              keyFrames);
        }
    }
    return makeFailed(QStringLiteral("停止检测稳定窗口内未获得连续3帧停止/无TAG/无故障的 0x2C0 状态。"),
                      QStringLiteral("expected_mismatch"),
                      keyFrames);
}

TestJudgeResult judgeStartedNoTag(const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << 0x207 << 0x2C0);
    if (!hasFrameFirstByte(frames, 0x207, QStringLiteral("01"))) {
        return makeBlocked(QStringLiteral("未发现 0x207 开始检测命令。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    for (const EvidenceFrame &frame : framesById(frames, 0x2C0, true)) {
        if (frame.bytes.size() >= 3 &&
            frame.bytes.at(0) == QStringLiteral("01") &&
            frame.bytes.at(1) == QStringLiteral("00") &&
            frame.bytes.at(2) == QStringLiteral("00")) {
            return makePassed(QStringLiteral("开始检测且无 TAG 场景下，0x2C0 Byte1=0x01、Byte2=0x00、Byte3=0x00。"),
                              keyFrames);
        }
    }
    return makeFailed(QStringLiteral("开始检测后，未发现 Byte1=0x01、Byte2=0x00、Byte3=0x00 的 0x2C0 状态帧。"),
                      QStringLiteral("expected_mismatch"),
                      keyFrames);
}

TestJudgeResult judgeDiag85Response(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    const bool enable = testCase.commandTemplate == QStringLiteral("mt.sid_0x85_diag_enable");
    const QString subFunction = enable ? QStringLiteral("01") : QStringLiteral("00");
    if (hasNegativeResponse(frames, QStringLiteral("85"))) {
        return makeFailed(QStringLiteral("SID=0x85 返回否定响应，功能用例失败。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }
    if (!evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("02C5") + subFunction, true)) {
        return makeBlocked(QStringLiteral("未发现 SID=0x85 肯定响应 02 C5 %1。").arg(subFunction),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    return makePassed(QStringLiteral("SID=0x85肯定响应与子功能0x%1一致；通信故障诊断的使能/抑制效果需结合故障注入人工观察。")
                          .arg(subFunction),
                      keyFrames);
}

TestJudgeResult judgeExpectedNegativeResponse(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C0);
    QString sid = testCase.expectedNegativeSid.trimmed().toUpper();
    if (sid.isEmpty()) {
        if (testCase.commandTemplate.contains(QStringLiteral("0x29"))) sid = QStringLiteral("29");
        else if (testCase.commandTemplate.contains(QStringLiteral("0x2e"), Qt::CaseInsensitive)) sid = QStringLiteral("2E");
        else if (testCase.commandTemplate.contains(QStringLiteral("0x01"))) sid = QStringLiteral("01");
        else sid = QStringLiteral("99");
    }
    int expectedCount = 1;
    if (testCase.commandTemplate == QStringLiteral("mt.sid_0x01_invalid_length")) {
        expectedCount = 2;
    } else if (testCase.commandTemplate == QStringLiteral("mt.sid_0x29_invalid_config")) {
        const QStringList requests = QStringList()
            << QStringLiteral("052903FF0005")
            << QStringLiteral("052902C00005")
            << QStringLiteral("032902C0");
        for (int index = 0; index < requests.size(); ++index) {
            EvidenceFrame request;
            if (!findFirstFrameData(frames, kRfidRequestId, requests.at(index), &request) ||
                !request.timestamp.isValid()) {
                return makeBlocked(QStringLiteral("DIAG-008第%1条非法0x29请求未采集完整。").arg(index + 1),
                                   QStringLiteral("request_missing"), keyFrames);
            }
            QDateTime windowEnd = latestFrameTimestamp(frames);
            if (index + 1 < requests.size()) {
                EvidenceFrame nextRequest;
                if (findFirstFrameDataAfter(frames, kRfidRequestId, requests.at(index + 1),
                                            request.timestamp, &nextRequest)) {
                    windowEnd = nextRequest.timestamp;
                }
            }
            EvidenceFrame matchedResponse;
            bool responseFound = false;
            for (const EvidenceFrame &response : framesBetween(
                     frames, request.timestamp, windowEnd, kRfidResponseId, 0)) {
                if (response.bytes.size() < 4 ||
                    response.bytes.at(1).compare(QStringLiteral("7F"), Qt::CaseInsensitive) != 0 ||
                    response.bytes.at(2).compare(QStringLiteral("29"), Qt::CaseInsensitive) != 0) {
                    continue;
                }
                matchedResponse = response;
                responseFound = true;
                break;
            }
            if (!responseFound) {
                return makeBlocked(QStringLiteral("DIAG-008第%1条非法0x29请求后、下一请求前未收到对应7F 29响应。")
                                       .arg(index + 1),
                                   QStringLiteral("response_missing"), keyFrames);
            }
            const QString nrc = matchedResponse.bytes.at(3).toUpper();
            if (!testCase.allowedNrc.isEmpty() && !testCase.allowedNrc.contains(nrc, Qt::CaseInsensitive)) {
                return makeFailed(QStringLiteral("DIAG-008第%1条非法请求返回NRC=0x%2，不在允许范围%3。")
                                      .arg(index + 1).arg(nrc, testCase.allowedNrc.join(QStringLiteral("、"))),
                                  QStringLiteral("unexpected_nrc"), keyFrames);
            }
        }
        return makePassed(QStringLiteral("DIAG-008三条非法0x29请求均在各自窗口内收到规范7F 29否定响应。"),
                          keyFrames);
    }
    QString badNrc;
    const int negativeCount = countNegativeResponses(frames, sid, testCase.allowedNrc, &badNrc);
    if (negativeCount < 0) {
        return makeFailed(QStringLiteral("收到 7F %1 否定响应，但 NRC=0x%2 不在允许列表：%3。")
                              .arg(sid, badNrc, testCase.allowedNrc.join(QStringLiteral(", "))),
                          QStringLiteral("unexpected_nrc"),
                          keyFrames);
    }
    if (negativeCount < expectedCount) {
        return makeBlocked(QStringLiteral("期望至少 %1 条 7F %2 否定响应，实际只采集到 %3 条。")
                               .arg(expectedCount)
                               .arg(sid)
                               .arg(negativeCount),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    EvidenceFrame negativeFrame;
    findNegativeResponse(frames, sid, &negativeFrame);
    const QString nrc = negativeFrame.bytes.size() >= 4 ? negativeFrame.bytes.at(3).toUpper() : QString();
    if (testCase.id == QStringLiteral("MT-RFID-SVC-004")) {
        EvidenceFrame firstInvalidRequest;
        EvidenceFrame lastInvalidRequest;
        for (const EvidenceFrame &request : framesById(frames, kRfidRequestId, false)) {
            if (request.bytes.size() < 2 ||
                request.bytes.at(1).compare(QStringLiteral("01"), Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (!firstInvalidRequest.timestamp.isValid()) {
                firstInvalidRequest = request;
            }
            lastInvalidRequest = request;
        }
        EvidenceFrame baselineStatus;
        for (const EvidenceFrame &status : framesById(frames, 0x2C0, true)) {
            if (status.timestamp < firstInvalidRequest.timestamp && status.bytes.size() >= 4) {
                baselineStatus = status;
            }
        }
        if (!baselineStatus.timestamp.isValid()) {
            return makeBlocked(QStringLiteral("非法SID=0x01请求前缺少0x2C0扫描周期基线。"),
                               QStringLiteral("baseline_missing"), keyFrames);
        }
        for (const EvidenceFrame &status : framesAfter(frames, lastInvalidRequest.timestamp, 0x2C0, -1)) {
            if (status.bytes.size() < 4) {
                continue;
            }
            if (status.bytes.at(3).compare(baselineStatus.bytes.at(3), Qt::CaseInsensitive) != 0) {
                return makeFailed(QStringLiteral("非法长度请求后扫描周期发生变化：基线=0x%1，实际=0x%2。")
                                      .arg(baselineStatus.bytes.at(3), status.bytes.at(3)),
                                  QStringLiteral("baseline_changed"), keyFrames);
            }
            return makePassed(QStringLiteral("两条非法长度请求均收到规范否定响应，且扫描周期保持基线0x%1不变。")
                                  .arg(baselineStatus.bytes.at(3)), keyFrames);
        }
        return makeBlocked(QStringLiteral("非法长度请求后缺少0x2C0回读，无法确认原周期未改变。"),
                           QStringLiteral("readback_missing"), keyFrames);
    }
    return makePassed(QStringLiteral("已采集到 %1 条符合预期的 7F %2 否定响应，首条 NRC=0x%3。")
                          .arg(negativeCount)
                          .arg(sid, nrc),
                      keyFrames);
}

TestJudgeResult judgeShortDlcSafeIgnore(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames,
                                                        QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C0);
    QSet<int> shortDlcs;
    QDateTime firstShortRequestTime;
    QDateTime lastShortRequestTime;
    for (const EvidenceFrame &frame : framesById(frames, kRfidRequestId, false)) {
        if (isReceiveFrame(frame) || frame.bytes.size() >= kClassicCanDlc || !frame.timestamp.isValid()) {
            continue;
        }
        shortDlcs.insert(frame.bytes.size());
        if (!firstShortRequestTime.isValid()) {
            firstShortRequestTime = frame.timestamp;
        }
        lastShortRequestTime = frame.timestamp;
    }
    if (!shortDlcs.contains(2) || !shortDlcs.contains(4) || !shortDlcs.contains(7)) {
        return makeBlocked(QStringLiteral("未完整采集主控 0x007 DLC=2/4/7 截断请求帧，无法判定短 DLC 处理。"),
                           QStringLiteral("request_missing"), keyFrames);
    }

    EvidenceFrame recoveryRequest;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("020132"),
                                 lastShortRequestTime, &recoveryRequest) ||
        !recoveryRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("短 DLC 帧后未发送合法 SID=0x01、周期=0x32 恢复请求。"),
                           QStringLiteral("recovery_request_missing"), keyFrames);
    }

    for (const EvidenceFrame &response : framesById(frames, kRfidResponseId, true)) {
        if (response.timestamp > firstShortRequestTime && response.timestamp < recoveryRequest.timestamp) {
            return makeFailed(QStringLiteral("0x007 短 DLC 帧后、恢复请求前收到 0x107 响应，未按要求静默丢弃：%1。")
                                  .arg(response.line),
                              QStringLiteral("unexpected_response"), keyFrames);
        }
    }

    EvidenceFrame positiveResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("024132"),
                                 recoveryRequest.timestamp, &positiveResponse)) {
        return makeBlocked(QStringLiteral("合法 SID=0x01 恢复请求后未收到 0x107=02 41 32 肯定响应。"),
                           QStringLiteral("recovery_response_missing"), keyFrames);
    }
    for (const EvidenceFrame &status : framesAfter(frames, positiveResponse.timestamp, 0x2C0, 0)) {
        if (status.bytes.size() >= 4 && status.bytes.at(3) == QStringLiteral("32")) {
            return makePassed(QStringLiteral("0x007 DLC=2/4/7 截断请求均被静默丢弃；合法 SID=0x01 后收到 02 41 32，且 0x2C0 扫描周期更新为 0x32。"),
                              keyFrames);
        }
    }
    return makeBlocked(QStringLiteral("已收到 02 41 32，但未采集到扫描周期字段为 0x32 的后续 0x2C0。"),
                       QStringLiteral("broadcast_missing"), keyFrames);
}

TestJudgeResult judgeControlReservedPaddingIgnore(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << 0x207 << 0x2C0);
    EvidenceFrame malformed;
    if (!findFirstFrameData(frames, 0x207, QStringLiteral("0199"), &malformed) || !malformed.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到保留字节非0x55的0x207异常帧。"),
                           QStringLiteral("request_missing"), keyFrames);
    }
    bool stoppedBaselineObserved = false;
    for (const EvidenceFrame &status : frames) {
        if (status.timestamp >= malformed.timestamp || status.canId != 0x2C0 || !isReceiveFrame(status) ||
            status.bytes.isEmpty()) {
            continue;
        }
        if (status.bytes.first() == QStringLiteral("00")) {
            stoppedBaselineObserved = true;
        }
    }
    if (!stoppedBaselineObserved) {
        return makeBlocked(QStringLiteral("保留字节错误帧前未采集到0x2C0 Byte1=0x00的停止检测基线。"),
                           QStringLiteral("baseline_missing"), keyFrames);
    }

    EvidenceFrame nextControl;
    for (const EvidenceFrame &frame : frames) {
        if (frame.timestamp > malformed.timestamp && frame.canId == 0x207 && !isReceiveFrame(frame)) {
            nextControl = frame;
            break;
        }
    }
    const QDateTime observationEndTime = nextControl.timestamp.isValid()
        ? nextControl.timestamp
        : latestFrameTimestamp(frames);
    for (const EvidenceFrame &status : framesBetween(frames, malformed.timestamp, observationEndTime, 0x2C0, 0)) {
        if (!status.bytes.isEmpty() && status.bytes.first() == QStringLiteral("01")) {
            return makePassed(QStringLiteral("保留字节错误的0x207按合法 Byte1=0x01 执行：停止检测基线后，0x2C0 已切换为开始检测。0x107错误填充补充项不计入结果。"),
                              keyFrames);
        }
    }
    return makeFailed(QStringLiteral("保留字节错误的0x207后未采集到0x2C0 Byte1=0x01；终端未按合法 Byte1=0x01 执行。"),
                      QStringLiteral("reserved_padding_execution_missing"), keyFrames);
}

TestJudgeResult judgeDeviceIdPrefix(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << 0x2C4 << 0x2C5);
    const QString deviceId = collectedDeviceId(frames);
    if (deviceId.isEmpty()) {
        return testCase.allowEmptyValue
            ? makeBlocked(QStringLiteral("已采集 0x2C4/0x2C5，但设备 ID 内容为空，请确认样机是否已写入设备 ID。"),
                          QStringLiteral("manual_confirmation_required"),
                          keyFrames)
            : makeFailed(QStringLiteral("设备 ID 为空，不满足该用例要求。"),
                         QStringLiteral("expected_mismatch"),
                         keyFrames);
    }
    if (!testCase.requiredPrefix.trimmed().isEmpty() &&
        !deviceId.startsWith(testCase.requiredPrefix.trimmed(), Qt::CaseInsensitive)) {
        return makeFailed(QStringLiteral("设备 ID=%1，未匹配期望前缀 %2。")
                              .arg(deviceId, testCase.requiredPrefix.trimmed()),
                          QStringLiteral("expected_mismatch"),
                          keyFrames);
    }
    const QString caseText = startupCheckText(testCase);
    if (requiresStartupBroadcastPeriodCheck(caseText)) {
        const QList<int> startupIds = startupPeriodTargetIds(caseText);
        const PeriodWindowCheck startupCheck = checkStartupBroadcastPeriod(frames, startupIds, caseText);
        if (!startupCheck.passed) {
            return makeBlocked(startupCheck.reason,
                               QStringLiteral("insufficient_period_evidence"),
                               keyFrameLinesForCase(testCase, frames, startupIds, 24));
        }
        return makePassed(QStringLiteral("已采集设备 ID=%1，内容非空且满足前缀要求；%2")
                              .arg(deviceId, startupCheck.reason),
                          keyFrameLinesForCase(testCase, frames, startupIds, 24));
    }
    return makePassed(QStringLiteral("已采集设备 ID=%1，内容非空且满足前缀要求。").arg(deviceId),
                      keyFrames);
}

TestJudgeResult judgeControl207TimeoutFault(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << 0x207 << 0x2C0);
    const QVector<EvidenceFrame> statuses = framesById(frames, 0x2C0, true);
    bool baselineNormal = false;
    EvidenceFrame faultStatus;
    for (const EvidenceFrame &status : statuses) {
        if (status.bytes.size() < 3) {
            continue;
        }
        const QString fault = status.bytes.at(2).toUpper();
        if (!faultStatus.timestamp.isValid() && fault == QStringLiteral("00")) {
            baselineNormal = true;
        } else if (baselineNormal && fault == QStringLiteral("02")) {
            faultStatus = status;
            break;
        }
    }
    if (!baselineNormal) {
        return makeBlocked(QStringLiteral("暂停 0x207 前未采集到 Byte3=0x00 基线状态，无法确认故障转换。"),
                           QStringLiteral("baseline_missing"), keyFrames);
    }
    if (!faultStatus.timestamp.isValid()) {
        return statuses.isEmpty()
            ? makeBlocked(QStringLiteral("暂停 0x207 后未采集到 0x2C0 状态帧，请确认广播是否开启。"),
                          QStringLiteral("evidence_missing"), keyFrames)
            : makeFailed(QStringLiteral("暂停 0x207 后未采集到 Byte3=0x02 通信异常状态。"),
                         QStringLiteral("expected_mismatch"), keyFrames);
    }

    EvidenceFrame restoreCommand;
    for (const EvidenceFrame &frame : frames) {
        if (frame.canId == 0x207 && !isReceiveFrame(frame) && frame.bytes.size() == kClassicCanDlc &&
            !frame.bytes.isEmpty() && faultStatus.timestamp < frame.timestamp &&
            (frame.bytes.at(0) == QStringLiteral("00") || frame.bytes.at(0) == QStringLiteral("01"))) {
            restoreCommand = frame;
            break;
        }
    }
    if (!restoreCommand.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("通信异常后未采集到恢复用的完整 DLC 0x207 命令。"),
                           QStringLiteral("recovery_request_missing"), keyFrames);
    }

    const QString expectedMode = restoreCommand.bytes.at(0);
    int recoveredNormalFrames = 0;
    int longestRecoveredNormalFrames = 0;
    for (const EvidenceFrame &status : framesAfter(frames, restoreCommand.timestamp, 0x2C0)) {
        if (status.bytes.size() < 3) {
            continue;
        }
        const bool recovered = status.bytes.at(2).compare(QStringLiteral("00"), Qt::CaseInsensitive) == 0;
        const bool modeMatched = !status.bytes.isEmpty() && status.bytes.at(0) == expectedMode;
        if (recovered && modeMatched) {
            ++recoveredNormalFrames;
            longestRecoveredNormalFrames = qMax(longestRecoveredNormalFrames, recoveredNormalFrames);
            if (recoveredNormalFrames >= 3) {
                return makePassed(QStringLiteral("已验证 Byte3 故障恢复序列 0x00→0x02→连续3帧0x00；恢复后的工作模式与 0x207=0x%1 一致。")
                                      .arg(expectedMode),
                                  keyFrames);
            }
        } else {
            recoveredNormalFrames = 0;
        }
    }
    return makeFailed(QStringLiteral("恢复 0x207 后未采集到连续3帧 Byte3=0x00 且工作模式匹配的 0x2C0（最长连续 %1 帧）。")
                          .arg(longestRecoveredNormalFrames),
                      QStringLiteral("recovery_incomplete"), keyFrames);
}

TestJudgeResult judgeIsoTpMultiframeFlow(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    EvidenceFrame firstFrame;
    int firstFrameIndex = -1;
    for (int index = 0; index < frames.size(); ++index) {
        const EvidenceFrame &frame = frames.at(index);
        if (frame.canId == kRfidRequestId && !isReceiveFrame(frame) && frame.bytes.size() == kClassicCanDlc &&
            !frame.bytes.isEmpty() && (hexByteValue(frame.bytes.first()) >> 4) == 0x1 &&
            frameHasData(frame, QStringLiteral("2EE7E1"))) {
            firstFrame = frame;
            firstFrameIndex = index;
            break;
        }
    }
    if (firstFrameIndex < 0) {
        return makeBlocked(QStringLiteral("未采集到发送方向的 ISO-TP 首帧 FF（DID=E7E1）。"),
                           QStringLiteral("isotp_first_frame_missing"), keyFrames);
    }

    const int firstPci = hexByteValue(firstFrame.bytes.at(0));
    const int payloadLength = ((firstPci & 0x0F) << 8) | hexByteValue(firstFrame.bytes.at(1));
    if (payloadLength < 8) {
        return makeFailed(QStringLiteral("ISO-TP 首帧声明长度 %1，小于多帧传输最小长度8。").arg(payloadLength),
                          QStringLiteral("isotp_length_invalid"), keyFrames);
    }

    EvidenceFrame flowControl;
    int flowControlIndex = -1;
    for (int index = firstFrameIndex + 1; index < frames.size(); ++index) {
        const EvidenceFrame &frame = frames.at(index);
        if (frame.canId == kRfidResponseId && isReceiveFrame(frame) && frame.bytes.size() == kClassicCanDlc &&
            !frame.bytes.isEmpty() && (hexByteValue(frame.bytes.first()) >> 4) == 0x3) {
            flowControl = frame;
            flowControlIndex = index;
            break;
        }
    }
    if (flowControlIndex < 0) {
        return makeBlocked(QStringLiteral("首帧 FF 后未采集到接收方向的流控 FC。"),
                           QStringLiteral("isotp_flow_control_missing"), keyFrames);
    }
    if (firstFrame.timestamp.isValid() && flowControl.timestamp.isValid() &&
        firstFrame.timestamp.msecsTo(flowControl.timestamp) > kRequestResponseTimeoutMs) {
        return makeFailed(QStringLiteral("首帧 FF 后 %1ms 才收到 FC，超过3秒。")
                              .arg(firstFrame.timestamp.msecsTo(flowControl.timestamp)),
                          QStringLiteral("isotp_flow_control_timeout"), keyFrames);
    }

    const int flowStatus = hexByteValue(flowControl.bytes.at(0)) & 0x0F;
    const int blockSize = hexByteValue(flowControl.bytes.at(1));
    const int stMin = hexByteValue(flowControl.bytes.at(2));
    if (flowStatus != 0x0) {
        return makeFailed(QStringLiteral("FC 流控状态不是 CTS(0x00)。"),
                          QStringLiteral("isotp_flow_control_invalid"), keyFrames);
    }
    if (stMin > 0x7F && (stMin < 0xF1 || stMin > 0xF9)) {
        return makeFailed(QStringLiteral("FC 的 STmin=0x%1 非法。")
                              .arg(stMin, 2, 16, QChar('0')).toUpper(),
                          QStringLiteral("isotp_stmin_invalid"), keyFrames);
    }

    int remainingBytes = payloadLength - 6;
    int expectedSequence = 1;
    int framesInBlock = 0;
    EvidenceFrame previousCf;
    for (int index = flowControlIndex + 1; index < frames.size() && remainingBytes > 0; ++index) {
        const EvidenceFrame &frame = frames.at(index);
        if (frame.canId != kRfidRequestId || isReceiveFrame(frame) || frame.bytes.size() != kClassicCanDlc ||
            frame.bytes.isEmpty() || (hexByteValue(frame.bytes.first()) >> 4) != 0x2) continue;
        const int sequence = hexByteValue(frame.bytes.first()) & 0x0F;
        if (sequence != (expectedSequence & 0x0F)) {
            return makeFailed(QStringLiteral("连续帧序号错误：期望 0x%1，实际 0x%2。")
                                  .arg(expectedSequence & 0x0F, 1, 16, QChar('0'))
                                  .arg(sequence, 1, 16, QChar('0')).toUpper(),
                              QStringLiteral("isotp_sequence_error"), keyFrames);
        }
        if (blockSize != 0 && framesInBlock >= blockSize) {
            return makeFailed(QStringLiteral("FC 块大小为 %1，但未收到下一条 FC 前已超出该数量的连续帧。").arg(blockSize),
                              QStringLiteral("isotp_block_size_exceeded"), keyFrames);
        }
        if (stMin <= 0x7F && previousCf.timestamp.isValid() && frame.timestamp.isValid() &&
            previousCf.timestamp.msecsTo(frame.timestamp) < stMin) {
            return makeFailed(QStringLiteral("连续帧间隔小于 FC 要求的 STmin=%1ms。").arg(stMin),
                              QStringLiteral("isotp_stmin_violation"), keyFrames);
        }
        const int copiedBytes = qMin(7, remainingBytes);
        if (copiedBytes < 7 && !allBytesEqual(frame.bytes, copiedBytes + 1, 7, QStringLiteral("55"))) {
            return makeFailed(QStringLiteral("最后一条 ISO-TP 连续帧的未使用字节不是 0x55 填充。"),
                              QStringLiteral("isotp_padding_invalid"), keyFrames);
        }
        remainingBytes -= copiedBytes; ++expectedSequence; ++framesInBlock; previousCf = frame;
    }
    if (remainingBytes > 0) {
        return makeBlocked(QStringLiteral("ISO-TP 连续帧不足，尚缺少 %1 个有效数据字节。").arg(remainingBytes),
                           QStringLiteral("isotp_consecutive_frame_missing"), keyFrames);
    }
    for (int index = firstFrameIndex + 1; index < frames.size(); ++index) {
        const EvidenceFrame &frame = frames.at(index);
        if (frame.canId == kRfidResponseId && isReceiveFrame(frame) &&
            (frameHasData(frame, QStringLiteral("6EE7E1")) || frameHasData(frame, QStringLiteral("7F2E")))) {
            return makePassed(QStringLiteral("ISO-TP 多帧传输通过：FF长度=%1，FC块大小=%2，STmin=0x%3，CF序号和尾部填充均正确。")
                                  .arg(payloadLength).arg(blockSize).arg(stMin, 2, 16, QChar('0')).toUpper(),
                              keyFrames);
        }
    }
    return makeBlocked(QStringLiteral("多帧数据已完整发送，但未采集到最终肯定或否定响应。"),
                       QStringLiteral("isotp_final_response_missing"), keyFrames);
}

TestJudgeResult judgeDiag10JumpAndQuery(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    const bool jumpBoot = testCase.commandTemplate.contains(QStringLiteral("jump_boot"));
    const QString subFunction = jumpBoot ? QStringLiteral("02") : QStringLiteral("01");
    const QString expectedLocation = jumpBoot ? QStringLiteral("00") : QStringLiteral("01");
    EvidenceFrame jumpRequest;
    if (!findFirstFrameData(frames, kRfidRequestId, QStringLiteral("0210") + subFunction, &jumpRequest) ||
        !jumpRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到0x10 %1跳转请求。").arg(subFunction),
                           QStringLiteral("request_missing"), keyFrames);
    }
    if (hasNegativeResponse(framesAfter(frames, jumpRequest.timestamp, kRfidResponseId, 0), QStringLiteral("10"))) {
        return makeFailed(QStringLiteral("跳转 APP/BOOT 服务返回否定响应。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }
    EvidenceFrame jumpResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("0250") + subFunction,
                                 jumpRequest.timestamp, &jumpResponse)) {
        return makeBlocked(QStringLiteral("未采集到本次0x10 %1请求对应的02 50 %1肯定响应。").arg(subFunction),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    EvidenceFrame queryRequest;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("01A4"),
                                 jumpResponse.timestamp, &queryRequest)) {
        return makeBlocked(QStringLiteral("跳转肯定响应后未采集到A4程序位置查询。"),
                           QStringLiteral("query_missing"), keyFrames);
    }
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("02E4") + expectedLocation,
                                 queryRequest.timestamp, nullptr)) {
        return makeBlocked(QStringLiteral("A4查询后未采集到期望E4 %1程序位置响应。").arg(expectedLocation),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    return makePassed(QStringLiteral("已严格关联10 %1→50 %1→A4→E4 %2，程序位置正确。")
                          .arg(subFunction, expectedLocation), keyFrames);
}

TestJudgeResult judgeBootResetAppRecovery(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QList<int> keyIds = QList<int>() << kRfidRequestId << kRfidResponseId
                                             << kRfidBroadcastFirstId << kRfidBroadcastLastId;
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, keyIds, 24);
    EvidenceFrame bootJumpRequest;
    if (!findFirstFrameData(frames, kRfidRequestId, QStringLiteral("021002"), &bootJumpRequest) ||
        !bootJumpRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到跳转 BOOT 请求 02 10 02。"),
                           QStringLiteral("boot_jump_missing"),
                           keyFrames);
    }

    EvidenceFrame bootQueryRequest;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("01A4"), bootJumpRequest.timestamp, &bootQueryRequest) ||
        !bootQueryRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("跳转 BOOT 后未采集到 0xA4 程序位置查询请求。"),
                           QStringLiteral("boot_query_missing"),
                           keyFrames);
    }
    EvidenceFrame bootResponse;
    if (!findFrameDataAfter(frames, kRfidResponseId, QStringLiteral("E400"), bootQueryRequest.timestamp, &bootResponse) ||
        !bootResponse.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("跳转 BOOT 后未采集到 E4 00 程序位置响应。"),
                           QStringLiteral("boot_evidence_missing"),
                           keyFrames);
    }

    EvidenceFrame resetRequest;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("0111"), bootResponse.timestamp, &resetRequest) ||
        !resetRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("确认 BOOT 后未采集到 SID=0x11 软件复位请求。"),
                           QStringLiteral("reset_missing"),
                           keyFrames);
    }
    EvidenceFrame appQueryRequest;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("01A4"), resetRequest.timestamp, &appQueryRequest) ||
        !appQueryRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("软件复位后未采集到 0xA4 程序位置查询请求。"),
                           QStringLiteral("app_query_missing"),
                           keyFrames);
    }
    if (!findFrameDataAfter(frames, kRfidResponseId, QStringLiteral("E401"), appQueryRequest.timestamp, nullptr)) {
        return makeBlocked(QStringLiteral("软件复位后未采集到 E4 01 APP 程序位置响应。"),
                           QStringLiteral("app_evidence_missing"),
                           keyFrames);
    }

    bool hasBroadcastAfterReset = false;
    for (int canId = kRfidBroadcastFirstId; canId <= kRfidBroadcastLastId; ++canId) {
        if (!framesAfter(frames, resetRequest.timestamp, canId).isEmpty()) {
            hasBroadcastAfterReset = true;
            break;
        }
    }
    if (!hasBroadcastAfterReset) {
        return makeBlocked(QStringLiteral("软件复位后未采集到 0x2C0~0x2C6 周期广播帧。"),
                           QStringLiteral("broadcast_evidence_missing"),
                           keyFrames);
    }

    return makePassed(QStringLiteral("已确认设备进入 BOOT，SID=0x11 软件复位后返回 APP，且周期广播恢复正常。"),
                      keyFrames);
}

QDateTime executionEventTime(const QString &evidenceText, const QString &eventText)
{
    QDateTime matchedTime;
    const QStringList lines = evidenceText.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!line.contains(eventText) || line.size() < 23) {
            continue;
        }
        const QDateTime timestamp = QDateTime::fromString(line.left(23), QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        if (timestamp.isValid()) {
            matchedTime = timestamp;
        }
    }
    return matchedTime;
}

QVector<int> writeDataForDid(const QVector<EvidenceFrame> &frames, quint16 did)
{
    const QVector<QVector<int>> payloads = collectIsoTpPayloads(frames, kRfidRequestId, 0x2E);
    for (const QVector<int> &payload : payloads) {
        if (payload.size() >= 4 && payload.at(1) == ((did >> 8) & 0xFF) && payload.at(2) == (did & 0xFF)) {
            return payload.mid(3);
        }
    }
    return QVector<int>();
}

QVector<QVector<int>> writeDataValuesForDid(const QVector<EvidenceFrame> &frames, quint16 did)
{
    QVector<QVector<int>> values;
    const QVector<QVector<int>> payloads = collectIsoTpPayloads(frames, kRfidRequestId, 0x2E);
    for (const QVector<int> &payload : payloads) {
        if (payload.size() >= 4 &&
            payload.at(1) == ((did >> 8) & 0xFF) &&
            payload.at(2) == (did & 0xFF)) {
            values.append(payload.mid(3));
        }
    }
    return values;
}

TestJudgeResult judgeNvmInvalidWriteUnchanged(const TestCase &testCase,
                                              const QVector<EvidenceFrame> &frames,
                                              const QString &evidenceText)
{
    const TestJudgeResult negativeResult = judgeExpectedNegativeResponse(testCase, frames);
    if (negativeResult.status != TestResultStatus::Passed) {
        return negativeResult;
    }
    const QStringList keyFrames = keyFrameLinesForCase(
        testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C3 << 0x2C4 << 0x2C5, 24);
    EvidenceFrame rebootResponse;
    for (const EvidenceFrame &frame : framesById(frames, kRfidResponseId, true)) {
        if (frameHasData(frame, QStringLiteral("0142"))) {
            rebootResponse = frame;
        }
    }
    if (!rebootResponse.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("非法写入后未采集到SID=0x02复位响应，无法读取NVM保持结果。"),
                           QStringLiteral("reboot_evidence_missing"), keyFrames);
    }
    const QVector<EvidenceFrame> readbackFrames = framesAfterAnyId(
        frames, rebootResponse.timestamp, QList<int>() << 0x2C3 << 0x2C4 << 0x2C5, -1);
    const QRegularExpression deviceIdRegex(
        QStringLiteral("NVM非法写基线[^\\r\\n]*设备ID=([^\\r\\n；]+)"));
    const QRegularExpressionMatch deviceIdMatch = deviceIdRegex.match(evidenceText);
    if (!deviceIdMatch.hasMatch()) {
        return makeBlocked(QStringLiteral("缺少非法写入前设备ID基线。"),
                           QStringLiteral("baseline_missing"), keyFrames);
    }
    const QString expectedDeviceId = deviceIdMatch.captured(1).trimmed();
    const QString actualDeviceId = collectedDeviceId(readbackFrames);
    if (actualDeviceId != expectedDeviceId) {
        return makeFailed(QStringLiteral("非法写入后设备ID发生变化：基线=%1，实际=%2。")
                              .arg(expectedDeviceId, actualDeviceId.isEmpty() ? QStringLiteral("空") : actualDeviceId),
                          QStringLiteral("nvm_changed"), keyFrames);
    }
    if (testCase.id == QStringLiteral("MT-RFID-NVM-004")) {
        const QRegularExpression versionRegex(
            QStringLiteral("NVM非法写基线[^\\r\\n]*HW=([0-9A-Fa-f]{4})；SW=([0-9A-Fa-f]{4})"));
        const QRegularExpressionMatch versionMatch = versionRegex.match(evidenceText);
        const QVector<EvidenceFrame> versionFrames = framesById(readbackFrames, 0x2C3, true);
        if (!versionMatch.hasMatch() || versionFrames.isEmpty() || versionFrames.first().bytes.size() < 6) {
            return makeBlocked(QStringLiteral("非法DID写入前后缺少完整版本基线或0x2C3回读。"),
                               QStringLiteral("version_readback_missing"), keyFrames);
        }
        const EvidenceFrame &versionFrame = versionFrames.first();
        const QString actualHardware = (versionFrame.bytes.at(2) + versionFrame.bytes.at(3)).toUpper();
        const QString actualSoftware = (versionFrame.bytes.at(4) + versionFrame.bytes.at(5)).toUpper();
        if (actualHardware != versionMatch.captured(1).toUpper() ||
            actualSoftware != versionMatch.captured(2).toUpper()) {
            return makeFailed(QStringLiteral("非法DID写入后版本字段发生变化：HW %1→%2，SW %3→%4。")
                                  .arg(versionMatch.captured(1).toUpper(), actualHardware,
                                       versionMatch.captured(2).toUpper(), actualSoftware),
                              QStringLiteral("nvm_changed"), keyFrames);
        }
    }
    return makePassed(QStringLiteral("非法0x2E请求收到规范否定响应；复位后版本/设备ID与写入前基线一致，NVM未改变。"),
                      keyFrames);
}

TestJudgeResult judgeNvmWriteAndVerify(const TestCase &testCase,
                                       const QVector<EvidenceFrame> &frames,
                                       const QString &evidenceText)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C3 << 0x2C4 << 0x2C5);
    if (hasNegativeResponse(frames, QStringLiteral("2E"))) {
        return makeFailed(QStringLiteral("非易失写入返回否定响应。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }

    const bool verifyDeviceId = testCase.judgeTemplate.contains(QStringLiteral("device_id")) ||
                                testCase.judgeTemplate.contains(QStringLiteral("sn"));
    const QString didLowByte = verifyDeviceId ? QStringLiteral("E1") : QStringLiteral("E0");
    EvidenceFrame writeResponse;
    if (!findWritePositiveResponse(frames, didLowByte, &writeResponse)) {
        return makeBlocked(QStringLiteral("未采集到 DID=0xE7%1 的 0x2E 写入肯定响应 03 6E E7 %1。").arg(didLowByte),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    if (!writeResponse.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("写入肯定响应时间戳无效，无法限定回读证据窗口。"),
                           QStringLiteral("timestamp_invalid"),
                           keyFrames);
    }

    const quint16 did = verifyDeviceId ? 0xE7E1 : 0xE7E0;
    const QVector<int> expectedValue = writeDataForDid(frames, did);
    const int expectedLength = verifyDeviceId ? 16 : 2;
    if (expectedValue.size() != expectedLength) {
        return makeBlocked(QStringLiteral("未能从本次 0x2E 请求重组 DID=0x%1 的完整 %2 字节写入值。")
                               .arg(did, 4, 16, QChar('0')).toUpper()
                               .arg(expectedLength),
                           QStringLiteral("request_value_missing"),
                           keyFrames);
    }

    const QDateTime powerOnTime = executionEventTime(evidenceText, QStringLiteral("已确认 NVM 写入后终端重新上电"));
    if (!powerOnTime.isValid()) {
        return makeBlocked(QStringLiteral("未找到真实断电后重新上电的确认事件，不能以软件重启代替 NVM 掉电保持验证。"),
                           QStringLiteral("power_cycle_not_confirmed"),
                           keyFrames);
    }
    const QDateTime readbackStart = powerOnTime > writeResponse.timestamp ? powerOnTime : writeResponse.timestamp;

    if (testCase.judgeTemplate == QStringLiteral("mt.nvm_sn_reboot_verify")) {
        const QVector<EvidenceFrame> powerOnFrames =
            framesAfterAnyId(frames, readbackStart, QList<int>() << 0x2C4 << 0x2C5, -1);
        const QString actualDeviceId = collectedDeviceId(powerOnFrames);
        QByteArray expectedBytes;
        for (const int value : expectedValue) {
            expectedBytes.append(static_cast<char>(value));
        }
        const QString expectedDeviceId = QString::fromLatin1(expectedBytes);
        if (actualDeviceId.size() != expectedLength) {
            return makeBlocked(QStringLiteral("真实上电后未采集到完整16字节设备ID，实际=%1。")
                                   .arg(actualDeviceId.isEmpty() ? QStringLiteral("空") : actualDeviceId),
                               QStringLiteral("readback_missing"), keyFrames);
        }
        if (actualDeviceId != expectedDeviceId) {
            return makeFailed(QStringLiteral("真实断电上电后设备ID不一致：写入=%1，回读=%2。")
                                  .arg(expectedDeviceId, actualDeviceId),
                              QStringLiteral("persistent_value_mismatch"), keyFrames);
        }
        return makePassed(QStringLiteral("本次SN写入响应正确，真实断电上电后完整设备ID仍为 %1。").arg(actualDeviceId),
                          keyFrames);
    }

    if (verifyDeviceId) {
        const QVector<EvidenceFrame> readbackFrames =
            framesAfterAnyId(frames, readbackStart, QList<int>() << 0x2C4 << 0x2C5, -1);
        const QString actualDeviceId = collectedDeviceId(readbackFrames);
        QByteArray expectedBytes;
        for (const int value : expectedValue) {
            expectedBytes.append(static_cast<char>(value));
        }
        const QString expectedDeviceId = QString::fromLatin1(expectedBytes);
        if (actualDeviceId.size() != expectedLength) {
            return makeBlocked(QStringLiteral("真实上电后0x2C4/0x2C5未形成完整16字节设备ID，实际=%1。")
                                   .arg(actualDeviceId.isEmpty() ? QStringLiteral("空") : actualDeviceId),
                               QStringLiteral("readback_missing"), keyFrames);
        }
        if (actualDeviceId != expectedDeviceId) {
            return makeFailed(QStringLiteral("设备ID精确回读不一致：写入=%1，真实上电后回读=%2。")
                                  .arg(expectedDeviceId, actualDeviceId),
                              QStringLiteral("persistent_value_mismatch"), keyFrames);
        }
        return makePassed(QStringLiteral("写入肯定响应正确，真实断电上电后0x2C4/0x2C5精确回读=%1。").arg(actualDeviceId),
                          keyFrames);
    }

    const QVector<EvidenceFrame> versionFrames =
        framesAfterAnyId(frames, readbackStart, QList<int>() << 0x2C3, -1);
    if (versionFrames.isEmpty()) {
        return makeBlocked(QStringLiteral("写入已响应，但缺少写入响应后的 0x2C3 版本广播回读证据。"),
                           QStringLiteral("evidence_missing"),
                           keyFrames);
    }
    const EvidenceFrame &versionFrame = versionFrames.first();
    if (versionFrame.bytes.size() < 4) {
        return makeBlocked(QStringLiteral("真实上电后的0x2C3数据不足4字节，无法读取硬件版本。"),
                           QStringLiteral("readback_missing"), keyFrames);
    }
    const int actualHigh = versionFrame.bytes.at(2).toInt(nullptr, 16);
    const int actualLow = versionFrame.bytes.at(3).toInt(nullptr, 16);
    if (actualHigh != expectedValue.at(0) || actualLow != expectedValue.at(1)) {
        return makeFailed(QStringLiteral("硬件版本精确回读不一致：写入=%1%2，真实上电后回读=%3%4。")
                              .arg(expectedValue.at(0), 2, 16, QChar('0'))
                              .arg(expectedValue.at(1), 2, 16, QChar('0'))
                              .arg(actualHigh, 2, 16, QChar('0'))
                              .arg(actualLow, 2, 16, QChar('0')).toUpper(),
                          QStringLiteral("persistent_value_mismatch"), keyFrames);
    }
    return makePassed(QStringLiteral("写入肯定响应正确，真实断电上电后0x2C3硬件版本精确回读=0x%1%2。")
                          .arg(actualHigh, 2, 16, QChar('0'))
                          .arg(actualLow, 2, 16, QChar('0')).toUpper(),
                      keyFrames);
}

TestJudgeResult judgeNvmDistinctWritePowerCycleRestore(
    const TestCase &testCase,
    const QVector<EvidenceFrame> &frames,
    const QString &evidenceText)
{
    const QStringList keyFrames = keyFrameLinesForCase(
        testCase,
        frames,
        QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C3 << 0x2C4 << 0x2C5,
        32);
    if (hasNegativeResponse(frames, QStringLiteral("2E"))) {
        return makeFailed(QStringLiteral("差异值写入或原值恢复过程中收到0x2E否定响应。"),
                          QStringLiteral("negative_response"), keyFrames);
    }

    const bool deviceIdTest = testCase.judgeTemplate.contains(QStringLiteral("device_id"));
    const quint16 did = deviceIdTest ? 0xE7E1 : 0xE7E0;
    const int expectedLength = deviceIdTest ? 16 : 2;
    const QVector<QVector<int>> writeValues = writeDataValuesForDid(frames, did);
    if (writeValues.size() < 2) {
        return makeBlocked(QStringLiteral("未采集到测试值写入和原值恢复两次完整的DID=0x%1请求。")
                               .arg(did, 4, 16, QChar('0')).toUpper(),
                           QStringLiteral("write_sequence_missing"), keyFrames);
    }
    const QVector<int> testValue = writeValues.first();
    const QVector<int> originalValue = writeValues.last();
    if (testValue.size() != expectedLength || originalValue.size() != expectedLength) {
        return makeBlocked(QStringLiteral("测试值或恢复值长度不正确：测试=%1字节，恢复=%2字节，期望=%3字节。")
                               .arg(testValue.size()).arg(originalValue.size()).arg(expectedLength),
                           QStringLiteral("request_value_missing"), keyFrames);
    }
    if (testValue == originalValue) {
        return makeFailed(QStringLiteral("本次测试值与写入前原值相同，无法证明NVM写入实际生效。"),
                          QStringLiteral("test_value_not_changed"), keyFrames);
    }

    auto byteVectorToData = [](const QVector<int> &values) {
        QByteArray data;
        for (const int value : values) {
            data.append(static_cast<char>(value));
        }
        return data;
    };
    auto displayedValue = [deviceIdTest, &byteVectorToData](const QVector<int> &values) {
        const QByteArray data = byteVectorToData(values);
        return deviceIdTest
                   ? QString::fromLatin1(data)
                   : QString::fromLatin1(data.toHex(' ').toUpper());
    };
    const QString testValueText = displayedValue(testValue);
    const QString originalValueText = displayedValue(originalValue);
    if (!evidenceText.contains(QStringLiteral("原值=%1").arg(originalValueText)) ||
        !evidenceText.contains(QStringLiteral("测试值=%1").arg(testValueText))) {
        return makeBlocked(QStringLiteral("差异写入基线事件与两次0x2E请求不一致，无法确认恢复值就是执行前原值。"),
                           QStringLiteral("baseline_missing"), keyFrames);
    }

    const QDateTime testPowerOnTime = executionEventTime(
        evidenceText, QStringLiteral("已确认 NVM 测试值写入后终端重新上电"));
    const QDateTime restorePowerOnTime = executionEventTime(
        evidenceText, QStringLiteral("已确认 NVM 原值恢复后终端重新上电"));
    if (!testPowerOnTime.isValid() || !restorePowerOnTime.isValid() ||
        restorePowerOnTime <= testPowerOnTime) {
        return makeBlocked(QStringLiteral("缺少测试值和原值恢复两个有序的真实断电上电确认事件。"),
                           QStringLiteral("power_cycle_not_confirmed"), keyFrames);
    }

    const QString positivePattern = deviceIdTest
                                        ? QStringLiteral("036EE7E1")
                                        : QStringLiteral("036EE7E0");
    bool testWriteAccepted = false;
    bool restoreWriteAccepted = false;
    for (const EvidenceFrame &frame : framesById(frames, kRfidResponseId, true)) {
        if (!frame.timestamp.isValid() || !frameHasData(frame, positivePattern)) {
            continue;
        }
        if (frame.timestamp < testPowerOnTime) {
            testWriteAccepted = true;
        } else if (frame.timestamp < restorePowerOnTime) {
            restoreWriteAccepted = true;
        }
    }
    if (!testWriteAccepted || !restoreWriteAccepted) {
        return makeBlocked(QStringLiteral("测试值写入或原值恢复缺少对应掉电前的0x6E肯定响应。"),
                           QStringLiteral("response_missing"), keyFrames);
    }

    auto verifyReadback = [&](const QDateTime &startTime,
                              const QVector<int> &expected,
                              const QString &stage,
                              QString *actualValue) -> TestJudgeResult {
        if (deviceIdTest) {
            const QVector<EvidenceFrame> readbackFrames = framesAfterAnyId(
                frames, startTime, QList<int>() << 0x2C4 << 0x2C5, -1);
            const QString actual = collectedDeviceId(readbackFrames);
            if (actualValue != nullptr) {
                *actualValue = actual;
            }
            if (actual.size() != expectedLength) {
                return makeBlocked(QStringLiteral("%1后未采集到完整16字节设备ID，实际=%2。")
                                       .arg(stage, actual.isEmpty() ? QStringLiteral("空") : actual),
                                   QStringLiteral("readback_missing"), keyFrames);
            }
            if (actual.toLatin1() != byteVectorToData(expected)) {
                return makeFailed(QStringLiteral("%1后设备ID不一致：期望=%2，实际=%3。")
                                      .arg(stage, displayedValue(expected), actual),
                                  QStringLiteral("persistent_value_mismatch"), keyFrames);
            }
            return makePassed(QStringLiteral("%1回读正确。").arg(stage), keyFrames);
        }

        const QVector<EvidenceFrame> versionFrames = framesAfterAnyId(
            frames, startTime, QList<int>() << 0x2C3, -1);
        if (versionFrames.isEmpty() || versionFrames.first().bytes.size() < 4) {
            return makeBlocked(QStringLiteral("%1后未采集到完整0x2C3硬件版本。" ).arg(stage),
                               QStringLiteral("readback_missing"), keyFrames);
        }
        const EvidenceFrame &frame = versionFrames.first();
        const QVector<int> actual = QVector<int>()
            << frame.bytes.at(2).toInt(nullptr, 16)
            << frame.bytes.at(3).toInt(nullptr, 16);
        if (actualValue != nullptr) {
            *actualValue = displayedValue(actual);
        }
        if (actual != expected) {
            return makeFailed(QStringLiteral("%1后硬件版本不一致：期望=%2，实际=%3。")
                                  .arg(stage, displayedValue(expected), displayedValue(actual)),
                              QStringLiteral("persistent_value_mismatch"), keyFrames);
        }
        return makePassed(QStringLiteral("%1回读正确。").arg(stage), keyFrames);
    };

    QString testReadback;
    const TestJudgeResult testResult = verifyReadback(
        testPowerOnTime, testValue, QStringLiteral("测试值真实断电上电"), &testReadback);
    if (testResult.status != TestResultStatus::Passed) {
        return testResult;
    }
    QString restoredReadback;
    const TestJudgeResult restoreResult = verifyReadback(
        restorePowerOnTime, originalValue, QStringLiteral("原值恢复后真实断电上电"), &restoredReadback);
    if (restoreResult.status != TestResultStatus::Passed) {
        return restoreResult;
    }

    return makePassed(
        QStringLiteral("NVM差异写入闭环通过：原值=%1，测试值=%2；测试值掉电保持且原值已恢复并再次通过掉电保持验证。")
            .arg(originalValueText, testValueText),
        keyFrames);
}

TestJudgeResult judgeNvmBusyOrSerializedWrite(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C4 << 0x2C5);
    int writeRequests = 0;
    for (const EvidenceFrame &frame : framesById(frames, kRfidRequestId, false)) {
        if (frameHasData(frame, QStringLiteral("2EE7E1"))) {
            ++writeRequests;
        }
    }
    const QByteArray snA("NVM003SNTESTA001");
    const QByteArray snB("NVM003SNTESTB001");
    bool hasSnARequest = false;
    bool hasSnBRequest = false;
    QByteArray restoredSn;
    int e7e1WriteRequests = 0;
    const QVector<QVector<int>> requestPayloads = collectIsoTpPayloads(frames, kRfidRequestId, 0x2E);
    for (const QVector<int> &payload : requestPayloads) {
        if (payload.size() >= 3 && payload.at(1) == 0xE7 && payload.at(2) == 0xE1) {
            ++e7e1WriteRequests;
        }
        if (payloadContainsDidAndData(payload, 0xE7E1, snA)) {
            hasSnARequest = true;
        }
        if (payloadContainsDidAndData(payload, 0xE7E1, snB)) {
            hasSnBRequest = true;
        }
        if (payload.size() == 19 && payload.at(0) == 0x2E && payload.at(1) == 0xE7 && payload.at(2) == 0xE1) {
            QByteArray payloadSn;
            for (int index = 3; index < payload.size(); ++index) {
                payloadSn.append(static_cast<char>(payload.at(index)));
            }
            if (payloadSn != snA && payloadSn != snB) {
                restoredSn = payloadSn;
            }
        }
    }
    writeRequests = qMax(writeRequests, e7e1WriteRequests);
    if (writeRequests < 2) {
        return makeBlocked(QStringLiteral("未采集到连续两次 DID=0xE7E1 的 0x2E 写 SN 请求，无法判定忙处理/串行处理。"),
                            QStringLiteral("request_missing"),
                            keyFrames);
    }
    if (!hasSnARequest || !hasSnBRequest) {
        return makeBlocked(QStringLiteral("连续写入请求未同时包含两组不同测试 SN，无法区分两次写入值。"),
                           QStringLiteral("request_value_missing"),
                            keyFrames);
    }
    if (restoredSn.size() != 16) {
        return makeBlocked(QStringLiteral("未采集到独立的原SN恢复写入请求，禁止在样机数据未恢复时判定通过。"),
                           QStringLiteral("restore_request_missing"), keyFrames);
    }

    EvidenceFrame restoreRequest;
    for (const EvidenceFrame &frame : framesById(frames, kRfidRequestId, false)) {
        const QVector<int> bytes = frameByteValues(frame);
        if (bytes.size() >= 5 && ((bytes.at(0) >> 4) & 0x0F) == 0x1 &&
            bytes.at(2) == 0x2E && bytes.at(3) == 0xE7 && bytes.at(4) == 0xE1) {
            restoreRequest = frame;
        }
    }
    EvidenceFrame restoreResponse;
    if (!restoreRequest.timestamp.isValid() ||
        !findFrameDataAfter(frames, kRfidResponseId, QStringLiteral("036EE7E1"),
                            restoreRequest.timestamp, &restoreResponse)) {
        return makeBlocked(QStringLiteral("原SN恢复写入未获得本次请求后的6E E7 E1肯定响应。"),
                           QStringLiteral("restore_response_missing"), keyFrames);
    }
    const QVector<EvidenceFrame> restoreReadbackFrames =
        framesAfterAnyId(frames, restoreResponse.timestamp, QList<int>() << 0x2C4 << 0x2C5, -1);
    const QString restoredDeviceId = collectedDeviceId(restoreReadbackFrames);
    if (restoredDeviceId.size() != 16) {
        return makeBlocked(QStringLiteral("原SN恢复后未采集到完整0x2C4/0x2C5回读，实际=%1。")
                               .arg(restoredDeviceId.isEmpty() ? QStringLiteral("空") : restoredDeviceId),
                           QStringLiteral("restore_readback_missing"), keyFrames);
    }
    if (restoredDeviceId.toLatin1() != restoredSn) {
        return makeFailed(QStringLiteral("原SN恢复回读不一致：期望=%1，实际=%2。")
                              .arg(QString::fromLatin1(restoredSn), restoredDeviceId),
                          QStringLiteral("restore_value_mismatch"), keyFrames);
    }

    const QStringList allowedNrc = QStringList()
        << QStringLiteral("21") << QStringLiteral("22") << QStringLiteral("31")
        << QStringLiteral("78") << QStringLiteral("13");
    QString badNrc;
    const int negativeCount = countNegativeResponses(frames, QStringLiteral("2E"), allowedNrc, &badNrc);
    if (negativeCount < 0) {
        return makeFailed(QStringLiteral("连续写入返回 7F 2E，但 NRC=0x%1 不在允许范围：%2。")
                              .arg(badNrc, allowedNrc.join(QStringLiteral(", "))),
                          QStringLiteral("unexpected_nrc"),
                          keyFrames);
    }
    if (negativeCount > 0) {
        return makePassed(QStringLiteral("已采集两组重叠SN写入及7F 2E忙/拒绝响应，并确认原SN=%1已恢复。")
                              .arg(restoredDeviceId),
                           keyFrames);
    }

    int positiveE7E1Count = 0;
    for (const EvidenceFrame &frame : framesById(frames, kRfidResponseId, true)) {
        if (frameHasData(frame, QStringLiteral("6EE7E1"))) {
            ++positiveE7E1Count;
        }
    }
    if (positiveE7E1Count >= 3) {
        return makePassed(QStringLiteral("未出现忙/拒绝响应，但三次写入均收到肯定响应：设备完成两组重叠请求的串行处理，原SN=%1已恢复。")
                              .arg(restoredDeviceId),
                          keyFrames);
    }

    return makeBlocked(QStringLiteral("原SN已恢复，但两组重叠写入既没有规范忙/拒绝响应，也没有两次完整肯定响应。"),
                        QStringLiteral("response_missing"),
                        keyFrames);
}
bool isTransmittedRequestWithData(const EvidenceFrame &frame, const QString &compactSequence)
{
    return frame.canId == kRfidRequestId && !isReceiveFrame(frame) && frameHasData(frame, compactSequence);
}

int isoTpServiceByteIndex(const EvidenceFrame &frame)
{
    const QVector<int> bytes = frameByteValues(frame);
    if (bytes.isEmpty()) {
        return -1;
    }
    const int pciType = (bytes.at(0) >> 4) & 0x0F;
    if (pciType == 0x00) {
        return bytes.size() >= 2 ? 1 : -1;
    }
    if (pciType == 0x01) {
        return bytes.size() >= 3 ? 2 : -1;
    }
    return -1;
}

bool isIsoTpServiceFrame(const EvidenceFrame &frame,
                         int canId,
                         bool receiveFrame,
                         int serviceId,
                         int parameter = -1)
{
    if (frame.canId != canId || isReceiveFrame(frame) != receiveFrame) {
        return false;
    }
    const QVector<int> bytes = frameByteValues(frame);
    const int serviceIndex = isoTpServiceByteIndex(frame);
    if (serviceIndex < 0 || bytes.at(serviceIndex) != serviceId) {
        return false;
    }
    return parameter < 0 ||
        (bytes.size() > serviceIndex + 1 && bytes.at(serviceIndex + 1) == parameter);
}

bool findFirstIsoTpServiceFrameAfter(const QVector<EvidenceFrame> &frames,
                                     int canId,
                                     bool receiveFrame,
                                     int serviceId,
                                     int parameter,
                                     const QDateTime &after,
                                     EvidenceFrame *matchedFrame)
{
    for (const EvidenceFrame &frame : frames) {
        if (after.isValid() && (!frame.timestamp.isValid() || frame.timestamp <= after)) {
            continue;
        }
        if (!isIsoTpServiceFrame(frame, canId, receiveFrame, serviceId, parameter)) {
            continue;
        }
        if (matchedFrame != nullptr) {
            *matchedFrame = frame;
        }
        return true;
    }
    return false;
}

bool findFirstIsoTpServiceFrameAfter(const QVector<EvidenceFrame> &frames,
                                     int canId,
                                     bool receiveFrame,
                                     int serviceId,
                                     int parameter,
                                     const EvidenceFrame &afterFrame,
                                     EvidenceFrame *matchedFrame)
{
    for (const EvidenceFrame &frame : frames) {
        if (!frame.timestamp.isValid() ||
            frame.timestamp < afterFrame.timestamp ||
            (frame.timestamp == afterFrame.timestamp && frame.sourceOrder <= afterFrame.sourceOrder)) {
            continue;
        }
        if (!isIsoTpServiceFrame(frame, canId, receiveFrame, serviceId, parameter)) {
            continue;
        }
        if (matchedFrame != nullptr) {
            *matchedFrame = frame;
        }
        return true;
    }
    return false;
}

bool hasAllBootBroadcastsBetween(const QVector<EvidenceFrame> &frames,
                                 const QDateTime &startTime,
                                 const QDateTime &endTime,
                                 QStringList *missingIds)
{
    const QList<int> requiredIds = QList<int>() << 0x2C3 << 0x2C4 << 0x2C5;
    QStringList missing;
    for (const int canId : requiredIds) {
        bool found = false;
        for (const EvidenceFrame &frame : frames) {
            if (frame.canId != canId || !isReceiveFrame(frame) || !frame.timestamp.isValid()) {
                continue;
            }
            if (frame.timestamp >= startTime && frame.timestamp < endTime) {
                found = true;
                break;
            }
        }
        if (!found) {
            missing.append(idText(canId));
        }
    }
    if (missingIds != nullptr) {
        *missingIds = missing;
    }
    return missing.isEmpty();
}

TestJudgeResult judgeBootBroadcastStartupPeriod(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QList<int> broadcastIds = QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C3 << 0x2C4 << 0x2C5;
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, broadcastIds, 30);
    EvidenceFrame bootJumpRequest;
    for (const EvidenceFrame &frame : frames) {
        if (isTransmittedRequestWithData(frame, QStringLiteral("021002"))) {
            bootJumpRequest = frame;
            break;
        }
    }
    if (!bootJumpRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到 0x10 02 跳转 BOOT 请求。"),
                           QStringLiteral("boot_request_missing"), keyFrames);
    }

    EvidenceFrame bootQueryRequest;
    EvidenceFrame bootResponse;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("01A4"), bootJumpRequest.timestamp, &bootQueryRequest) ||
        !findFrameDataAfter(frames, kRfidResponseId, QStringLiteral("E400"), bootQueryRequest.timestamp, &bootResponse)) {
        return makeBlocked(QStringLiteral("跳转 BOOT 后未采集到 A4 查询及 E4 00 BOOT 响应。"),
                           QStringLiteral("boot_evidence_missing"), keyFrames);
    }

    QVector<EvidenceFrame> bootFrames;
    for (const EvidenceFrame &frame : frames) {
        if (frame.timestamp.isValid() && frame.timestamp >= bootJumpRequest.timestamp) {
            bootFrames.append(frame);
        }
    }
    const PeriodWindowCheck periodCheck = checkStartupBroadcastPeriod(
        bootFrames, QList<int>() << 0x2C3 << 0x2C4 << 0x2C5, startupCheckText(testCase));
    if (!periodCheck.passed) {
        return makeBlocked(periodCheck.reason, QStringLiteral("insufficient_period_evidence"), keyFrames);
    }
    QString versionSummary;
    QString contentFailure;
    if (!validateVersionBroadcastContent(bootFrames, &versionSummary, &contentFailure)) {
        return makeFailed(contentFailure, QStringLiteral("version_content_invalid"), keyFrames);
    }
    QString deviceId;
    if (!validateCompleteDeviceId(bootFrames, &deviceId, &contentFailure)) {
        return makeFailed(contentFailure, QStringLiteral("device_id_invalid"), keyFrames);
    }
    return makePassed(QStringLiteral("已确认E4 00 BOOT；广播周期满足要求：%1；%2；设备ID=%3。")
                          .arg(periodCheck.reason, versionSummary, deviceId), keyFrames);
}

TestJudgeResult judgeBootA1A2BroadcastStop(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QList<int> ids = QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C3 << 0x2C4 << 0x2C5;
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, ids, 36);
    EvidenceFrame a1Request;
    EvidenceFrame a2Request;
    EvidenceFrame bootJumpRequest;
    for (const EvidenceFrame &frame : frames) {
        if (!bootJumpRequest.timestamp.isValid() &&
            isTransmittedRequestWithData(frame, QStringLiteral("021002"))) {
            bootJumpRequest = frame;
        }
        if (!a1Request.timestamp.isValid() && isTransmittedRequestWithData(frame, QStringLiteral("A1"))) {
            a1Request = frame;
        }
        if (isTransmittedRequestWithData(frame, QStringLiteral("03A20001"))) {
            a2Request = frame;
        }
    }
    if (!a1Request.timestamp.isValid() || !a2Request.timestamp.isValid() || !bootJumpRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("自动流程不完整：需采集初始 0x10 02 跳转 BOOT、A1 服务和安全 A2 探测服务。"),
                           QStringLiteral("request_missing"), keyFrames);
    }

    if (bootJumpRequest.timestamp >= a1Request.timestamp || a1Request.timestamp >= a2Request.timestamp) {
        return makeBlocked(QStringLiteral("BOOT、A1、A2 的执行时序不完整，无法建立停止广播判定窗口。"),
                           QStringLiteral("baseline_missing"), keyFrames);
    }

    QStringList missingBeforeA1;
    if (!hasAllBootBroadcastsBetween(frames, bootJumpRequest.timestamp, a1Request.timestamp, &missingBeforeA1)) {
        return makeBlocked(QStringLiteral("A1 前 BOOT 广播基线不完整，缺少：%1。")
                               .arg(missingBeforeA1.join(QStringLiteral("、"))),
                           QStringLiteral("a1_baseline_missing"), keyFrames);
    }
    QStringList missingBeforeA2;
    EvidenceFrame appStatusQuery;
    for (const EvidenceFrame &frame : frames) {
        if (frame.timestamp <= a1Request.timestamp || frame.timestamp >= a2Request.timestamp ||
            !isTransmittedRequestWithData(frame, QStringLiteral("01A4"))) {
            continue;
        }
        if (a1Request.timestamp.msecsTo(frame.timestamp) >= kOtaBootInactivityTimeoutMs) {
            appStatusQuery = frame;
            break;
        }
    }
    if (!appStatusQuery.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("A1 后未等待满 OTA 5 秒无后续数据超时并发送 A4 程序位置查询。"),
                           QStringLiteral("a1_timeout_app_query_missing"), keyFrames);
    }
    EvidenceFrame appStatusResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("02E401"),
                                 appStatusQuery.timestamp, &appStatusResponse) ||
        !appStatusResponse.timestamp.isValid() || appStatusResponse.timestamp >= a2Request.timestamp) {
        return makeFailed(QStringLiteral("A1 后 OTA 超时查询未收到 E4 01（APP），未确认已从 BOOT 返回 APP。"),
                          QStringLiteral("a1_timeout_app_not_confirmed"), keyFrames);
    }
    EvidenceFrame reenterBootRequest;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("021002"), appStatusResponse.timestamp, &reenterBootRequest) ||
        !reenterBootRequest.timestamp.isValid() || reenterBootRequest.timestamp >= a2Request.timestamp) {
        return makeBlocked(QStringLiteral("A1 超时确认回 APP 后未采集到第二次 0x10 02 跳转 BOOT 请求，无法确认 A2 服务发送时处于 BOOT。"),
                           QStringLiteral("boot_reentry_missing"), keyFrames);
    }
    const QDateTime a1ObservationEndTime = a1Request.timestamp.addMSecs(kBootBroadcastStopObservationMs);
    QStringList afterA1;
    for (const int canId : QList<int>() << 0x2C3 << 0x2C4 << 0x2C5) {
        if (!framesBetween(frames, a1Request.timestamp, a1ObservationEndTime, canId,
                           kBootBroadcastStopTransportGraceMs).isEmpty()) {
            afterA1.append(idText(canId));
        }
    }
    if (!afterA1.isEmpty()) {
        return makeFailed(QStringLiteral("A1 服务后扣除 %1ms 传输收敛窗口的 %2ms 快发阶段内仍收到 BOOT 广播：%3。")
                              .arg(kBootBroadcastStopTransportGraceMs)
                              .arg(kBootBroadcastStopObservationMs)
                              .arg(afterA1.join(QStringLiteral("、"))),
                          QStringLiteral("a1_broadcast_not_stopped"), keyFrames);
    }
    if (!hasAllBootBroadcastsBetween(frames, reenterBootRequest.timestamp, a2Request.timestamp, &missingBeforeA2)) {
        return makeBlocked(QStringLiteral("A1 超时回 APP 后第二次跳转 BOOT 的广播基线不完整，缺少：%1。")
                               .arg(missingBeforeA2.join(QStringLiteral("、"))),
                           QStringLiteral("boot_reentry_baseline_missing"), keyFrames);
    }
    const QDateTime evidenceEndTime = latestFrameTimestamp(frames);
    const QDateTime a2ObservationEndTime = a2Request.timestamp.addMSecs(kBootBroadcastStopObservationMs);
    if (!evidenceEndTime.isValid() || evidenceEndTime < a2ObservationEndTime) {
        return makeBlocked(QStringLiteral("A2 服务后仅观察 %1ms，少于要求的 BOOT 快发阶段 %2ms 观察窗口。")
                               .arg(a2Request.timestamp.msecsTo(evidenceEndTime))
                               .arg(kBootBroadcastStopObservationMs),
                           QStringLiteral("a2_observation_insufficient"), keyFrames);
    }
    QStringList afterA2;
    for (const int canId : QList<int>() << 0x2C3 << 0x2C4 << 0x2C5) {
        if (!framesBetween(frames, a2Request.timestamp, a2ObservationEndTime, canId,
                           kBootBroadcastStopTransportGraceMs).isEmpty()) {
            afterA2.append(idText(canId));
        }
    }
    if (!afterA2.isEmpty()) {
        return makeFailed(QStringLiteral("A2 服务后扣除 %1ms 传输收敛窗口的 %2ms 快发阶段内仍收到 BOOT 广播：%3。")
                              .arg(kBootBroadcastStopTransportGraceMs)
                              .arg(kBootBroadcastStopObservationMs)
                              .arg(afterA2.join(QStringLiteral("、"))),
                          QStringLiteral("a2_broadcast_not_stopped"), keyFrames);
    }
    EvidenceFrame appRestoreRequest;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("021001"),
                                 a2ObservationEndTime, &appRestoreRequest) ||
        !appRestoreRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("A1/A2广播停止主体规则满足，但A2观察结束后未发送0x10 01恢复APP。"),
                           QStringLiteral("app_restore_request_missing"), keyFrames);
    }
    EvidenceFrame restoredAppQuery;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("01A4"),
                                 appRestoreRequest.timestamp, &restoredAppQuery) ||
        !restoredAppQuery.timestamp.isValid() ||
        !findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("02E401"),
                                 restoredAppQuery.timestamp, nullptr)) {
        return makeBlocked(QStringLiteral("A1/A2广播停止主体规则满足，但A2阶段后未确认恢复到APP；设备可能仍停留BOOT，请先恢复再执行其他用例。"),
                           QStringLiteral("app_restore_not_confirmed"), keyFrames);
    }
    return makePassed(QStringLiteral("已验证：A1、A2均在独立BOOT快发阶段后连续%1ms停止0x2C3/0x2C4/0x2C5；A1超时返回APP，A2阶段结束后也已确认恢复APP。")
                          .arg(kBootBroadcastStopObservationMs),
                      keyFrames);
}

TestJudgeResult judgeOtaProgramStatusResponse(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    EvidenceFrame queryRequest;
    if (!findFirstFrameData(frames, kRfidRequestId, QStringLiteral("01A4"), &queryRequest) ||
        !queryRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到A4程序位置查询请求。"),
                           QStringLiteral("request_missing"), keyFrames);
    }
    if (hasNegativeResponse(framesAfter(frames, queryRequest.timestamp, kRfidResponseId, 0), QStringLiteral("A4"))) {
        return makeFailed(QStringLiteral("OTA 程序位置查询返回否定响应。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }
    for (const EvidenceFrame &frame : framesAfter(frames, queryRequest.timestamp, kRfidResponseId, 0)) {
        if (frame.bytes.size() >= 3 && frame.bytes.at(1).compare(QStringLiteral("E4"), Qt::CaseInsensitive) == 0) {
            const QString location = frame.bytes.at(2).toUpper();
            if (location != QStringLiteral("00") && location != QStringLiteral("01")) {
                return makeFailed(QStringLiteral("A4响应程序位置值0x%1非法，只允许00(BOOT)或01(APP)。").arg(location),
                                  QStringLiteral("location_value_invalid"), keyFrames);
            }
            return makePassed(QStringLiteral("本次A4查询返回E4 %1，当前程序位置=%2。")
                                  .arg(location, location == QStringLiteral("00") ? QStringLiteral("BOOT") : QStringLiteral("APP")),
                              keyFrames);
        }
    }
    return makeBlocked(QStringLiteral("未采集到 OTA 程序位置响应 E4。"),
                       QStringLiteral("response_missing"),
                       keyFrames);
}

TestJudgeResult judgeOtaA1AcceptedBoot(const TestCase &testCase,
                                      const QVector<EvidenceFrame> &frames,
                                      const QString &evidenceText)
{
    const QList<int> ids = QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C3 << 0x2C4 << 0x2C5;
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, ids, 30);
    EvidenceFrame a1Request;
    if (!findFirstFrameData(frames, kRfidRequestId, QStringLiteral("A1"), &a1Request) ||
        !a1Request.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到 OTA A1 升级开始请求。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    QVector<int> e1Payload;
    if (!findIsoTpPayload(frames, kRfidResponseId, 0xE1, &e1Payload) ||
        e1Payload.size() < 2 ||
        e1Payload.at(1) != 0x00) {
        return makeBlocked(QStringLiteral("未采集到 A1 合法请求期望的 E1 00 接受响应。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    if (e1Payload.size() < 7) {
        return makeBlocked(QStringLiteral("A1 接受响应 E1 00 字段不完整，无法确认分包大小、分包数和系统状态。"),
                           QStringLiteral("response_incomplete"),
                           keyFrames);
    }

    const int packetSize = (e1Payload.at(2) << 8) | e1Payload.at(3);
    const int packetCount = (e1Payload.at(4) << 8) | e1Payload.at(5);
    const int systemStatus = e1Payload.at(6);
    if (packetSize <= 0) {
        return makeBlocked(QStringLiteral("A1 接受响应中的分包大小无效：%1。").arg(packetSize),
                           QStringLiteral("invalid_packet_size"),
                           keyFrames);
    }
    if (packetCount <= 0) {
        return makeBlocked(QStringLiteral("A1 接受响应中的分包数无效：%1。").arg(packetCount),
                           QStringLiteral("invalid_packet_count"),
                           keyFrames);
    }
    if (systemStatus != 0x02) {
        return makeBlocked(QStringLiteral("A1 接受响应中的系统状态不是 BOOT：%1。")
                               .arg(otaSystemStatusText(systemStatus)),
                           QStringLiteral("system_status_mismatch"),
                           keyFrames);
    }
    EvidenceFrame e1Response;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("E100"),
                                 a1Request.timestamp, &e1Response) ||
        !e1Response.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("E1 00接受响应未关联到本次A1请求之后。"),
                           QStringLiteral("response_missing"), keyFrames);
    }
    EvidenceFrame queryRequest;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("01A4"),
                                 e1Response.timestamp, &queryRequest)) {
        return makeBlocked(QStringLiteral("A1 接受后未采集到 A4 程序位置查询请求。"),
                           QStringLiteral("query_missing"),
                           keyFrames);
    }
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("02E400"),
                                 queryRequest.timestamp, nullptr)) {
        return makeBlocked(QStringLiteral("A1 接受后未采集到 E4 00 BOOT 程序位置响应。"),
                           QStringLiteral("boot_evidence_missing"),
                           keyFrames);
    }
    if (evidenceHasFrameData(frames, kRfidRequestId, QStringLiteral("A2"))) {
        return makeFailed(QStringLiteral("A1 合法请求短流程中出现 A2 数据下发，不符合仅验证 A1 的执行要求。"),
                          QStringLiteral("unexpected_a2"),
                          keyFrames);
    }
    if (testCase.id == QStringLiteral("MT-RFID-OTA-006")) {
        EvidenceFrame bootJumpRequest;
        for (const EvidenceFrame &frame : framesById(frames, kRfidRequestId, false)) {
            if (frame.timestamp < a1Request.timestamp && frameHasData(frame, QStringLiteral("021002"))) {
                bootJumpRequest = frame;
            }
        }
        if (!bootJumpRequest.timestamp.isValid()) {
            return makeBlocked(QStringLiteral("OTA-006在A1前缺少跳转BOOT边界。"),
                               QStringLiteral("boot_baseline_missing"), keyFrames);
        }
        QStringList missingBaseline;
        if (!hasAllBootBroadcastsBetween(frames, bootJumpRequest.timestamp, a1Request.timestamp, &missingBaseline)) {
            return makeBlocked(QStringLiteral("OTA-006发送A1前BOOT快发基线不完整，缺少%1。")
                                   .arg(missingBaseline.join(QStringLiteral("、"))),
                               QStringLiteral("boot_baseline_missing"), keyFrames);
        }
        constexpr int kObservationMs = 4000;
        constexpr int kTransportGraceMs = 100;
        const QDateTime observationEnd = a1Request.timestamp.addMSecs(kObservationMs);
        const QDateTime collectWindowEnd = executionEventTime(
            evidenceText, QStringLiteral("半自动采集窗口结束"));
        const QDateTime evidenceEnd = collectWindowEnd.isValid()
            ? collectWindowEnd
            : latestFrameTimestamp(frames);
        if (!evidenceEnd.isValid() || evidenceEnd < observationEnd) {
            return makeBlocked(QStringLiteral("OTA-006的A1后观察不足%1ms。").arg(kObservationMs),
                               QStringLiteral("observation_insufficient"), keyFrames);
        }
        QStringList unexpectedIds;
        for (const int canId : QList<int>() << 0x2C3 << 0x2C4 << 0x2C5) {
            if (!framesBetween(frames, a1Request.timestamp, observationEnd, canId, kTransportGraceMs).isEmpty()) {
                unexpectedIds.append(idText(canId));
            }
        }
        if (!unexpectedIds.isEmpty()) {
            return makeFailed(QStringLiteral("A1后扣除100ms链路收敛窗口，在4秒观察期仍收到BOOT广播：%1。")
                                  .arg(unexpectedIds.join(QStringLiteral("、"))),
                              QStringLiteral("boot_broadcast_not_stopped"), keyFrames);
        }
    }
    return makePassed(QStringLiteral("已采集 A1 请求和 E1 00 接受响应：分包大小=%1 字节，分包数=%2，系统状态=%3；A4 查询确认设备处于 BOOT。")
                          .arg(packetSize)
                          .arg(packetCount)
                          .arg(otaSystemStatusText(systemStatus)),
                      keyFrames);
}

TestJudgeResult judgeOtaA1Rejected(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    EvidenceFrame a1Request;
    if (!findFirstFrameData(frames, kRfidRequestId, QStringLiteral("A1"), &a1Request) ||
        !a1Request.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到 OTA A1 升级开始请求。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    EvidenceFrame rejectFrame;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("E101"),
                                 a1Request.timestamp, &rejectFrame) ||
        !rejectFrame.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到 A1 不匹配场景期望的 E1 01 拒绝响应。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    if (!framesAfter(frames, rejectFrame.timestamp, kRfidRequestId, 0).isEmpty() &&
        evidenceHasFrameData(framesAfter(frames, rejectFrame.timestamp, kRfidRequestId, 0),
                             kRfidRequestId, QStringLiteral("A2"))) {
        return makeFailed(QStringLiteral("已收到 E1 01 拒绝响应，但证据中仍出现 A2 数据下发，不符合拒绝升级要求。"),
                          QStringLiteral("unexpected_a2_after_reject"),
                          keyFrames);
    }

    const QString locationSource = QStringLiteral("%1 %2").arg(testCase.module, testCase.precondition).toUpper();
    const bool expectBoot = locationSource.contains(QStringLiteral("BOOT"));
    const bool expectApp = locationSource.contains(QStringLiteral("APP"));
    const QString expectedLocation = expectBoot ? QStringLiteral("BOOT") : QStringLiteral("APP");
    const QString expectedE4 = expectBoot ? QStringLiteral("E400") : QStringLiteral("E401");
    if (expectApp || expectBoot) {
        EvidenceFrame queryRequest;
        if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("01A4"),
                                     rejectFrame.timestamp, &queryRequest)) {
            return makeBlocked(QStringLiteral("已采集 A1 拒绝响应，但拒绝后未采集到 A4 程序位置查询请求。"),
                               QStringLiteral("query_missing"),
                               keyFrames);
        }
        if (!findFirstFrameDataAfter(frames, kRfidResponseId, expectedE4,
                                     queryRequest.timestamp, nullptr)) {
            return makeBlocked(QStringLiteral("已采集 A1 拒绝响应和拒绝后 A4 查询，但未确认设备停留 %1。").arg(expectedLocation),
                               QStringLiteral("location_mismatch"),
                               keyFrames);
        }
    }

    return makePassed(QStringLiteral("已采集 A1 请求和 E1 01 拒绝响应，未进入 A2 数据下发，并通过 A4 确认设备停留 %1。").arg(expectedLocation),
                      keyFrames);
}

TestJudgeResult judgeOtaA2FirstFrameError(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    if (!evidenceHasFrameData(frames, kRfidRequestId, QStringLiteral("A2"))) {
        if (evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("E101"), true)) {
            return makeBlocked(QStringLiteral("A1 升级开始请求被设备拒绝，流程未进入 A2；请检查固件厂商、硬件版本和设备状态。"),
                               QStringLiteral("a1_rejected"),
                               keyFrames);
        }
        return makeBlocked(QStringLiteral("未采集到 OTA A2 数据请求。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    if (!evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("E2000103"), true)) {
        return makeBlocked(QStringLiteral("未采集到 A2 首包数据错误期望的 E2 00 01 03 响应。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    return makePassed(QStringLiteral("已采集 A2 首包请求及 E2 00 01 03 首帧数据错误响应。"),
                      keyFrames);
}

TestJudgeResult judgeOtaA2FirstFrameBootHold(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    EvidenceFrame a2Request;
    if (!findFirstFrameData(frames, kRfidRequestId, QStringLiteral("A2"), &a2Request)) {
        if (evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("E101"), true)) {
            return makeBlocked(QStringLiteral("A1 升级开始请求被设备拒绝，流程未进入 A2；请检查固件厂商、硬件版本和设备状态。"),
                               QStringLiteral("a1_rejected"),
                               keyFrames);
        }
        return makeBlocked(QStringLiteral("未采集到 OTA A2 首包请求。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    EvidenceFrame firstChunkResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("E2000100"), a2Request.timestamp, &firstChunkResponse) &&
        !findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("E2000102"), a2Request.timestamp, &firstChunkResponse)) {
        return makeBlocked(QStringLiteral("未采集到 A2 首包写入成功响应 E2 00 01 00/02。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    EvidenceFrame postA2Query;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("01A4"),
                                 firstChunkResponse.timestamp, &postA2Query)) {
        return makeBlocked(QStringLiteral("A2 首包写入成功后未采集到 A4 程序位置查询请求。"),
                           QStringLiteral("query_missing"),
                           keyFrames);
    }
    EvidenceFrame bootLocationResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("02E400"),
                                 postA2Query.timestamp, &bootLocationResponse)) {
        return makeBlocked(QStringLiteral("A2 首包写入成功并停止后，未采集到 E4 00 BOOT 程序位置响应。"),
                           QStringLiteral("boot_evidence_missing"),
                           keyFrames);
    }
    if (evidenceHasFrameData(frames, kRfidRequestId, QStringLiteral("A3"))) {
        return makeFailed(QStringLiteral("A2 首包验证用例中出现 A3 执行/中止请求，不符合仅验证首包后停止的执行要求。"),
                          QStringLiteral("unexpected_a3"),
                          keyFrames);
    }
    return makePassed(QStringLiteral("已采集 A2 首包写入成功响应，停止继续升级后 A4 查询确认设备保持 BOOT。"),
                      keyFrames);
}

TestJudgeResult judgeOtaA3CrcErrorRejected(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    EvidenceFrame a3Request;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA3, 0x01,
                                         QDateTime(), &a3Request) ||
        !a3Request.timestamp.isValid()) {
        if (evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("E101"), true)) {
            return makeBlocked(QStringLiteral("A1 升级开始请求被设备拒绝，流程未进入 A3；请检查固件厂商、硬件版本和设备状态。"),
                               QStringLiteral("a1_rejected"),
                               keyFrames);
        }
        if (findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA2, -1,
                                            QDateTime(), nullptr)) {
            return makeBlocked(QStringLiteral("已进入 A2 数据阶段，但未采集到 A3 执行升级请求；请检查固件分包是否在等待窗口内完成。"),
                               QStringLiteral("a3_missing_after_a2"),
                               keyFrames);
        }
        return makeBlocked(QStringLiteral("未采集到 OTA A3 执行升级请求。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    EvidenceFrame crcResponse;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidResponseId, true, 0xE3, 0x01,
                                         a3Request, &crcResponse) ||
        !crcResponse.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到 A3 CRC 错误期望的 E3 01 固件校验错误响应。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    EvidenceFrame queryRequest;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA4, -1,
                                         crcResponse, &queryRequest)) {
        return makeBlocked(QStringLiteral("A3 CRC 错误响应后未采集到 A4 程序位置查询请求。"),
                           QStringLiteral("query_missing"),
                           keyFrames);
    }
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidResponseId, true, 0xE4, 0x00,
                                         queryRequest, nullptr)) {
        return makeBlocked(QStringLiteral("A3 CRC 错误响应后未采集到 E4 00 BOOT 程序位置响应。"),
                           QStringLiteral("boot_evidence_missing"),
                           keyFrames);
    }
    return makePassed(QStringLiteral("已采集 A3 执行请求及 E3 01 固件校验错误响应，并通过 A4 查询确认设备停留 BOOT。"),
                      keyFrames);
}

TestJudgeResult judgeOtaSuccessAppRunning(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(
        testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C3, 30);
    EvidenceFrame a3Request;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA3, 0x01,
                                         QDateTime(), &a3Request) ||
        !a3Request.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到 OTA A3 执行升级请求。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    EvidenceFrame finishResponse;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidResponseId, true, 0xE3, 0x00,
                                         a3Request, &finishResponse) ||
        !finishResponse.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到 A3 执行升级成功响应 E3 00。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    EvidenceFrame firstQuery;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA4, -1,
                                         finishResponse, &firstQuery)) {
        return makeBlocked(QStringLiteral("升级成功后未采集到 A4 程序位置查询请求。"),
                           QStringLiteral("query_missing"),
                           keyFrames);
    }
    EvidenceFrame firstAppResponse;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidResponseId, true, 0xE4, 0x01,
                                         firstQuery, &firstAppResponse) ||
        !firstAppResponse.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("升级成功后未采集到 E4 01 APP 程序位置响应。"),
                           QStringLiteral("app_evidence_missing"),
                           keyFrames);
    }
    EvidenceFrame firstVersion;
    for (const EvidenceFrame &frame : framesAfter(frames, firstAppResponse.timestamp, 0x2C3, -1)) {
        if (frame.bytes.size() >= 6) {
            firstVersion = frame;
            break;
        }
    }
    if (!firstVersion.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("升级成功确认APP后未采集到有效0x2C3软件版本。"),
                           QStringLiteral("version_missing"), keyFrames);
    }
    EvidenceFrame resetRequest;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("0111"),
                                 firstVersion.timestamp, &resetRequest)) {
        return makeBlocked(QStringLiteral("采集升级后版本后未发送SID=0x11软件复位。"),
                           QStringLiteral("reset_missing"), keyFrames);
    }
    EvidenceFrame resetResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("0151"),
                                 resetRequest.timestamp, &resetResponse)) {
        return makeBlocked(QStringLiteral("SID=0x11软件复位未收到01 51肯定响应。"),
                           QStringLiteral("reset_response_missing"), keyFrames);
    }
    EvidenceFrame secondQuery;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA4, -1,
                                         resetResponse, &secondQuery)) {
        return makeBlocked(QStringLiteral("软件复位后未发送第二次A4程序位置查询。"),
                           QStringLiteral("query_missing"), keyFrames);
    }
    EvidenceFrame secondAppResponse;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidResponseId, true, 0xE4, 0x01,
                                         secondQuery, &secondAppResponse)) {
        return makeBlocked(QStringLiteral("软件复位后未通过E4 01确认返回APP。"),
                           QStringLiteral("app_evidence_missing"), keyFrames);
    }
    EvidenceFrame secondVersion;
    for (const EvidenceFrame &frame : framesAfter(frames, resetResponse.timestamp, 0x2C3, -1)) {
        if (frame.bytes.size() >= 6) {
            secondVersion = frame;
            break;
        }
    }
    if (!secondVersion.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("软件复位后未采集到第二条有效0x2C3版本帧。"),
                           QStringLiteral("version_missing"), keyFrames);
    }
    const QString beforeResetVersion = firstVersion.bytes.at(4) + firstVersion.bytes.at(5);
    const QString afterResetVersion = secondVersion.bytes.at(4) + secondVersion.bytes.at(5);
    if (beforeResetVersion.compare(afterResetVersion, Qt::CaseInsensitive) != 0) {
        return makeFailed(QStringLiteral("APP软件版本复位前后不一致：复位前=0x%1，复位后=0x%2。")
                              .arg(beforeResetVersion, afterResetVersion),
                          QStringLiteral("version_persistence_mismatch"), keyFrames);
    }
    return makePassed(QStringLiteral("已严格关联A3→E3 00→A4/E4 01→0x2C3→SID11/51→A4/E4 01→0x2C3；软件版本0x%1复位后保持一致。目标固件版本仍需与固件文件人工核对。")
                          .arg(beforeResetVersion), keyFrames);
}

TestJudgeResult judgeOtaTimeoutBootHold(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    EvidenceFrame a2Request;
    if (!findFirstFrameData(frames, kRfidRequestId, QStringLiteral("A2"), &a2Request) ||
        !a2Request.timestamp.isValid()) {
        if (evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("E101"), true)) {
            return makeBlocked(QStringLiteral("A1 升级开始请求被设备拒绝，流程未进入 A2；请检查固件厂商、硬件版本和设备状态。"),
                               QStringLiteral("a1_rejected"),
                               keyFrames);
        }
        return makeBlocked(QStringLiteral("未采集到 OTA A2 数据请求，无法确认超时发生在升级数据阶段。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    EvidenceFrame locationQuery;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("A4"),
                                 a2Request.timestamp, &locationQuery) ||
        !locationQuery.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("OTA 超时停止后未采集到 A4 程序位置查询请求。"),
                           QStringLiteral("query_missing"),
                           keyFrames);
    }
    EvidenceFrame locationResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("E400"),
                                 locationQuery.timestamp, &locationResponse)) {
        return makeBlocked(QStringLiteral("OTA 超时停止后未采集到 E4 00 BOOT 程序位置响应。"),
                           QStringLiteral("boot_evidence_missing"),
                           keyFrames);
    }
    return makePassed(QStringLiteral("已采集 A2 数据阶段证据，超时停止后通过 A4 查询确认设备停留 BOOT。"),
                      keyFrames);
}

TestJudgeResult judgeOtaAbortBootHold(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    EvidenceFrame abortRequest;
    if (!findFirstFrameData(frames, kRfidRequestId, QStringLiteral("A302"), &abortRequest) ||
        !abortRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("未采集到 OTA A3 02 中止升级请求。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    EvidenceFrame locationQuery;
    if (!findFirstFrameDataAfter(frames, kRfidRequestId, QStringLiteral("A4"),
                                 abortRequest.timestamp, &locationQuery) ||
        !locationQuery.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("中止升级后未采集到 A4 程序位置查询请求。"),
                           QStringLiteral("query_missing"),
                           keyFrames);
    }
    EvidenceFrame locationResponse;
    if (!findFirstFrameDataAfter(frames, kRfidResponseId, QStringLiteral("E400"),
                                 locationQuery.timestamp, &locationResponse)) {
        return makeBlocked(QStringLiteral("中止升级后未采集到 E4 00 BOOT 程序位置响应。"),
                           QStringLiteral("boot_evidence_missing"),
                           keyFrames);
    }
    return makePassed(QStringLiteral("已发送 A3 02 中止升级，并通过 A4 查询确认设备停留 BOOT。"),
                      keyFrames);
}

TestJudgeResult judgeOtaPowerLossRecovery(const TestCase &testCase,
                                          const QVector<EvidenceFrame> &frames,
                                          const QString &evidenceText)
{
    const QStringList keyFrames = keyFrameLinesForCase(
        testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C3, 24);

    EvidenceFrame firstA2Request;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA2, -1,
                                         QDateTime(), &firstA2Request) ||
        !firstA2Request.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("断电前未采集到第一次升级的 A2 数据请求，无法确认断电发生在写入阶段。"),
                           QStringLiteral("first_a2_missing"), keyFrames);
    }

    const QDateTime powerOnTime = executionEventTime(
        evidenceText, QStringLiteral("已确认 OTA 后重新上电，开始 BOOT 状态采集"));
    if (!powerOnTime.isValid()) {
        return makeBlocked(QStringLiteral("缺少 OTA 中断后重新上电的人工确认事件，无法建立恢复阶段时序边界。"),
                           QStringLiteral("power_on_event_missing"), keyFrames);
    }

    EvidenceFrame recoveryQuery;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA4, -1,
                                         powerOnTime.addMSecs(-1), &recoveryQuery) ||
        !recoveryQuery.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("重新上电后未采集到 A4 程序位置查询请求。"),
                           QStringLiteral("recovery_query_missing"), keyFrames);
    }

    EvidenceFrame recoveryBootResponse;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidResponseId, true, 0xE4, 0x00,
                                         recoveryQuery, &recoveryBootResponse) ||
        !recoveryBootResponse.timestamp.isValid()) {
        return makeFailed(QStringLiteral("重新上电后未通过 A4 查询确认设备停留 BOOT。"),
                          QStringLiteral("recovery_boot_missing"), keyFrames);
    }

    EvidenceFrame secondA1Request;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA1, -1,
                                         recoveryBootResponse, &secondA1Request) ||
        !secondA1Request.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("确认恢复到 BOOT 后未采集到第二次升级的 A1 请求。"),
                           QStringLiteral("second_a1_missing"), keyFrames);
    }

    EvidenceFrame secondA2Request;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA2, -1,
                                         secondA1Request, &secondA2Request) ||
        !secondA2Request.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("第二次升级未进入 A2 数据写入阶段。"),
                           QStringLiteral("second_a2_missing"), keyFrames);
    }

    EvidenceFrame finishRequest;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA3, 0x01,
                                         secondA2Request, &finishRequest) ||
        !finishRequest.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("第二次升级未采集到 A3 01 执行升级请求。"),
                           QStringLiteral("finish_request_missing"), keyFrames);
    }

    EvidenceFrame finishResponse;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidResponseId, true, 0xE3, 0x00,
                                         finishRequest, &finishResponse) ||
        !finishResponse.timestamp.isValid()) {
        return makeFailed(QStringLiteral("第二次升级发送 A3 01 后未收到 E3 00 成功响应。"),
                          QStringLiteral("finish_response_missing"), keyFrames);
    }

    EvidenceFrame finalQuery;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidRequestId, false, 0xA4, -1,
                                         finishResponse, &finalQuery) ||
        !finalQuery.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("第二次升级完成后未采集到 A4 程序位置查询请求。"),
                           QStringLiteral("final_query_missing"), keyFrames);
    }

    EvidenceFrame finalAppResponse;
    if (!findFirstIsoTpServiceFrameAfter(frames, kRfidResponseId, true, 0xE4, 0x01,
                                         finalQuery, &finalAppResponse)) {
        return makeFailed(QStringLiteral("第二次升级完成后未通过 A4 查询确认设备运行 APP。"),
                          QStringLiteral("final_app_missing"), keyFrames);
    }

    const QVector<EvidenceFrame> appVersionFrames = framesAfter(frames, finishResponse.timestamp, 0x2C3);
    if (appVersionFrames.isEmpty()) {
        return makeBlocked(QStringLiteral("第二次升级成功后未采集到 0x2C3 APP 版本广播，通信恢复证据不足。"),
                           QStringLiteral("app_broadcast_missing"), keyFrames);
    }

    return makePassed(QStringLiteral("第一次升级在 A2 阶段中断；重新上电后确认停留 BOOT，第二次升级成功并运行 APP。"),
                      keyFrames);
}

TestJudgeResult judgeSemiAssist(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QString templ = testCase.judgeTemplate.trimmed();
    const QStringList keyFrames = keyFrameLinesForCase(testCase,
        frames,
        QList<int>() << 0x207 << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C3 << 0x2C4 << 0x2C5 << 0x2C6);
    if (templ == QStringLiteral("mt.semi.tag_present_detected")) {
        for (const EvidenceFrame &status : framesById(frames, 0x2C0, true)) {
            if (status.bytes.size() < 2 || status.bytes.at(1) != QStringLiteral("01")) {
                continue;
            }
            QString tagDetail;
            const TagFragmentCheck tagCheck = checkRecognizedTagFragments(frames, status, &tagDetail);
            if (tagCheck == TagFragmentCheck::Missing) {
                return makeBlocked(QStringLiteral("0x2C0 上报已识别 TAG，但同一状态轮次的 TAG 分片证据不足：%1。")
                                       .arg(tagDetail),
                                   QStringLiteral("tag_evidence_missing"), keyFrames);
            }
            if (tagCheck == TagFragmentCheck::Invalid) {
                return makeFailed(QStringLiteral("状态上报已识别 TAG，但同一状态轮次的 TAG 分片为占位值：%1。")
                                      .arg(tagDetail),
                                  QStringLiteral("tag_status_data_inconsistent"), keyFrames);
            }
            return makePassed(QStringLiteral("0x2C0 上报已识别 TAG，且关联 TAG 分片有效。"),
                              keyFrames);
        }
        return makeBlocked(QStringLiteral("未发现 0x2C0 Byte2=0x01 的证据，请确认 TAG 放置后重试。"),
                           QStringLiteral("expected_missing"),
                           keyFrames);
    }
    if (templ == QStringLiteral("mt.semi.tag_16byte_collected")) {
        if (evidenceHasFrameByte(frames, 0x2C0, 1, QStringLiteral("01")) &&
            hasValidTagSegmentFrame(frames, 0x2C1) &&
            hasValidTagSegmentFrame(frames, 0x2C2)) {
            const QString extendedText = hasUnrecognizedTagPlaceholderFrame(frames, 0x2C6)
                ? QStringLiteral("；已采集到 0x2C6 全 0x30 扩展占位帧")
                : QString();
            return makePassed(QStringLiteral("已采集到 0x2C1 和 0x2C2 有效 16 字节 TAG 分片%1，请人工确认拼接内容。")
                                  .arg(extendedText),
                              keyFrames);
        }
        return makeBlocked(QStringLiteral("未同时采集到 0x2C0 Byte2=0x01、0x2C1/0x2C2 有效可打印 ASCII TAG 分片。"),
                           QStringLiteral("evidence_missing"),
                           keyFrames);
    }
    if (templ == QStringLiteral("mt.semi.tag_absent_cleared") ||
        templ == QStringLiteral("mt.semi.tag_residue_cleared")) {
        const bool requiresExtendedTagPart = templ == QStringLiteral("mt.semi.tag_residue_cleared");
        const bool hasRequiredTagPlaceholders =
            hasUnrecognizedTagPlaceholderFrame(frames, 0x2C1) &&
            hasUnrecognizedTagPlaceholderFrame(frames, 0x2C2) &&
            (!requiresExtendedTagPart || hasUnrecognizedTagPlaceholderFrame(frames, 0x2C6));
        if (evidenceHasFrameByte(frames, 0x2C0, 1, QStringLiteral("00")) &&
            hasRequiredTagPlaceholders) {
            return makePassed(requiresExtendedTagPart
                                  ? QStringLiteral("已采集到无 TAG 状态，且 0x2C1/0x2C2/0x2C6 TAG 分片均为 0x30 未识别占位值。")
                                  : QStringLiteral("已采集到无 TAG 状态，且 0x2C1/0x2C2 TAG 分片均为 0x30 未识别占位值。"),
                              keyFrames);
        }
        return makeFailed(requiresExtendedTagPart
                              ? QStringLiteral("无 TAG 状态下未采集到 0x2C1/0x2C2/0x2C6 全 0x30 占位值，可能仍存在旧 TAG 或异常广播。")
                              : QStringLiteral("无 TAG 状态下未采集到 0x2C1/0x2C2 全 0x30 占位值，可能仍存在旧 TAG 或异常广播。"),
                          QStringLiteral("expected_mismatch"),
                          keyFrames);
    }
    if (templ == QStringLiteral("mt.semi.tag_24byte_collected")) {
        if (evidenceHasFrameByte(frames, 0x2C0, 1, QStringLiteral("01")) &&
            hasValidTagSegmentFrame(frames, 0x2C1) &&
            hasValidTagSegmentFrame(frames, 0x2C2) &&
            hasValidTagSegmentFrame(frames, 0x2C6)) {
            return makePassed(QStringLiteral("已采集到 0x2C1/0x2C2/0x2C6 有效 24 字节 TAG 分片，请人工确认拼接内容。"),
                              keyFrames);
        }
        return makeBlocked(QStringLiteral("未完整采集 0x2C0 Byte2=0x01 以及 0x2C1/0x2C2/0x2C6 有效可打印 ASCII TAG 分片。"),
                           QStringLiteral("evidence_missing"),
                           keyFrames);
    }
    if (templ == QStringLiteral("mt.semi.version_frame_collected")) {
        if (!framesById(frames, 0x2C3, true).isEmpty()) {
            return makePassed(QStringLiteral("已采集到 0x2C3 版本帧，请与样机基线人工确认。"),
                              keyFrames);
        }
        return makeBlocked(QStringLiteral("未采集到 0x2C3 版本帧。"),
                           QStringLiteral("evidence_missing"),
                           keyFrames);
    }
    if (templ == QStringLiteral("mt.semi.device_id_collected")) {
        if (!framesById(frames, 0x2C4, true).isEmpty() && !framesById(frames, 0x2C5, true).isEmpty()) {
            return makePassed(QStringLiteral("已采集到 0x2C4 和 0x2C5 设备 ID 分片，请人工确认拼接内容。"),
                              keyFrames);
        }
        return makeBlocked(QStringLiteral("未同时采集到 0x2C4/0x2C5 设备 ID 分片。"),
                           QStringLiteral("evidence_missing"),
                           keyFrames);
    }
    if (templ == QStringLiteral("mt.semi.fault_status_collected") ||
        templ == QStringLiteral("mt.semi.fault_control_status_collected")) {
        bool hasUnexpectedFaultCode = false;
        QString unexpectedFaultCode;
        for (const EvidenceFrame &frame : framesById(frames, 0x2C0, true)) {
            if (frame.bytes.size() < 3) {
                continue;
            }
            const QString faultCode = frame.bytes.at(2).toUpper();
            if (faultCode == QStringLiteral("01") || faultCode == QStringLiteral("02")) {
                const QString faultText = faultCode == QStringLiteral("01")
                    ? QStringLiteral("模块故障")
                    : QStringLiteral("通信异常");
                return makePassed(QStringLiteral("已采集到 0x2C0 Byte3=0x%1（%2），符合故障信息上报预期。")
                                      .arg(faultCode, faultText),
                                  keyFrames);
            }
            if (faultCode != QStringLiteral("00")) {
                hasUnexpectedFaultCode = true;
                unexpectedFaultCode = faultCode;
            }
        }
        if (hasUnexpectedFaultCode) {
            return makeFailed(QStringLiteral("已采集到 0x2C0 Byte3=0x%1，但不属于预期的 0x01 模块故障或 0x02 通信异常。")
                                  .arg(unexpectedFaultCode),
                              QStringLiteral("unexpected_fault_code"),
                              keyFrames);
        }
        return makeBlocked(QStringLiteral("未采集到 0x2C0 Byte3=0x01/0x02 故障状态。"),
                           QStringLiteral("expected_missing"),
                           keyFrames);
    }
    return makeBlocked(QStringLiteral("暂不支持该半自动判定模板。"),
                       QStringLiteral("manual_required"),
                       keyFrames);
}
}

QString TestCaseJudge::caseJoinedText(const TestCase &testCase)
{
    QStringList fields;
    fields << testCase.id
           << testCase.module
           << testCase.testData
           << testCase.steps
           << testCase.expectedResult
           << testCase.basis
           << testCase.commandTemplate
           << testCase.judgeTemplate
           << testCase.expectedNegativeSid
           << testCase.allowedNrc.join(QStringLiteral(" "))
           << testCase.semiAssistTemplate
           << testCase.semiJudgeTemplate;
    return fields.join(QStringLiteral(" "));
}

QString TestCaseJudge::compactHexText(QString text)
{
    text = text.toUpper();
    text.remove(QRegularExpression(QStringLiteral("[^0-9A-FX]")));
    return text;
}

bool TestCaseJudge::isNegativeCase(const QString &caseText)
{
    return caseText.contains(QStringLiteral("异常")) ||
           caseText.contains(QStringLiteral("否定")) ||
           caseText.contains(QStringLiteral("非法")) ||
           caseText.contains(QStringLiteral("错误"));
}

QStringList TestCaseJudge::extractKeyFrames(const QString &evidenceText, const QStringList &keywords)
{
    QStringList frames;
    const QStringList lines = evidenceText.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString upperLine = line.toUpper();
        for (const QString &keyword : keywords) {
            if (upperLine.contains(keyword.toUpper())) {
                frames.append(line.trimmed());
                break;
            }
        }
        if (frames.size() >= 8) {
            break;
        }
    }
    return frames;
}

QString TestCaseJudge::expectationText(const TestCase &testCase) const
{
    QStringList lines;
    lines << QStringLiteral("执行模式：%1")
        .arg(testCase.executionMode.isEmpty() ? QStringLiteral("manual") : testCase.executionMode);
    if (!testCase.commandTemplate.isEmpty()) {
        lines << QStringLiteral("命令模板：%1").arg(testCase.commandTemplate);
    }
    if (!testCase.judgeTemplate.isEmpty()) {
        lines << QStringLiteral("判定模板：%1").arg(testCase.judgeTemplate);
    }
    if (!testCase.manualPrompt.isEmpty()) {
        lines << QStringLiteral("人工提示：%1").arg(testCase.manualPrompt);
    }
    if (!testCase.expectedNegativeSid.isEmpty()) {
        lines << QStringLiteral("期望否定响应SID：%1").arg(testCase.expectedNegativeSid);
    }
    if (!testCase.allowedNrc.isEmpty()) {
        lines << QStringLiteral("允许NRC：%1").arg(testCase.allowedNrc.join(QStringLiteral(", ")));
    }
    if (!testCase.semiAssistTemplate.isEmpty()) {
        lines << QStringLiteral("半自动辅助：%1").arg(testCase.semiAssistTemplate);
    }
    if (!testCase.semiJudgeTemplate.isEmpty()) {
        lines << QStringLiteral("半自动判定：%1").arg(testCase.semiJudgeTemplate);
    }
    lines << QStringLiteral("超时：%1 ms；重试：%2 次").arg(testCase.timeoutMs).arg(testCase.retryCount);
    return lines.join(QStringLiteral("\n"));
}

TestJudgeResult TestCaseJudge::judge(const TestCase &testCase, const QString &evidenceText) const
{
    if (evidenceText.trimmed().isEmpty()) {
        return makeBlocked(QStringLiteral("未找到当前用例证据日志，请先开始执行用例并采集 CAN 收发帧。"),
                           QStringLiteral("evidence_missing"));
    }

    const QVector<EvidenceFrame> frames = parseEvidenceFrames(evidenceText);
    if (frames.isEmpty()) {
        return makeBlocked(QStringLiteral("证据日志中没有可解析的 CAN 帧，请确认日志格式和采集状态。"),
                           QStringLiteral("evidence_missing"));
    }

    const QString judgeTemplate = testCase.judgeTemplate.trimmed();
    if (judgeTemplate == QStringLiteral("mt.broadcast_all_present_after_reboot")) {
        return judgeBroadcastAllAfterReboot(frames);
    }
    if (judgeTemplate == QStringLiteral("mt.short_dlc_safe_ignore")) {
        return judgeShortDlcSafeIgnore(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.semi.rf_chip_fault_recovery")) {
        const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << 0x207 << 0x2C0);
        const QVector<EvidenceFrame> statuses = framesById(frames, 0x2C0, true);
        QStringList phases;
        int consecutive = 0;
        QString previous;
        for (const EvidenceFrame &status : statuses) {
            if (status.bytes.size() < 3) {
                continue;
            }
            const QString fault = status.bytes.at(2).toUpper();
            if (fault == previous) {
                ++consecutive;
            } else {
                previous = fault;
                consecutive = 1;
            }
            if (consecutive == 3 && (fault == QStringLiteral("00") || fault == QStringLiteral("01")) &&
                (phases.isEmpty() || phases.last() != fault)) {
                phases.append(fault);
            }
        }
        if (phases.contains(QStringLiteral("01")) && phases.size() >= 3 &&
            phases.at(0) == QStringLiteral("00") && phases.at(1) == QStringLiteral("01") && phases.last() == QStringLiteral("00")) {
            return makePassed(QStringLiteral("已采集连续3帧故障状态序列 Byte3=0x00→0x01→0x00，射频芯片故障与恢复上报正确。"), keyFrames);
        }
        if (statuses.isEmpty()) {
            return makeBlocked(QStringLiteral("故障阶段未收到任何0x2C0；可能擦除后终端不再广播，需记录为外部硬件阻塞。"),
                               QStringLiteral("broadcast_missing"), keyFrames);
        }
        return makeFailed(QStringLiteral("未采集到连续3帧 Byte3=0x00→0x01→0x00 的故障恢复序列，实际稳定阶段：%1。")
                              .arg(phases.join(QStringLiteral("→"))),
                          QStringLiteral("fault_recovery_sequence_mismatch"), keyFrames);
    }
    if (judgeTemplate == QStringLiteral("mt.control_reserved_padding_ignore")) {
        return judgeControlReservedPaddingIgnore(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.broadcast_start_after_reboot")) {
        return judgeBroadcastStartAfterReboot(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.device_id_prefix_check")) {
        return judgeDeviceIdPrefix(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.startup_broadcast_period")) {
        const QString caseText = startupCheckText(testCase);
        const QList<int> startupIds = startupPeriodTargetIds(caseText);
        const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, startupIds, 24);
        const PeriodWindowCheck startupCheck = checkStartupBroadcastPeriod(frames, startupIds, caseText);
        if (startupCheck.passed) {
            if (testCase.id == QStringLiteral("MT-RFID-BC-010")) {
                QString versionSummary;
                QString contentFailure;
                if (!validateVersionBroadcastContent(frames, &versionSummary, &contentFailure)) {
                    return makeFailed(contentFailure, QStringLiteral("version_content_invalid"), keyFrames);
                }
                return makePassed(QStringLiteral("周期自动检查通过：%1；%2。目标版本与实物/固件的一致性仍需人工确认。")
                                      .arg(startupCheck.reason, versionSummary), keyFrames);
            }
            return makePassed(QStringLiteral("上电广播周期满足要求：%1").arg(startupCheck.reason),
                              keyFrames);
        }
        return makeBlocked(startupCheck.reason,
                           QStringLiteral("insufficient_period_evidence"),
                           keyFrames);
    }
    if (judgeTemplate == QStringLiteral("mt.control_0x207_timeout_fault")) {
        return judgeControl207TimeoutFault(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.isotp_multiframe_flow")) {
        return judgeIsoTpMultiframeFlow(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.boot_reset_app_recovery")) {
        return judgeBootResetAppRecovery(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.diag_0x10_jump_and_query")) {
        return judgeDiag10JumpAndQuery(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.nvm_write_same_value_and_verify") ||
        judgeTemplate == QStringLiteral("mt.nvm_device_id_writeback_verify") ||
        judgeTemplate == QStringLiteral("mt.nvm_sn_reboot_verify")) {
        return judgeNvmWriteAndVerify(testCase, frames, evidenceText);
    }
    if (judgeTemplate == QStringLiteral("mt.nvm_hw_distinct_power_cycle_restore") ||
        judgeTemplate == QStringLiteral("mt.nvm_device_id_distinct_power_cycle_restore")) {
        return judgeNvmDistinctWritePowerCycleRestore(testCase, frames, evidenceText);
    }
    if (judgeTemplate == QStringLiteral("mt.nvm_busy_or_serialized_write")) {
        return judgeNvmBusyOrSerializedWrite(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.nvm_invalid_write_unchanged")) {
        return judgeNvmInvalidWriteUnchanged(testCase, frames, evidenceText);
    }
    if (judgeTemplate == QStringLiteral("mt.ota_program_status_response")) {
        return judgeOtaProgramStatusResponse(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.boot_broadcast_startup_period")) {
        return judgeBootBroadcastStartupPeriod(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.boot_a1_a2_broadcast_stop")) {
        return judgeBootA1A2BroadcastStop(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.ota_a1_accepted_boot")) {
        return judgeOtaA1AcceptedBoot(testCase, frames, evidenceText);
    }
    if (judgeTemplate == QStringLiteral("mt.ota_a1_rejected")) {
        return judgeOtaA1Rejected(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.ota_a2_first_frame_error")) {
        return judgeOtaA2FirstFrameError(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.ota_a2_first_frame_boot_hold")) {
        return judgeOtaA2FirstFrameBootHold(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.ota_a3_crc_error_rejected")) {
        return judgeOtaA3CrcErrorRejected(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.ota_success_app_running")) {
        return judgeOtaSuccessAppRunning(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.ota_timeout_boot_hold")) {
        return judgeOtaTimeoutBootHold(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.ota_abort_boot_hold")) {
        return judgeOtaAbortBootHold(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.ota_power_loss_recovery")) {
        return judgeOtaPowerLossRecovery(testCase, frames, evidenceText);
    }
    if (judgeTemplate == QStringLiteral("mt.status_2c0_work_mode") ||
        judgeTemplate == QStringLiteral("mt.status_2c0_no_tag")) {
        return judgeControlStatus(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.control_0x207_invalid_rejected")) {
        return judgeInvalidControlRejected(frames);
    }
    if (judgeTemplate == QStringLiteral("mt.control_0x207_toggle_followed")) {
        return judgeControlToggleFollowed(frames);
    }
    if (judgeTemplate == QStringLiteral("mt.control_0x207_fault_toggle_repeated")) {
        return judgeFaultControlToggleRepeated(frames);
    }
    if (judgeTemplate == QStringLiteral("mt.status_2c0_stopped_no_tag")) {
        return judgeStoppedNoTag(frames);
    }
    if (judgeTemplate == QStringLiteral("mt.status_2c0_start_no_tag")) {
        return judgeStartedNoTag(frames);
    }
    if (judgeTemplate == QStringLiteral("mt.scan_period_response_and_2c0")) {
        return judgeScanPeriod(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.diag_0x85_response")) {
        return judgeDiag85Response(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.negative_response_expected")) {
        return judgeExpectedNegativeResponse(testCase, frames);
    }
    if (judgeTemplate.startsWith(QStringLiteral("mt.semi."))) {
        return judgeSemiAssist(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.positive_response_and_broadcast")) {
        return judgePositiveResponseAndBroadcast(frames, QStringLiteral("11"), QStringLiteral("0151"));
    }
    if (judgeTemplate == QStringLiteral("mt.broadcast_disabled")) {
        return judgeBroadcastDisabled(frames);
    }
    if (judgeTemplate == QStringLiteral("mt.broadcast_recovered")) {
        if (testCase.commandTemplate == QStringLiteral("mt.sid_0x28_broadcast_disable_then_enable")) {
            return judgeBroadcastRecoveredAfterDisableEnable(frames);
        }
        return judgeBroadcastRecovered(frames);
    }
    if (judgeTemplate == QStringLiteral("mt.period_config_response_and_effect")) {
        return judgePeriodConfig(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.period_restore_default_responses")) {
        return judgeDefaultPeriodRestoreResponses(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.period_restore_2c0_response")) {
        return judgeDefaultPeriodRestoreResponses(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.scan_period_restore_response")) {
        return judgeScanPeriodRestore(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.app_restore_after_test")) {
        return judgeAppRestoreAfterTest(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.reboot_response_and_broadcast")) {
        return judgeBroadcastAllAfterReboot(frames);
    }
    if (judgeTemplate == QStringLiteral("mt.reboot_preserves_device_config")) {
        return judgeRebootPreservesDeviceConfig(testCase, frames, evidenceText);
    }
    if (judgeTemplate == QStringLiteral("mt.response_sid_or_negative")) {
        if (hasNegativeResponse(frames, QStringLiteral("01"))) {
            return makeFailed(QStringLiteral("收到 7F 01 否定响应，正向响应类用例失败。"),
                              QStringLiteral("negative_response"),
                              keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId));
        }
        if (evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("41"), true)) {
            return makePassed(QStringLiteral("收到 SID=0x01 的肯定响应 0x41。"),
                              keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId));
        }
    }
    if (judgeTemplate == QStringLiteral("mt.manual_review")) {
        return makeBlocked(QStringLiteral("该用例设计为人工场景或专用流程确认，请按步骤核对证据后手工保存结果。"),
                           QStringLiteral("manual_required"),
                           keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId
                               << 0x207 << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C6));
    }

    const QString caseText = caseJoinedText(testCase);
    const bool negative = isNegativeCase(caseText);
    const QString compact = compactHexText(evidenceText);
    if (negative && compact.contains(QStringLiteral("7F"))) {
        return makeBlocked(QStringLiteral("证据日志包含否定响应，但自动判定无法确认 NRC 与用例要求完全一致，请人工确认后保存。"),
                           QStringLiteral("manual_required"),
                           extractKeyFrames(evidenceText, QStringList() << QStringLiteral("7F")));
    }

    if (testCase.commandTemplate == QStringLiteral("mt.sid_0x01_set_scan_period") ||
        caseText.contains(QStringLiteral("SID=0x01")) ||
        caseText.contains(QStringLiteral("扫描周期"))) {
        return judgeScanPeriod(testCase, frames);
    }
    if (testCase.commandTemplate == QStringLiteral("mt.sid_0x02_reboot") ||
        caseText.contains(QStringLiteral("SID=0x02")) ||
        caseText.contains(QStringLiteral("重启"))) {
        return judgeBroadcastAllAfterReboot(frames);
    }
    if (testCase.commandTemplate == QStringLiteral("mt.sid_0x29_period_config") ||
        caseText.contains(QStringLiteral("0x29"))) {
        return judgePeriodConfig(testCase, frames);
    }
    if (requiresStartupBroadcastPeriodCheck(caseText)) {
        const QList<int> startupIds = startupPeriodTargetIds(caseText);
        const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, startupIds, 24);
        const PeriodWindowCheck startupCheck = checkStartupBroadcastPeriod(frames, startupIds, caseText);
        if (startupCheck.passed) {
            return makePassed(QStringLiteral("上电广播周期满足要求：%1").arg(startupCheck.reason),
                              keyFrames);
        }
        return makeBlocked(startupCheck.reason,
                           QStringLiteral("insufficient_period_evidence"),
                           keyFrames);
    }
    if (caseText.contains(QStringLiteral("0x2C0")) || caseText.contains(QStringLiteral("0x2C6")) ||
        caseText.contains(QStringLiteral("广播"))) {
        const QStringList keyFrames = keyFrameLines(frames, QList<int>() << 0x2C0 << 0x2C1 << 0x2C2
            << 0x2C3 << 0x2C4 << 0x2C5 << 0x2C6);
        const QVector<QPair<int, QString>> expectedBytes = expectedBroadcastBytes(caseText);
        for (const QPair<int, QString> &expectedByte : expectedBytes) {
            if (!evidenceHasFrameByte(frames, 0x2C0, expectedByte.first, expectedByte.second)) {
                return makeFailed(QStringLiteral("广播帧存在，但 0x2C0 Byte%1 未匹配期望值 0x%2。")
                                      .arg(expectedByte.first + 1)
                                      .arg(expectedByte.second),
                                  QStringLiteral("expected_mismatch"),
                                  keyFrames);
            }
        }
        if (!keyFrames.isEmpty() && !expectedBytes.isEmpty()) {
            return makePassed(QStringLiteral("广播帧字段与用例 Byte 期望值一致。"), keyFrames);
        }
        if (!keyFrames.isEmpty()) {
            return makeBlocked(QStringLiteral("证据日志包含广播帧，但该用例没有可机器校验的 Byte 期望条件，请人工确认后保存。"),
                               QStringLiteral("manual_required"),
                               keyFrames);
        }
        return makeBlocked(QStringLiteral("未发现 0x2C0~0x2C6 广播帧证据，需人工确认。"),
                           QStringLiteral("broadcast_missing"));
    }

    return makeBlocked(QStringLiteral("证据不足或暂无专用自动判定规则，请人工确认。"),
                       QStringLiteral("manual_required"),
                       keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId));
}
