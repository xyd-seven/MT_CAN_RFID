#include "testsummarybuilder.h"

#include <QTextStream>

namespace {
struct SummaryCounts
{
    int total = 0;
    int passed = 0;
    int failed = 0;
    int blocked = 0;
    int notApplicable = 0;
    int notRun = 0;
};

void addStatus(SummaryCounts *counts, TestResultStatus status)
{
    if (counts == nullptr) {
        return;
    }
    ++counts->total;
    switch (status) {
    case TestResultStatus::Passed: ++counts->passed; break;
    case TestResultStatus::Failed: ++counts->failed; break;
    case TestResultStatus::Blocked: ++counts->blocked; break;
    case TestResultStatus::NotApplicable: ++counts->notApplicable; break;
    case TestResultStatus::NotRun:
    default: ++counts->notRun; break;
    }
}

int executedCount(const SummaryCounts &counts)
{
    return counts.passed + counts.failed + counts.blocked + counts.notApplicable;
}

double passRate(const SummaryCounts &counts)
{
    const int executed = executedCount(counts);
    return executed == 0 ? 0.0 : static_cast<double>(counts.passed) * 100.0 / executed;
}
}

QString TestSummaryBuilder::progressText(const QVector<TestCase> &cases, const QMap<QString, TestCaseResult> &results) const
{
    SummaryCounts total;
    QMap<QString, SummaryCounts> moduleCounts;
    QMap<QString, SummaryCounts> priorityCounts;
    QMap<QString, int> failureCategories;

    for (const TestCase &testCase : cases) {
        const TestCaseResult result = results.value(testCase.id);
        addStatus(&total, result.status);
        SummaryCounts module = moduleCounts.value(testCase.module);
        addStatus(&module, result.status);
        moduleCounts.insert(testCase.module, module);
        SummaryCounts priority = priorityCounts.value(testCase.priority);
        addStatus(&priority, result.status);
        priorityCounts.insert(testCase.priority, priority);
        if ((result.status == TestResultStatus::Failed || result.status == TestResultStatus::Blocked) &&
            !result.failureCategory.trimmed().isEmpty()) {
            failureCategories[result.failureCategory] = failureCategories.value(result.failureCategory) + 1;
        }
    }

    QString text;
    QTextStream stream(&text);
    stream << QStringLiteral("总体：总数%1，已执行%2，通过%3，失败%4，阻塞%5，不适用%6，未执行%7，通过率%8%\n")
        .arg(total.total)
        .arg(executedCount(total))
        .arg(total.passed)
        .arg(total.failed)
        .arg(total.blocked)
        .arg(total.notApplicable)
        .arg(total.notRun)
        .arg(passRate(total), 0, 'f', 2);

    stream << QStringLiteral("\n按优先级：\n");
    for (auto it = priorityCounts.constBegin(); it != priorityCounts.constEnd(); ++it) {
        const SummaryCounts counts = it.value();
        stream << QStringLiteral("- %1：总数%2，已执行%3，通过%4，失败%5，阻塞%6，通过率%7%\n")
            .arg(it.key())
            .arg(counts.total)
            .arg(executedCount(counts))
            .arg(counts.passed)
            .arg(counts.failed)
            .arg(counts.blocked)
            .arg(passRate(counts), 0, 'f', 2);
    }

    stream << QStringLiteral("\n按模块：\n");
    for (auto it = moduleCounts.constBegin(); it != moduleCounts.constEnd(); ++it) {
        const SummaryCounts counts = it.value();
        stream << QStringLiteral("- %1：总数%2，已执行%3，通过%4，失败%5，阻塞%6，通过率%7%\n")
            .arg(it.key())
            .arg(counts.total)
            .arg(executedCount(counts))
            .arg(counts.passed)
            .arg(counts.failed)
            .arg(counts.blocked)
            .arg(passRate(counts), 0, 'f', 2);
    }

    stream << QStringLiteral("\n失败/阻塞归类：\n");
    if (failureCategories.isEmpty()) {
        stream << QStringLiteral("- 暂无\n");
    } else {
        for (auto it = failureCategories.constBegin(); it != failureCategories.constEnd(); ++it) {
            stream << QStringLiteral("- %1：%2\n").arg(it.key()).arg(it.value());
        }
    }
    return text;
}

QString TestSummaryBuilder::failureSummaryMarkdown(const QVector<TestCase> &cases, const QMap<QString, TestCaseResult> &results) const
{
    QString text;
    QTextStream stream(&text);
    stream << "| 用例ID | 模块 | 结果 | 失败归类 | 判定原因 | 缺陷编号 |\n";
    stream << "| --- | --- | --- | --- | --- | --- |\n";
    for (const TestCase &testCase : cases) {
        const TestCaseResult result = results.value(testCase.id);
        if (result.status != TestResultStatus::Failed && result.status != TestResultStatus::Blocked) {
            continue;
        }
        QString reason = result.judgeReason.isEmpty() ? result.actualResult : result.judgeReason;
        reason.replace("\n", "<br>");
        stream << "| " << testCase.id << " | " << testCase.module << " | "
               << testResultStatusText(result.status) << " | " << result.failureCategory
               << " | " << reason << " | " << result.defectId << " |\n";
    }
    return text;
}

QString TestSummaryBuilder::retestListText(const QVector<TestCase> &cases, const QMap<QString, TestCaseResult> &results) const
{
    QStringList lines;
    for (const TestCase &testCase : cases) {
        const TestCaseResult result = results.value(testCase.id);
        if (result.status != TestResultStatus::Failed && result.status != TestResultStatus::Blocked) {
            continue;
        }
        lines << QStringLiteral("%1 [%2/%3] %4；原因=%5；复测次数=%6")
            .arg(testCase.id,
                 testCase.module,
                 testCase.priority,
                 testResultStatusText(result.status),
                 result.failureCategory.isEmpty() ? QStringLiteral("-") : result.failureCategory)
            .arg(result.retestCount);
    }
    return lines.isEmpty() ? QStringLiteral("暂无失败/阻塞复测项。") : lines.join(QStringLiteral("\n"));
}

QString TestSummaryBuilder::reportConclusion(const QVector<TestCase> &cases, const QMap<QString, TestCaseResult> &results) const
{
    SummaryCounts total;
    for (const TestCase &testCase : cases) {
        addStatus(&total, results.value(testCase.id).status);
    }
    if (total.failed == 0 && total.blocked == 0 && total.notRun == 0) {
        return QStringLiteral("结论：所有已导入用例均已执行且未发现失败/阻塞项，本轮测试建议通过。");
    }
    return QStringLiteral("结论：本轮共 %1 条用例，已执行 %2 条，失败 %3 条，阻塞 %4 条，未执行 %5 条；建议优先处理失败/阻塞和 P0 未执行项后再关闭测试。")
        .arg(total.total)
        .arg(executedCount(total))
        .arg(total.failed)
        .arg(total.blocked)
        .arg(total.notRun);
}
