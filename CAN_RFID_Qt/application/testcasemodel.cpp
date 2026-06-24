#include "testcasemodel.h"

#include <QBrush>
#include <QColor>

namespace {
constexpr int ColumnId = 0;
constexpr int ColumnModule = 1;
constexpr int ColumnPriority = 2;
constexpr int ColumnType = 3;
constexpr int ColumnStatus = 4;
constexpr int ColumnUpdated = 5;
constexpr int ColumnCount = 6;
}

TestCaseModel::TestCaseModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int TestCaseModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleRows.size();
}

int TestCaseModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TestCaseModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size()) {
        return QVariant();
    }

    const TestCase &testCase = m_cases.at(m_visibleRows.at(index.row()));
    const TestCaseResult result = m_results.value(testCase.id);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColumnId:
            return testCase.id;
        case ColumnModule:
            return testCase.module;
        case ColumnPriority:
            return testCase.priority;
        case ColumnType:
            return testCase.type;
        case ColumnStatus:
            return testResultStatusText(result.status);
        case ColumnUpdated:
            return result.finishedAt.isValid() ? result.finishedAt.toString("MM-dd hh:mm") : QStringLiteral("-");
        default:
            return QVariant();
        }
    }

    if (role == Qt::BackgroundRole && index.column() == ColumnStatus) {
        return QBrush(statusColor(result.status));
    }

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ColumnPriority || index.column() == ColumnStatus || index.column() == ColumnUpdated) {
            return Qt::AlignCenter;
        }
        return Qt::AlignVCenter;
    }

    return QVariant();
}

QVariant TestCaseModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }

    switch (section) {
    case ColumnId:
        return QStringLiteral("用例ID");
    case ColumnModule:
        return QStringLiteral("模块");
    case ColumnPriority:
        return QStringLiteral("优先级");
    case ColumnType:
        return QStringLiteral("类型");
    case ColumnStatus:
        return QStringLiteral("结果");
    case ColumnUpdated:
        return QStringLiteral("更新时间");
    default:
        return QVariant();
    }
}

void TestCaseModel::setCases(const QVector<TestCase> &cases)
{
    beginResetModel();
    m_cases = cases;
    rebuildVisibleRows();
    endResetModel();
}

void TestCaseModel::setResults(const QMap<QString, TestCaseResult> &results)
{
    beginResetModel();
    m_results = results;
    rebuildVisibleRows();
    endResetModel();
}

void TestCaseModel::setFilters(const QString &module, const QString &priority, const QString &status, const QString &keyword)
{
    beginResetModel();
    m_moduleFilter = module;
    m_priorityFilter = priority;
    m_statusFilter = status;
    m_keyword = keyword.trimmed();
    rebuildVisibleRows();
    endResetModel();
}

TestCase TestCaseModel::caseAt(int row) const
{
    if (row < 0 || row >= m_visibleRows.size()) {
        return TestCase();
    }
    return m_cases.at(m_visibleRows.at(row));
}

void TestCaseModel::rebuildVisibleRows()
{
    m_visibleRows.clear();
    for (int index = 0; index < m_cases.size(); ++index) {
        if (matchesFilters(m_cases.at(index))) {
            m_visibleRows.append(index);
        }
    }
}

bool TestCaseModel::matchesFilters(const TestCase &testCase) const
{
    if (!m_moduleFilter.isEmpty() && m_moduleFilter != QStringLiteral("全部") && testCase.module != m_moduleFilter) {
        return false;
    }
    if (!m_priorityFilter.isEmpty() && m_priorityFilter != QStringLiteral("全部") && testCase.priority != m_priorityFilter) {
        return false;
    }

    const TestCaseResult result = m_results.value(testCase.id);
    if (!m_statusFilter.isEmpty() && m_statusFilter != QStringLiteral("全部") &&
        testResultStatusText(result.status) != m_statusFilter) {
        return false;
    }

    if (!m_keyword.isEmpty()) {
        const QString joined = QStringLiteral("%1 %2 %3 %4 %5 %6")
            .arg(testCase.id, testCase.module, testCase.priority, testCase.type, testCase.steps, testCase.expectedResult);
        return joined.contains(m_keyword, Qt::CaseInsensitive);
    }
    return true;
}

QColor TestCaseModel::statusColor(TestResultStatus status) const
{
    switch (status) {
    case TestResultStatus::Passed:
        return QColor(226, 239, 218);
    case TestResultStatus::Failed:
        return QColor(252, 228, 214);
    case TestResultStatus::Blocked:
        return QColor(255, 242, 204);
    case TestResultStatus::NotApplicable:
        return QColor(232, 232, 232);
    case TestResultStatus::NotRun:
    default:
        return QColor(242, 242, 242);
    }
}
