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
#include "application/qingjucanmanager.h"
#include "application/qingjurfidservice.h"
#include "application/qingjuotaservice.h"
#include <QProgressBar>
#include <QStackedWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>

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
    QString protocolDecodeText(const CanFrame &frame) const;
    QString qingjuAddressName(quint8 address) const;
    void setLabelValue(QLabel *label, const QString &value);
    bool parseQingjuAddress(const QString &text, quint8 *address, QString *error) const;
    void clearQingjuRfidPanel();
    void updateQingjuOnlineStatus(bool clearOfflineData);
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
    QDateTime lastQingjuNpkFrameTime;
    QDateTime lastQingjuRfrFrameTime;
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
    QStackedWidget *stressStatsStackedWidget;
    QWidget *mtStressPanel;
    QWidget *qjStressPanel;
    QLabel *qjStressStateValue;
    QLabel *qjStressElapsedValue;
    QLabel *qjStressTotalSamplesValue;
    QLabel *qjStressSuccessCountValue;
    QLabel *qjStressSuccessRateValue;
    QLabel *qjStressCurrentUidValue;
    QLabel *qjStressLastSuccessUidValue;
    QLabel *qjStressUniqueUidCountValue;
    QLabel *qjStressNoTagCountValue;
    QLabel *qjStressUidReadErrorValue;
    QLabel *qjStressModuleFaultValue;
    QLabel *qjStressCommunicationFaultValue;
    QLabel *qjStressContentErrorValue;
    QLabel *qjStressMaxContinuousFailureValue;
    QLabel *qjStressLastFailureReasonValue;

    QPushButton *otaQueryBtn;
    QPushButton *otaStartUpgradeBtn;
    QPushButton *otaAbortUpgradeBtn;
    QPushButton *otaSelectFileBtn;

    // OTA 升级压力测试 UI 控件
    QCheckBox *otaStressTestEnabledCheck;
    QSpinBox *otaStressCyclesSpin;
    QSpinBox *otaCooldownSpin;
    QCheckBox *otaStressSuspendLogCheck;
    QLabel *qjOtaTargetLabel;
    QComboBox *qjOtaTargetCombo;
    QLabel *otaCurrentCycleLabel;
    QLabel *otaSuccessCyclesLabel;
    QLabel *otaFailureCyclesLabel;
    QLabel *otaStressSuccessRateLabel;
    QLabel *otaLastFailureReasonLabel;

    // OTA 异常注入测试 UI 控件
    QGroupBox *otaErrorInjectionGroup;
    QGroupBox *otaStressGroup;
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
    void handleQingjuOtaStateChangeForStressTest(QingjuOtaService::State state, const QString &message);
    void onOtaInjectMasterToggled(bool checked);
    OtaErrorConfig getOtaErrorConfig() const;
    QingjuOtaErrorConfig getQingjuOtaErrorConfig() const;
    quint8 selectedQingjuOtaTarget() const;

    // 青桔协议槽函数及方法
    void onProtocolModeChanged(int index);
    void updateQingjuRfidPanel(const QingjuNpkState &state);
    void onQjCustomWriteClicked();
    void onQjCustomReadClicked();
    void handleQjCustomResponse(quint8 srcAddr, quint8 destAddr, quint8 funcCode, const QByteArray &payload);
    void handleQjCustomLog(const QString &text);
    QWidget *createQjRfidMonitorPanel(QWidget *parent);

private:
    // 青桔协议服务及管理器
    QComboBox *protocolModeCombo;
    QStackedWidget *rfidStackedWidget;
    QWidget *mtRfidPanel;
    QWidget *qjRfidPanel;
    QingjuCanManager *qingjuCanManager;
    QingjuRfidService *qingjuRfidService;
    QingjuOtaService *qingjuOtaService;
    QSpinBox *qjRfidPeriodSpin;
    QPushButton *qjStartBtn;
    QPushButton *qjStopBtn;
    QCheckBox *qjAutoWritePwdCheckBox;
    QComboBox *qjTargetDeviceCombo;
    QComboBox *qjQueryModeCombo;
    QSpinBox *qjHostPollPeriodSpin;
    QPushButton *qjDistanceQueryOnceBtn;
    QGroupBox *qjRfidAssetGroup;
    QGroupBox *qjRfidStatusGroup;

    // 青桔 NPK 监控标签
    QLabel *qjRfidAddrValue;
    QLabel *qjRfidResultValue;
    QLabel *qjRfidAppStatusValue;
    QLabel *qjRfidAlarmValue;
    QLabel *qjRfidUidValue;
    QLabel *qjRfidPwdValue;
    QLabel *qjRfidModelValue;
    QLabel *qjRfidSupplierValue;
    QLabel *qjRfidSerialValue;
    QLabel *qjRfidVendorValue;
    QLabel *qjRfidModelCodeValue;
    QLabel *qjRfidFwStrValue;
    QLabel *qjRfidHwStrValue;
    QLabel *qjRfidSnValue;
    QLabel *qjRfidFirmwareVerValue;
    QLabel *qjRfidHardwareVerValue;

    // 自定义寄存器读写控制
    QComboBox *qjRegisterPresetCombo;
    QComboBox *qjDestAddrCombo;
    QLineEdit *qjRegAddrEdit;
    QLineEdit *qjRegValueEdit;
    QComboBox *qjFuncCodeCombo;
    QPushButton *qjCustomWriteBtn;
    QPushButton *qjCustomReadBtn;
    QTextEdit *qjCustomLog;
    bool qjCustomRequestPending;
    quint8 qjCustomExpectedSrc;
    quint8 qjCustomExpectedFunc;

    // 青桔 OTA 异常测试面板
    QGroupBox *qjOtaAnomalyGroup;
    QComboBox *qjOtaAnomalyCombo;
    QCheckBox *qjOtaAnomalyEnableCheck;
};

#endif // MAINWINDOW_H
