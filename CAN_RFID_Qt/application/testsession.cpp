#include "testsession.h"

QString testResultStatusText(TestResultStatus status)
{
    switch (status) {
    case TestResultStatus::Passed:
        return QStringLiteral("通过");
    case TestResultStatus::Failed:
        return QStringLiteral("失败");
    case TestResultStatus::Blocked:
        return QStringLiteral("阻塞");
    case TestResultStatus::NotApplicable:
        return QStringLiteral("不适用");
    case TestResultStatus::NotRun:
    default:
        return QStringLiteral("未执行");
    }
}

TestResultStatus testResultStatusFromText(const QString &text)
{
    if (text == QStringLiteral("通过")) {
        return TestResultStatus::Passed;
    }
    if (text == QStringLiteral("失败")) {
        return TestResultStatus::Failed;
    }
    if (text == QStringLiteral("阻塞")) {
        return TestResultStatus::Blocked;
    }
    if (text == QStringLiteral("不适用")) {
        return TestResultStatus::NotApplicable;
    }
    return TestResultStatus::NotRun;
}
