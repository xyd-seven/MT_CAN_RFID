#include "testcasejudge.h"

#include <QDateTime>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

namespace {
const int kRfidRequestId = 0x007;
const int kRfidResponseId = 0x107;
const int kRfidBroadcastFirstId = 0x2C0;
const int kRfidBroadcastLastId = 0x2C6;
const int kClassicCanDlc = 8;

struct EvidenceFrame
{
    QDateTime timestamp;
    QString line;
    QString direction;
    int canId = -1;
    QStringList bytes;
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
    for (const QString &line : lines) {
        const QStringList fields = parseCsvLine(line);
        if (fields.size() < 5) {
            continue;
        }

        EvidenceFrame frame;
        frame.line = line.trimmed();
        frame.timestamp = QDateTime::fromString(fields.at(0).trimmed(), QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
        frame.direction = fields.at(1).trimmed();
        if (!parseCanId(fields.at(3), &frame.canId)) {
            continue;
        }
        frame.bytes = hexByteTokens(fields.at(4));
        frames.append(frame);
    }
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

QStringList keyFrameLines(const QVector<EvidenceFrame> &frames, const QList<int> &ids, int limit = 8)
{
    QStringList lines;
    for (const EvidenceFrame &frame : frames) {
        if (ids.contains(frame.canId)) {
            lines.append(frame.line);
            if (lines.size() >= limit) {
                break;
            }
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

bool hasPositiveResponseSid(const QVector<EvidenceFrame> &frames, const QString &positiveSid)
{
    return evidenceHasFrameData(frames, kRfidResponseId, positiveSid, true);
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

bool parsePeriodConfig(const TestCase &testCase, int *targetId, int *periodMs, QString *compactPayload)
{
    const QString text = testCase.testData;
    QRegularExpression payloadRegex(QStringLiteral("\\b05\\s+29\\s+([0-9A-Fa-f]{2})\\s+([0-9A-Fa-f]{2})\\s+([0-9A-Fa-f]{2})\\s+([0-9A-Fa-f]{2})\\b"));
    QRegularExpressionMatch match = payloadRegex.match(text);
    if (!match.hasMatch()) {
        return false;
    }

    bool ok = false;
    const int parsedId = match.captured(1).toInt(&ok, 16) << 8;
    if (!ok) return false;
    const int parsedIdLow = match.captured(2).toInt(&ok, 16);
    if (!ok) return false;
    const int parsedPeriod = match.captured(3).toInt(&ok, 16) << 8;
    if (!ok) return false;
    const int parsedPeriodLow = match.captured(4).toInt(&ok, 16);
    if (!ok) return false;

    if (targetId != nullptr) {
        *targetId = parsedId | parsedIdLow;
    }
    if (periodMs != nullptr) {
        *periodMs = parsedPeriod | parsedPeriodLow;
    }
    if (compactPayload != nullptr) {
        *compactPayload = QStringLiteral("0529%1%2%3%4")
            .arg(match.captured(1).toUpper(),
                 match.captured(2).toUpper(),
                 match.captured(3).toUpper(),
                 match.captured(4).toUpper());
    }
    return true;
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

bool frameBytesAllZero(const EvidenceFrame &frame)
{
    for (const QString &byte : frame.bytes) {
        if (byte.compare(QStringLiteral("00"), Qt::CaseInsensitive) != 0) {
            return false;
        }
    }
    return !frame.bytes.isEmpty();
}

bool hasAllZeroFrame(const QVector<EvidenceFrame> &frames, int canId)
{
    for (const EvidenceFrame &frame : framesById(frames, canId, true)) {
        if (frameBytesAllZero(frame)) {
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
    if (!evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("0241") + periodHex, true)) {
        return makeBlocked(QStringLiteral("未发现 02 41 %1 肯定响应，扫描周期配置证据不足。").arg(periodHex),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    if (!evidenceHasFrameByte(frames, 0x2C0, 3, periodHex)) {
        return makeFailed(QStringLiteral("已收到 02 41 %1，但未发现 0x2C0 Byte4 更新为 0x%1。").arg(periodHex),
                          QStringLiteral("expected_mismatch"),
                          keyFrames);
    }
    return makePassed(QStringLiteral("收到 02 41 %1 肯定响应，且 0x2C0 Byte4 与扫描周期配置一致。").arg(periodHex),
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
        << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C6);
    int targetId = -1;
    int periodMs = -1;
    QString requestCompact;
    if (!parsePeriodConfig(testCase, &targetId, &periodMs, &requestCompact)) {
        return makeBlocked(QStringLiteral("0x29 用例测试数据格式无效，无法解析目标 ID 和周期。"),
                           QStringLiteral("manual_required"),
                           keyFrames);
    }
    if (hasNegativeResponse(frames, QStringLiteral("29"))) {
        return makeFailed(QStringLiteral("收到 0x29 周期配置否定响应，正向用例失败。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }
    const QString positiveCompact = QStringLiteral("69") + requestCompact.mid(4);
    if (!evidenceHasFrameData(frames, kRfidResponseId, positiveCompact, true)) {
        return makeBlocked(QStringLiteral("未发现 0x29 周期配置肯定响应，或响应 ID/周期与请求不一致。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    EvidenceFrame responseFrame;
    if (!findFirstFrameData(frames, kRfidResponseId, positiveCompact, &responseFrame) ||
        !responseFrame.timestamp.isValid()) {
        return makeBlocked(QStringLiteral("已收到 0x29 肯定响应，但无法解析响应时间，不能按响应后窗口统计周期。"),
                           QStringLiteral("timestamp_invalid"),
                           keyFrames);
    }

    const QVector<EvidenceFrame> targetFrames = framesAfter(frames, responseFrame.timestamp, targetId, 0);
    if (periodMs == 0xFFFF) {
        const QVector<EvidenceFrame> afterGraceFrames = framesAfter(frames, responseFrame.timestamp, targetId, 200);
        if (afterGraceFrames.isEmpty()) {
            return makePassed(QStringLiteral("收到 0x29 禁止广播肯定响应，目标 %1 在响应后 200ms 之外未再出现。").arg(idText(targetId)),
                              keyFrames);
        }
        return makeFailed(QStringLiteral("目标 %1 配置 0xFFFF 后仍在响应后 200ms 之外出现广播。").arg(idText(targetId)),
                          QStringLiteral("broadcast_not_disabled"),
                          keyFrames);
    }

    if (targetFrames.size() < 3) {
        return makeBlocked(QStringLiteral("已收到 0x29 肯定响应，但目标 %1 周期采样不足，至少需要 3 帧。").arg(idText(targetId)),
                           QStringLiteral("insufficient_samples"),
                           keyFrames);
    }

    qint64 totalInterval = 0;
    int intervalCount = 0;
    for (int index = 1; index < targetFrames.size(); ++index) {
        if (!targetFrames.at(index - 1).timestamp.isValid() || !targetFrames.at(index).timestamp.isValid()) {
            return makeBlocked(QStringLiteral("目标 %1 帧时间戳无效，无法统计周期。").arg(idText(targetId)),
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
        return makeBlocked(QStringLiteral("目标 %1 帧时间间隔无效，无法统计周期。").arg(idText(targetId)),
                           QStringLiteral("timestamp_invalid"),
                           keyFrames);
    }
    const double averageMs = static_cast<double>(totalInterval) / intervalCount;
    const double tolerance = periodMs <= 100 ? 30.0 : (periodMs <= 1000 ? periodMs * 0.2 : periodMs * 0.25);
    if (qAbs(averageMs - periodMs) > tolerance) {
        return makeFailed(QStringLiteral("目标 %1 周期配置响应正确，但实测平均周期 %2 ms 与期望 %3 ms 超出容差。")
                              .arg(idText(targetId))
                              .arg(averageMs, 0, 'f', 1)
                              .arg(periodMs),
                          QStringLiteral("period_mismatch"),
                          keyFrames);
    }
    return makePassed(QStringLiteral("收到 0x29 肯定响应，目标 %1 响应后平均周期 %2 ms，符合期望 %3 ms。")
                          .arg(idText(targetId))
                          .arg(averageMs, 0, 'f', 1)
                          .arg(periodMs),
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
        if (frame.canId == 0x207 && !frame.bytes.isEmpty() &&
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
    for (int index = 0; index < commandsToCheck; ++index) {
        const EvidenceFrame command = commands.at(index);
        const QString expectedMode = command.bytes.at(0);
        const QDateTime endTime = (index + 1 < commands.size() && commands.at(index + 1).timestamp.isValid())
            ? commands.at(index + 1).timestamp
            : command.timestamp.addMSecs(700);
        bool followed = false;
        for (const EvidenceFrame &status : framesBetween(frames, command.timestamp, endTime, 0x2C0, 0)) {
            if (!status.bytes.isEmpty() && status.bytes.at(0) == expectedMode) {
                followed = true;
                break;
            }
        }
        if (!followed) {
            failedRounds.append(QStringLiteral("第%1条命令期望0x%2").arg(index + 1).arg(expectedMode));
        }
    }
    if (!failedRounds.isEmpty()) {
        return makeFailed(QStringLiteral("0x207 切换后 0x2C0 未逐次跟随：%1。").arg(failedRounds.join(QStringLiteral("；"))),
                          QStringLiteral("expected_mismatch"),
                          keyFrames);
    }
    return makePassed(QStringLiteral("0x207 开始/停止 10 轮切换完整，且 0x2C0 在每次命令后均跟随对应工作模式。"),
                      keyFrames);
}

TestJudgeResult judgeStoppedNoTag(const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << 0x207 << 0x2C0);
    if (!hasFrameFirstByte(frames, 0x207, QStringLiteral("00"))) {
        return makeBlocked(QStringLiteral("未发现 0x207 停止检测命令。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    for (const EvidenceFrame &frame : framesById(frames, 0x2C0, true)) {
        if (frame.bytes.size() >= 3 &&
            frame.bytes.at(0) == QStringLiteral("00") &&
            frame.bytes.at(1) == QStringLiteral("00") &&
            frame.bytes.at(2) == QStringLiteral("00")) {
            return makePassed(QStringLiteral("停止检测后，0x2C0 Byte1/Byte2/Byte3 均为 0x00。"),
                              keyFrames);
        }
    }
    return makeFailed(QStringLiteral("停止检测后，未发现 Byte1=0x00、Byte2=0x00、Byte3=0x00 的 0x2C0 状态帧。"),
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
    return makePassed(QStringLiteral("SID=0x85 肯定响应与请求子功能 0x%1 一致。").arg(subFunction),
                      keyFrames);
}

TestJudgeResult judgeExpectedNegativeResponse(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLines(frames, QList<int>() << kRfidRequestId << kRfidResponseId);
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
        expectedCount = 3;
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
    return makePassed(QStringLiteral("已采集到 %1 条符合预期的 7F %2 否定响应，首条 NRC=0x%3。")
                          .arg(negativeCount)
                          .arg(sid, nrc),
                      keyFrames);
}

TestJudgeResult judgeShortDlcSafeIgnore(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C0);
    bool hasShortRequest = false;
    for (const EvidenceFrame &frame : framesById(frames, kRfidRequestId, false)) {
        if (frame.bytes.size() > 0 && frame.bytes.size() < kClassicCanDlc) {
            hasShortRequest = true;
            break;
        }
    }
    if (!hasShortRequest) {
        return makeBlocked(QStringLiteral("未采集到短 DLC 异常请求帧，无法判定设备是否安全忽略。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }
    if (hasNegativeResponse(frames, QStringLiteral("01")) ||
        hasNegativeResponse(frames, QStringLiteral("02")) ||
        hasNegativeResponse(frames, QStringLiteral("29"))) {
        return makePassed(QStringLiteral("短 DLC 请求已发送，设备返回否定响应或继续正常通信，未出现异常复位证据。"),
                          keyFrames);
    }
    if (!appearedBroadcastIds(frames).isEmpty()) {
        return makePassed(QStringLiteral("短 DLC 请求已发送，后续仍采集到正常广播帧，设备通信未被异常请求破坏。"),
                          keyFrames);
    }
    return makeBlocked(QStringLiteral("短 DLC 请求已发送，但后续缺少响应或广播证据，请延长采集或确认设备在线。"),
                       QStringLiteral("evidence_missing"),
                       keyFrames);
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
    return makePassed(QStringLiteral("已采集设备 ID=%1，内容非空且满足前缀要求。").arg(deviceId),
                      keyFrames);
}

TestJudgeResult judgeControl207TimeoutFault(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << 0x207 << 0x2C0);
    for (const EvidenceFrame &frame : framesById(frames, 0x2C0, true)) {
        if (frame.bytes.size() >= 3 && frame.bytes.at(2).compare(QStringLiteral("00"), Qt::CaseInsensitive) != 0) {
            return makePassed(QStringLiteral("暂停 0x207 后采集到 0x2C0 Byte3 非 0 故障状态。"),
                              keyFrames);
        }
    }
    if (!framesById(frames, 0x2C0, true).isEmpty()) {
        return makeFailed(QStringLiteral("暂停 0x207 后仍未采集到 0x2C0 Byte3 非 0 故障状态。"),
                          QStringLiteral("expected_mismatch"),
                          keyFrames);
    }
    return makeBlocked(QStringLiteral("暂停 0x207 后未采集到 0x2C0 状态帧，请确认广播是否开启。"),
                       QStringLiteral("evidence_missing"),
                       keyFrames);
}

TestJudgeResult judgeDiag10JumpAndQuery(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    if (hasNegativeResponse(frames, QStringLiteral("10"))) {
        return makeFailed(QStringLiteral("跳转 APP/BOOT 服务返回否定响应。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }
    if (!hasPositiveResponseSid(frames, QStringLiteral("0250"))) {
        return makeBlocked(QStringLiteral("未采集到 SID=0x10 的肯定响应 02 50 xx。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    if (hasNegativeResponse(frames, QStringLiteral("A4"))) {
        return makeFailed(QStringLiteral("程序位置查询返回否定响应。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }
    if (!hasPositiveResponseSid(frames, QStringLiteral("E4"))) {
        return makeBlocked(QStringLiteral("跳转后未采集到程序位置查询响应 E4，请确认设备是否完成模式切换。"),
                           QStringLiteral("response_missing"),
                           keyFrames);
    }
    return makePassed(QStringLiteral("已采集跳转肯定响应，并采集到程序位置查询响应。"), keyFrames);
}

TestJudgeResult judgeNvmWriteAndVerify(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
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

    if (testCase.judgeTemplate == QStringLiteral("mt.nvm_sn_reboot_verify")) {
        EvidenceFrame rebootResponse;
        if (!findFrameDataAfter(frames, kRfidResponseId, QStringLiteral("0142"), writeResponse.timestamp, &rebootResponse)) {
            EvidenceFrame rebootRequest;
            if (findFrameDataAfter(frames, kRfidRequestId, QStringLiteral("0102"), writeResponse.timestamp, &rebootRequest)) {
                return makeBlocked(QStringLiteral("已写入并发送重启请求，但缺少重启肯定响应 01 42，无法验证重启后保持性。"),
                                   QStringLiteral("response_missing"),
                                   keyFrames);
            }
            return makeBlocked(QStringLiteral("已写入，但未采集到写入后的重启请求/响应，无法验证重启后保持性。"),
                               QStringLiteral("request_missing"),
                               keyFrames);
        }
        if (!rebootResponse.timestamp.isValid()) {
            return makeBlocked(QStringLiteral("重启响应时间戳无效，无法限定重启后回读窗口。"),
                               QStringLiteral("timestamp_invalid"),
                               keyFrames);
        }
        const QVector<EvidenceFrame> rebootReadbackFrames =
            framesAfterAnyId(frames, rebootResponse.timestamp, QList<int>() << 0x2C4 << 0x2C5);
        return judgeDeviceIdPrefix(testCase, rebootReadbackFrames);
    }

    if (verifyDeviceId) {
        const QVector<EvidenceFrame> readbackFrames =
            framesAfterAnyId(frames, writeResponse.timestamp, QList<int>() << 0x2C4 << 0x2C5);
        return judgeDeviceIdPrefix(testCase, readbackFrames);
    }

    const QVector<EvidenceFrame> versionFrames =
        framesAfterAnyId(frames, writeResponse.timestamp, QList<int>() << 0x2C3);
    if (versionFrames.isEmpty()) {
        return makeBlocked(QStringLiteral("写入已响应，但缺少写入响应后的 0x2C3 版本广播回读证据。"),
                           QStringLiteral("evidence_missing"),
                           keyFrames);
    }
    return makePassed(QStringLiteral("已采集写入肯定响应及写入响应后的 0x2C3 回读广播证据。"), keyFrames);
}

TestJudgeResult judgeNvmBusyOrSerializedWrite(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId << 0x2C3);
    int writeRequests = 0;
    for (const EvidenceFrame &frame : framesById(frames, kRfidRequestId, false)) {
        if (frameHasData(frame, QStringLiteral("2EE7"))) {
            ++writeRequests;
        }
    }
    if (writeRequests < 2) {
        return makeBlocked(QStringLiteral("未采集到连续两次 0x2E 写入请求，无法判定忙处理/串行处理。"),
                           QStringLiteral("request_missing"),
                           keyFrames);
    }

    if (evidenceHasFrameData(frames, kRfidResponseId, QStringLiteral("036EE7"), true)) {
        return makePassed(QStringLiteral("已采集连续写入请求及 0x2E 写入肯定响应，设备按串行/接受方式处理。"),
                          keyFrames);
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
        return makePassed(QStringLiteral("已采集连续写入请求及 7F 2E 否定响应，设备按忙/拒绝方式处理。"),
                          keyFrames);
    }

    return makeBlocked(QStringLiteral("已发送连续写入请求，但未采集到 03 6E E7 xx 或 03 7F 2E NRC 响应证据。"),
                       QStringLiteral("response_missing"),
                       keyFrames);
}
TestJudgeResult judgeOtaProgramStatusResponse(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QStringList keyFrames = keyFrameLinesForCase(testCase, frames, QList<int>() << kRfidRequestId << kRfidResponseId);
    if (hasNegativeResponse(frames, QStringLiteral("A4"))) {
        return makeFailed(QStringLiteral("OTA 程序位置查询返回否定响应。"),
                          QStringLiteral("negative_response"),
                          keyFrames);
    }
    for (const EvidenceFrame &frame : framesById(frames, kRfidResponseId, true)) {
        if (frame.bytes.size() >= 3 && frame.bytes.at(1).compare(QStringLiteral("E4"), Qt::CaseInsensitive) == 0) {
            return makePassed(QStringLiteral("已采集 OTA 程序位置响应 E4。"), keyFrames);
        }
    }
    return makeBlocked(QStringLiteral("未采集到 OTA 程序位置响应 E4。"),
                       QStringLiteral("response_missing"),
                       keyFrames);
}

TestJudgeResult judgeSemiAssist(const TestCase &testCase, const QVector<EvidenceFrame> &frames)
{
    const QString templ = testCase.judgeTemplate.trimmed();
    const QStringList keyFrames = keyFrameLinesForCase(testCase,
        frames,
        QList<int>() << 0x207 << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C3 << 0x2C4 << 0x2C5 << 0x2C6);
    if (templ == QStringLiteral("mt.semi.tag_present_detected")) {
        if (evidenceHasFrameByte(frames, 0x2C0, 1, QStringLiteral("01"))) {
            return makePassed(QStringLiteral("0x2C0 Byte2 上报已识别到 TAG。"),
                              keyFrames);
        }
        return makeBlocked(QStringLiteral("未发现 0x2C0 Byte2=0x01 的证据，请确认 TAG 放置后重试。"),
                           QStringLiteral("expected_missing"),
                           keyFrames);
    }
    if (templ == QStringLiteral("mt.semi.tag_16byte_collected")) {
        if (!framesById(frames, 0x2C1, true).isEmpty() && !framesById(frames, 0x2C2, true).isEmpty()) {
            return makePassed(QStringLiteral("已采集到 0x2C1 和 0x2C2 TAG 分片，请人工确认拼接内容。"),
                              keyFrames);
        }
        return makeBlocked(QStringLiteral("未同时采集到 0x2C1/0x2C2 TAG 分片。"),
                           QStringLiteral("evidence_missing"),
                           keyFrames);
    }
    if (templ == QStringLiteral("mt.semi.tag_absent_cleared") ||
        templ == QStringLiteral("mt.semi.tag_residue_cleared")) {
        if (evidenceHasFrameByte(frames, 0x2C0, 1, QStringLiteral("00")) &&
            hasAllZeroFrame(frames, 0x2C1) &&
            hasAllZeroFrame(frames, 0x2C2)) {
            return makePassed(QStringLiteral("已采集到无 TAG 状态，且 TAG 分片为全 0。"),
                              keyFrames);
        }
        return makeFailed(QStringLiteral("无 TAG 清零证据不完整，可能仍存在旧 TAG 或非 0 TAG 分片。"),
                          QStringLiteral("expected_mismatch"),
                          keyFrames);
    }
    if (templ == QStringLiteral("mt.semi.tag_24byte_collected")) {
        if (!framesById(frames, 0x2C1, true).isEmpty() &&
            !framesById(frames, 0x2C2, true).isEmpty() &&
            !framesById(frames, 0x2C6, true).isEmpty()) {
            return makePassed(QStringLiteral("已采集到 0x2C1/0x2C2/0x2C6 TAG 分片，请人工确认拼接内容。"),
                              keyFrames);
        }
        return makeBlocked(QStringLiteral("未完整采集 24 字节 TAG 所需的 0x2C1/0x2C2/0x2C6 分片。"),
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
        for (const EvidenceFrame &frame : framesById(frames, 0x2C0, true)) {
            if (frame.bytes.size() >= 3 && frame.bytes.at(2) != QStringLiteral("00")) {
                return makePassed(QStringLiteral("0x2C0 Byte3 上报非 0 故障状态，请人工确认故障场景。"),
                                  keyFrames);
            }
        }
        return makeBlocked(QStringLiteral("未采集到 0x2C0 Byte3 非 0 故障状态。"),
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
    if (judgeTemplate == QStringLiteral("mt.broadcast_start_after_reboot")) {
        return judgeBroadcastAllAfterReboot(frames);
    }
    if (judgeTemplate == QStringLiteral("mt.device_id_prefix_check")) {
        return judgeDeviceIdPrefix(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.control_0x207_timeout_fault")) {
        return judgeControl207TimeoutFault(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.diag_0x10_jump_and_query")) {
        return judgeDiag10JumpAndQuery(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.nvm_write_same_value_and_verify") ||
        judgeTemplate == QStringLiteral("mt.nvm_device_id_writeback_verify") ||
        judgeTemplate == QStringLiteral("mt.nvm_sn_reboot_verify")) {
        return judgeNvmWriteAndVerify(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.nvm_busy_or_serialized_write")) {
        return judgeNvmBusyOrSerializedWrite(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.ota_program_status_response")) {
        return judgeOtaProgramStatusResponse(testCase, frames);
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
        return judgeBroadcastRecovered(frames);
    }
    if (judgeTemplate == QStringLiteral("mt.period_config_response_and_effect")) {
        return judgePeriodConfig(testCase, frames);
    }
    if (judgeTemplate == QStringLiteral("mt.reboot_response_and_broadcast")) {
        return judgeBroadcastAllAfterReboot(frames);
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
    if (caseText.contains(QStringLiteral("0x2C0")) || caseText.contains(QStringLiteral("0x2C6")) ||
        caseText.contains(QStringLiteral("广播"))) {
        const QStringList keyFrames = keyFrameLines(frames, QList<int>() << 0x2C0 << 0x2C1 << 0x2C2 << 0x2C6);
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
