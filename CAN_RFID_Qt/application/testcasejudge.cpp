#include "testcasejudge.h"

#include <QRegularExpression>

QString TestCaseJudge::caseJoinedText(const TestCase &testCase)
{
    return QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8")
        .arg(testCase.id,
             testCase.module,
             testCase.testData,
             testCase.steps,
             testCase.expectedResult,
             testCase.basis,
             testCase.commandTemplate,
             testCase.judgeTemplate);
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
        if (frames.size() >= 5) {
            break;
        }
    }
    return frames;
}

QString TestCaseJudge::expectationText(const TestCase &testCase) const
{
    const QString text = caseJoinedText(testCase);
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
    lines << QStringLiteral("超时：%1 ms；重试：%2 次").arg(testCase.timeoutMs).arg(testCase.retryCount);
    lines << QString();

    if (text.contains(QStringLiteral("SID=0x01")) || text.contains(QStringLiteral("扫描周期"))) {
        lines << QStringLiteral("扫描周期配置：请求应为 02 01 XX 55 55 55 55 55，XX 单位为 10ms。")
              << QStringLiteral("肯定响应：02 41 XX 55 55 55 55 55。否定响应：03 7F 01 NRC 55 55 55 55。")
              << QStringLiteral("协议文档示例 01 01 28 / 01 41 28 疑似错误；按 ISO-TP 单帧长度应为 02。");
    }
    if (text.contains(QStringLiteral("SID=0x02")) || text.contains(QStringLiteral("重启"))) {
        lines << QStringLiteral("重启服务：请求应为 01 02 55 55 55 55 55 55，肯定响应通常为 01 42 55 55 55 55 55 55。");
    }
    if (text.contains(QStringLiteral("0x29"))) {
        lines << QStringLiteral("0x29 广播周期配置：请求应为 05 29 ID_H ID_L PERIOD_H PERIOD_L 55 55。")
              << QStringLiteral("0x2C0~0x2C6 为重点广播 ID；0xFFFF 表示停止发送。正响应 SID 应为 0x69。");
    }
    if (text.contains(QStringLiteral("0x2E")) || text.contains(QStringLiteral("SN"))) {
        lines << QStringLiteral("0x2E 写 NVM：单帧短数据响应 SID=0x6E；写 16 字节 SN 应走 ISO-TP 首帧/流控/连续帧。")
              << QStringLiteral("SN 合法性：16 位，前六位 R2A3A0，ASCII 写入 DID 0xE7E1。批量自动执行不直接触发持久化写入。");
    }
    if (text.contains(QStringLiteral("0x2C0")) || text.contains(QStringLiteral("0x2C6")) || text.contains(QStringLiteral("广播"))) {
        lines << QStringLiteral("广播帧检查：关注 0x2C0~0x2C6 是否出现、周期是否符合配置、未识别 TAG 时数据是否按协议清零。");
    }
    if (isNegativeCase(text)) {
        lines << QStringLiteral("负向/异常用例：预期收到 7F SID NRC 格式否定响应，且设备不能死机、总线异常或无提示卡死。");
    }
    return lines.join(QStringLiteral("\n"));
}

