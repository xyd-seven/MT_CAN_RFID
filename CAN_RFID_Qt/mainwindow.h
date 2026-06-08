#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <zlgcan.h>
#include "canthread.h"
#include <QThread>
#include <QCloseEvent>
#include <QLabel>
#include <QWidget>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGroupBox>
#include <QTimer>
#include "rfidprotocol.h"
#include "application/appconfig.h"
#include "application/logservice.h"
#include "application/otaservice.h"
#include "application/rfidservice.h"
#include "application/stresstestservice.h"
#include <QProgressBar>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void handleRecvedFrames(const QVector<CanFrame> &frames);
    void on_cleanListBtn_clicked();

    void on_openDeviceBtn_clicked();

    void on_closeDeviceBtn_clicked();

    void on_initCANBtn_clicked();

    void on_StartCANBtn_clicked();

    bool afterReSet();

    void on_reSetCANBtn_clicked();

    void on_sendBtn_clicked();

    void AddDataToList(QStringList strList);

    void closeEvent(QCloseEvent *event);
private:
    void setupRfidPanel();
    QWidget *createRfidMonitorTab(QWidget *parent);
    QWidget *createStressTestTab(QWidget *parent);
    QWidget *createOtaTab(QWidget *parent);
    void handleRfidFrame(const CanFrame &frame);
    void updateRfidPanel(const RfidState &state);
    void updateStressTestPanel(const StressTestStats &stats);
    void sendRfidFrame(UINT canId, const QByteArray &payload);
    void addCanFrameToList(const CanFrame &frame);
    void setLabelValue(QLabel *label, const QString &value);
    void setupCanLogSaveButton();
    void setupStatusPanel();
    void setupCompactMainLayout();
    void updateCanControlState(bool deviceOpened, bool canInitialized, bool canStarted);
    void updateControlsState();
    bool isOtaRunning() const;
    void oneClickStartCan();
    void startStressTest();
    void stopStressTest(bool autoStopped);
    void refreshStressTestTick();
    bool exportStressSummary();
    void exportCanLogSnapshot();
    void loadAppConfig();
    void saveAppConfig();

    Ui::MainWindow *ui;
    CANThread *canthread;
    QLabel *rfidWorkModeValue;
    QLabel *rfidCardStatusValue;
    QLabel *rfidFaultStatusValue;
    QLabel *rfidScanPeriodValue;
    QLabel *rfidTagValue;
    QLabel *rfidTagPart1Value;
    QLabel *rfidTagPart2Value;
    QLabel *rfidTagPart3Value;
    QLabel *rfidDeviceIdValue;
    QLabel *rfidVersionValue;
    QLabel *rfidResponseValue;
    QSpinBox *rfidScanPeriodSpin;
    RfidService rfidService;
    StressTestService stressTestService;
    AppConfig appConfig;
    LogService logService;
    int maxLogRows;
    bool canStarted;
    QTimer *stressRefreshTimer;
    QLabel *stressStateValue;
    QLabel *stressElapsedValue;
    QLabel *stressTotalSamplesValue;
    QLabel *stressSuccessCountValue;
    QLabel *stressNoTagCountValue;
    QLabel *stressTagLengthErrorValue;
    QLabel *stressFaultCountValue;
    QLabel *stressSuccessRateValue;
    QLabel *stressTagValidRateValue;
    QLabel *stressCurrentTagValue;
    QLabel *stressLastSuccessTagValue;
    QLabel *stressTagChangeCountValue;
    QLabel *stressUniqueTagCountValue;
    QLabel *stressMaxContinuousFailureValue;
    QLabel *stressLastFailureReasonValue;
    QCheckBox *stressAutoSaveCheckBox;
    QCheckBox *stressAutoExportSummaryCheckBox;
    QSpinBox *stressDurationSecondsSpin;
    QSpinBox *stressTargetSamplesSpin;
    QCheckBox *canAutoSaveCheckBox;
    QPushButton *saveCanLogButton;
    QPushButton *oneClickStartButton;
    QLabel *canDeviceStatusValue;
    QLabel *topCanStatusValue;
    QLabel *topRfidStatusValue;
    QLabel *topStressStatusValue;
    QString logDirectory;
    OtaService otaService;
    QLabel *otaStateValue;
    QLabel *otaMessageValue;
    QLabel *otaFirmwarePathValue;
    QProgressBar *otaProgressBar;
    QTimer *testerPresentTimer;
    QTimer *rfidControlTimer;
    QTimer *rfidOnlineCheckTimer;
    QDateTime lastRfidFrameTime;
    QLabel *rfidOnlineStatusValue;
    QProgressBar *stressProgressBar;
    QLabel *stressRemainingLabel;
    bool rfidScanning;
    bool deviceOpened;
    bool canInitialized;

    QGroupBox *statusGroup;
    QTabWidget *rfidTabs;

    QPushButton *rfidStartScanBtn;
    QPushButton *rfidStopScanBtn;
    QPushButton *rfidRestartBtn;
    QPushButton *rfidSetPeriodBtn;
    QCheckBox *rfidControlEnabledCheck;
    QCheckBox *show0x207LogCheck;

    QPushButton *stressStartBtn;
    QPushButton *stressStopBtn;
    QPushButton *stressResetBtn;
    QPushButton *stressExportBtn;

    QPushButton *otaQueryBtn;
    QPushButton *otaStartUpgradeBtn;
    QPushButton *otaAbortUpgradeBtn;
    QPushButton *otaSelectFileBtn;

    // OTA 升级压力测试 UI 控件
    QCheckBox *otaStressTestEnabledCheck;
    QSpinBox *otaStressCyclesSpin;
    QSpinBox *otaCooldownSpin;
    QCheckBox *otaStressSuspendLogCheck;
    QLabel *otaCurrentCycleLabel;
    QLabel *otaSuccessCyclesLabel;
    QLabel *otaFailureCyclesLabel;
    QLabel *otaStressSuccessRateLabel;
    QLabel *otaLastFailureReasonLabel;

    // OTA 异常注入测试 UI 控件
    QGroupBox *otaErrorInjectionGroup;
    QCheckBox *otaInjectMasterCheck;
    QCheckBox *otaInjectCrcErrorCheck;
    QCheckBox *otaInjectSeqErrorCheck;
    QCheckBox *otaInjectHwMismatchCheck;
    QCheckBox *otaInjectSilentTimeoutCheck;
    QCheckBox *otaInjectIgnoreFcCheck;
    QCheckBox *otaInjectIsoTpSnCheck;
    QCheckBox *otaInjectOutOfOrderCheck;

    // OTA 升级压力测试状态变量
    bool m_otaStressRunning;
    int m_otaCurrentCycle;
    int m_otaTargetCycles;
    int m_otaSuccessCount;
    int m_otaFailureCount;
    QString m_otaLastFailureReason;
    bool m_originalLogEnabled;
    QTimer *m_otaCooldownTimer;

private slots:
    void updateOtaStressUI();
    void handleOtaStateChangeForStressTest(OtaService::State state, const QString &message);
    void onOtaInjectMasterToggled(bool checked);
    OtaErrorConfig getOtaErrorConfig() const;
};

#endif // MAINWINDOW_H
