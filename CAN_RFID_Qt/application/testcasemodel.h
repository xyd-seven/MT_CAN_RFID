#ifndef TESTCASEMODEL_H
#define TESTCASEMODEL_H

#include "testsession.h"

#include <QAbstractTableModel>
#include <QMap>
#include <QVector>

class TestCaseModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit TestCaseModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setCases(const QVector<TestCase> &cases);
    void setResults(const QMap<QString, TestCaseResult> &results);
    void setFilters(const QString &module, const QString &priority, const QString &status, const QString &keyword);

    TestCase caseAt(int row) const;

private:
    void rebuildVisibleRows();
    bool matchesFilters(const TestCase &testCase) const;
    QColor statusColor(TestResultStatus status) const;

    QVector<TestCase> m_cases;
    QMap<QString, TestCaseResult> m_results;
    QVector<int> m_visibleRows;
    QString m_moduleFilter;
    QString m_priorityFilter;
    QString m_statusFilter;
    QString m_keyword;
};

#endif // TESTCASEMODEL_H
