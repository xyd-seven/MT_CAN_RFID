#include "canlogwindow.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtGlobal>

CanLogWindow::CanLogWindow(QWidget *parent)
    : QWidget(parent),
      logTable(new QTableWidget(this)),
      maxRows(1000)
{
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowTitle(QStringLiteral("CAN日志窗口"));
    resize(1100, 600);

    logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable->verticalHeader()->setVisible(false);
    logTable->horizontalHeader()->setStretchLastSection(true);

    QPushButton *clearButton = new QPushButton(QStringLiteral("清空窗口"), this);
    connect(clearButton, &QPushButton::clicked, this, &CanLogWindow::clearRows);
    QPushButton *closeButton = new QPushButton(QStringLiteral("关闭窗口"), this);
    connect(closeButton, &QPushButton::clicked, this, &CanLogWindow::close);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(clearButton);
    buttonLayout->addWidget(closeButton);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    layout->addWidget(logTable, 1);
    layout->addLayout(buttonLayout);
}

void CanLogWindow::setHeaders(const QStringList &headers)
{
    logTable->setColumnCount(headers.count());
    logTable->setHorizontalHeaderLabels(headers);
}

void CanLogWindow::setMaxRows(int rows)
{
    maxRows = qMax(1, rows);
    const int overflow = logTable->rowCount() - maxRows;
    for (int i = 0; i < overflow; ++i) {
        logTable->removeRow(0);
    }
}

void CanLogWindow::appendRows(const QVector<QStringList> &rows)
{
    if (rows.isEmpty()) {
        return;
    }

    QScrollBar *vBar = logTable->verticalScrollBar();
    const bool wasAtBottom = (vBar == nullptr || vBar->value() == vBar->maximum());

    const int overflow = logTable->rowCount() + rows.size() - maxRows;
    for (int i = 0; i < overflow; ++i) {
        logTable->removeRow(0);
    }

    const int firstRow = logTable->rowCount();
    logTable->setRowCount(firstRow + rows.size());
    for (int rowOffset = 0; rowOffset < rows.size(); ++rowOffset) {
        const QStringList &rowData = rows.at(rowOffset);
        const int row = firstRow + rowOffset;
        for (int column = 0; column < rowData.count() && column < logTable->columnCount(); ++column) {
            QTableWidgetItem *item = new QTableWidgetItem(rowData.at(column));
            if (column != rowData.count() - 1) {
                item->setTextAlignment(Qt::AlignCenter | Qt::AlignHCenter);
            }
            logTable->setItem(row, column, item);
        }
    }

    if (wasAtBottom) {
        logTable->scrollToBottom();
    }
}

void CanLogWindow::clearRows()
{
    logTable->setRowCount(0);
}

void CanLogWindow::copyFromTable(QTableWidget *sourceTable)
{
    if (sourceTable == nullptr) {
        return;
    }

    QStringList headers;
    for (int column = 0; column < sourceTable->columnCount(); ++column) {
        QTableWidgetItem *headerItem = sourceTable->horizontalHeaderItem(column);
        headers << (headerItem == nullptr ? QString() : headerItem->text());
    }
    setHeaders(headers);

    logTable->setUpdatesEnabled(false);
    logTable->setRowCount(0);
    
    int totalSourceRows = sourceTable->rowCount();
    constexpr int InitialCopyRows = 200;
    int startRow = qMax(0, totalSourceRows - InitialCopyRows);
    
    QVector<QStringList> rows;
    rows.reserve(totalSourceRows - startRow);
    for (int row = startRow; row < totalSourceRows; ++row) {
        QStringList rowData;
        for (int column = 0; column < sourceTable->columnCount(); ++column) {
            QTableWidgetItem *item = sourceTable->item(row, column);
            rowData << (item == nullptr ? QString() : item->text());
        }
        rows.append(rowData);
    }
    appendRows(rows);
    logTable->setUpdatesEnabled(true);
}
