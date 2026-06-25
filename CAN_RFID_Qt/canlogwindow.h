#ifndef CANLOGWINDOW_H
#define CANLOGWINDOW_H

#include <QWidget>
#include <QStringList>
#include <QVector>

class QTableWidget;

class CanLogWindow : public QWidget
{
    Q_OBJECT

public:
    explicit CanLogWindow(QWidget *parent = nullptr);

    void setHeaders(const QStringList &headers);
    void setMaxRows(int rows);
    void appendRows(const QVector<QStringList> &rows);
    void clearRows();
    void copyFromTable(QTableWidget *sourceTable);

private:
    QTableWidget *logTable;
    int maxRows;
};

#endif // CANLOGWINDOW_H
