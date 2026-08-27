#ifndef DUALPRODUCTIONWIDGET_H
#define DUALPRODUCTIONWIDGET_H

#include <QWidget>

#include "application/appconfig.h"
#include "application/productionlogservice.h"
#include "application/productiontestservice.h"

class QCheckBox;
class QCloseEvent;
class QGroupBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTextEdit;
class QTimer;
class ProductionStationController;
struct CanFrame;

class DualProductionWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DualProductionWidget(QWidget *parent = nullptr);
    ~DualProductionWidget() override;

    void loadConfig(const AppConfigData &config);
    void saveConfig(AppConfigData *config) const;
    void setDeviceType(quint32 deviceType);
    void setResistanceEnabled(bool enabled);
    void setProtocolMode(int protocolMode);
    void setMainCanBusy(bool busy);
    void setLogDirectory(const QString &directoryPath);
    bool handleKeyEvent(QKeyEvent *event);
    bool devicesActive() const;
    bool isAnyStationRunning() const;
    void startConfiguredDevices();
    void shutdown();

signals:
    void activityChanged();
    void configChanged();

private:
    struct StationPanel
    {
        QGroupBox *group = nullptr;
        QLabel *deviceStatus = nullptr;
        QLabel *resultBanner = nullptr;
        QLabel *snValue = nullptr;
        QLabel *phaseValue = nullptr;
        QLabel *progressValue = nullptr;
        QLabel *rateValue = nullptr;
        QLabel *failureCountValue = nullptr;
        QLabel *outcomeReasonLabel = nullptr;
        QLabel *failureReasonValue = nullptr;
        QLabel *hardwareVersionValue = nullptr;
        QLabel *softwareVersionValue = nullptr;
        QLabel *materialVersionValue = nullptr;
        QLabel *deviceIdValue = nullptr;
        QProgressBar *progressBar = nullptr;
        QPushButton *stopButton = nullptr;
        QPushButton *clearButton = nullptr;
    };

    void buildUi();
    QGroupBox *createStationGroup(int stationIndex, StationPanel *panel);
    void connectStation(int stationIndex);
    void startStationOneDevice();
    void startDevices();
    void stopDevices(bool showConfirmation);
    void toggleParameterLock();
    void processScan(const QString &text);
    void startStationTest(int stationIndex, const QString &sn);
    int selectAvailableStation() const;
    void setPreferredStation(int stationIndex);
    void updateUiState();
    void updateStationPanel(int stationIndex, const ProductionTestState &state);
    void handleStationFrame(int stationIndex, const CanFrame &frame);
    void resetStationReadback(int stationIndex);
    void appendLog(int stationIndex, const QString &message);
    void updateProductionStats();
    void saveProductionResult(int stationIndex, const ProductionTestState &state);
    void storeCurrentProtocolSettings();
    void restoreCurrentProtocolSettings();
    bool validateParameters(QString *error) const;

    ProductionStationController *m_stations[2];
    StationPanel m_panels[2];
    quint32 m_deviceType;
    bool m_resistanceEnabled;
    bool m_mainCanBusy;
    int m_protocolMode;
    int m_preferredStation;
    bool m_shuttingDown;
    bool m_meituanLocked;
    bool m_qingjuLocked;
    QString m_meituanHwVersion;
    QString m_qingjuHwVersion;
    QString m_materialChange;
    QString m_scanBuffer;
    QString m_deviceIdParts[2][2];
    ProductionLogService m_productionLogService;

    QSpinBox *m_deviceIndexSpins[2];
    QLabel *m_protocolLabel;
    QLabel *m_nextStationLabel;
    QLineEdit *m_scanEdit;
    QLineEdit *m_hwVersionEdit;
    QLineEdit *m_materialChangeEdit;
    QLabel *m_materialChangeLabel;
    QSpinBox *m_passThresholdSpin;
    QPushButton *m_parameterLockButton;
    QPushButton *m_startDevicesButton;
    QPushButton *m_stopDevicesButton;
    QPushButton *m_startTestButton;
    QLabel *m_statsDateValue;
    QLabel *m_statsTotalValue;
    QLabel *m_statsPassedValue;
    QLabel *m_statsFailedValue;
    QLabel *m_statsStoppedValue;
    QLabel *m_statsRateValue;
    QPushButton *m_openLogDirectoryButton;
    QLabel *m_systemLogLabel;
    QTextEdit *m_stationLogTexts[2];
    QTimer *m_scanTimer;
};

#endif // DUALPRODUCTIONWIDGET_H