TestJudgeResult TestCaseJudge::judge(const TestCase &testCase, const QString &evidenceText) const
{
    TestJudgeResult result;
    if (evidenceText.trimmed().isEmpty()) {
        result.status = TestResultStatus::Blocked;
        result.reason = QStringLiteral("未找到当前用例证据日志，请先开始执行用例并采集 CAN 收发帧。");
        result.failureCategory = QStringLiteral("evidence_missing");
        return result;
    }

    const QString evidence = evidenceText.toUpper();
    const QString compact = compactHexText(evidence);
    const QString caseText = caseJoinedText(testCase);
    const bool negative = isNegativeCase(caseText);

    if (negative && evidence.contains(QStringLiteral("7F"))) {
        result.status = TestResultStatus::Passed;
        result.reason = QStringLiteral("证据日志包含 7F 否定响应，符合负向/异常用例的基本预期，请人工确认 NRC 是否符合协议。");
        result.keyFrames = extractKeyFrames(evidenceText, QStringList() << QStringLiteral("7F"));
        return result;
    }

    if (caseText.contains(QStringLiteral("扫描周期")) || caseText.contains(QStringLiteral("SID=0x01")) ||
        testCase.commandTemplate == QStringLiteral("mt.sid_0x01_set_scan_period")) {
        result.keyFrames = extractKeyFrames(evidenceText, QStringList() << QStringLiteral("02 01") << QStringLiteral("02 41") << QStringLiteral("7F 01"));
        if (compact.contains(QStringLiteral("010128")) || compact.contains(QStringLiteral("014128"))) {
            result.status = TestResultStatus::Failed;
            result.reason = QStringLiteral("发现协议文档示例式长度字段 01 01/01 41，按 ISO-TP 单帧应使用长度 02，请确认设备或发送端实现。");
            result.failureCategory = QStringLiteral("protocol_format");
            return result;
        }
        if (compact.contains(QStringLiteral("0201")) && compact.contains(QStringLiteral("0241"))) {
            result.status = TestResultStatus::Passed;
            result.reason = QStringLiteral("发现扫描周期请求 02 01 XX 及肯定响应 02 41 XX，自动判定通过。");
            return result;
        }
        if (compact.contains(QStringLiteral("7F01"))) {
            result.status = negative ? TestResultStatus::Passed : TestResultStatus::Failed;
            result.reason = QStringLiteral("发现扫描周期服务否定响应 7F 01，需结合用例预期确认 NRC。");
            result.failureCategory = negative ? QString() : QStringLiteral("negative_response");
            return result;
        }
        result.reason = QStringLiteral("未发现完整扫描周期请求/响应证据，需人工确认。");
        result.failureCategory = QStringLiteral("manual_required");
        return result;
    }

    if (caseText.contains(QStringLiteral("重启")) || caseText.contains(QStringLiteral("SID=0x02")) ||
        testCase.commandTemplate == QStringLiteral("mt.sid_0x02_reboot")) {
        result.keyFrames = extractKeyFrames(evidenceText, QStringList() << QStringLiteral("01 02") << QStringLiteral("01 42") << QStringLiteral("7F 02"));
        if (compact.contains(QStringLiteral("0102")) && compact.contains(QStringLiteral("0142"))) {
            result.status = TestResultStatus::Passed;
            result.reason = QStringLiteral("发现重启请求 01 02 及肯定响应 01 42，自动判定通过。");
            return result;
        }
        if (compact.contains(QStringLiteral("7F02"))) {
            result.status = negative ? TestResultStatus::Passed : TestResultStatus::Failed;
            result.reason = QStringLiteral("发现重启服务否定响应 7F 02，需结合用例预期确认 NRC。");
            result.failureCategory = negative ? QString() : QStringLiteral("negative_response");
            return result;
        }
        result.reason = QStringLiteral("未发现完整重启请求/响应证据，需人工确认。");
        result.failureCategory = QStringLiteral("manual_required");
        return result;
    }

    if (caseText.contains(QStringLiteral("0x29")) || testCase.commandTemplate == QStringLiteral("mt.sid_0x29_period_config")) {
        result.keyFrames = extractKeyFrames(evidenceText, QStringList() << QStringLiteral("05 29") << QStringLiteral("69") << QStringLiteral("7F 29"));
        if (compact.contains(QStringLiteral("0529")) && evidence.contains(QStringLiteral("69"))) {
            result.status = TestResultStatus::Passed;
            result.reason = QStringLiteral("发现 0x29 周期配置请求及 0x69 正响应，自动判定通过。");
            return result;
        }
        if (compact.contains(QStringLiteral("7F29"))) {
            result.status = negative ? TestResultStatus::Passed : TestResultStatus::Failed;
            result.reason = QStringLiteral("发现 0x29 否定响应 7F 29，需结合用例预期确认 NRC。");
            result.failureCategory = negative ? QString() : QStringLiteral("negative_response");
            return result;
        }
        result.reason = QStringLiteral("未发现完整 0x29 请求/响应证据，需人工确认。");
        result.failureCategory = QStringLiteral("manual_required");
        return result;
    }

    if (caseText.contains(QStringLiteral("0x2E")) || caseText.contains(QStringLiteral("SN"))) {
        result.keyFrames = extractKeyFrames(evidenceText, QStringList() << QStringLiteral("2E") << QStringLiteral("6E") << QStringLiteral("7F 2E") << QStringLiteral("30"));
        if (evidence.contains(QStringLiteral("6E"))) {
            result.status = TestResultStatus::Passed;
            result.reason = QStringLiteral("发现 0x2E 正响应 SID=0x6E，自动判定通过；如为 SN 写入，请继续确认写后读取/广播一致。");
            return result;
        }
        if (compact.contains(QStringLiteral("7F2E"))) {
            result.status = negative ? TestResultStatus::Passed : TestResultStatus::Failed;
            result.reason = QStringLiteral("发现 0x2E 否定响应 7F 2E，需结合用例预期确认 NRC。");
            result.failureCategory = negative ? QString() : QStringLiteral("negative_response");
            return result;
        }
        if (evidence.contains(QStringLiteral("30")) && evidence.contains(QStringLiteral("2E"))) {
            result.reason = QStringLiteral("发现 0x2E/ISO-TP 流控相关证据，但未发现 0x6E 正响应，需人工确认完整分包流程。");
            result.failureCategory = QStringLiteral("flow_control_incomplete");
            return result;
        }
        result.reason = QStringLiteral("未发现完整 0x2E 响应证据，需人工确认。");
        result.failureCategory = QStringLiteral("manual_required");
        return result;
    }

    if (caseText.contains(QStringLiteral("0x2C0")) || caseText.contains(QStringLiteral("0x2C6")) ||
        caseText.contains(QStringLiteral("广播"))) {
        result.keyFrames = extractKeyFrames(evidenceText, QStringList() << QStringLiteral("0X2C0") << QStringLiteral("0X2C1") << QStringLiteral("0X2C2") << QStringLiteral("0X2C6"));
        if (!result.keyFrames.isEmpty()) {
            result.status = TestResultStatus::Passed;
            result.reason = QStringLiteral("证据日志包含 0x2C0~0x2C6 广播帧，自动判定基础通信/广播出现项通过；周期和字段内容仍建议人工复核。");
            return result;
        }
        result.reason = QStringLiteral("未发现 0x2C0~0x2C6 广播帧证据，需人工确认。");
        result.failureCategory = QStringLiteral("broadcast_missing");
        return result;
    }

    if (evidence.contains(QStringLiteral("0X107")) || evidence.contains(QStringLiteral("肯定")) || evidence.contains(QStringLiteral("响应"))) {
        result.reason = QStringLiteral("发现响应类证据，但该用例暂无专用自动判定规则，请人工确认后保存结果。");
        result.failureCategory = QStringLiteral("manual_required");
        result.keyFrames = extractKeyFrames(evidenceText, QStringList() << QStringLiteral("0X107") << QStringLiteral("响应"));
        return result;
    }

    result.reason = QStringLiteral("证据不足或暂无专用判定规则，请人工确认。");
    result.failureCategory = QStringLiteral("manual_required");
    return result;
}
