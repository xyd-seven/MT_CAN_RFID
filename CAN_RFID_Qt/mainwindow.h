#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <zlgcan.h>
#include "canthread.h"
#include <QThread>
#include <QCloseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QLabel>
#include <QWidget>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGroupBox>
#include <QTimer>
#include <QAction>
#include "rfidprotocol.h"
#include "application/appconfig.h"
#include "application/logservice.h"
#include "application/otaservice.h"
#include "application/productiontestservice.h"
#include "application/rfiddiagnostictransfer.h"
#include "application/rfidservice.h"
#include "application/stresstestservice.h"
#include "application/testcasemodel.h"
#include "application/testcasejudge.h"
#include "application/testcaseservice.h"
#include "application/testsummarybuilder.h"
#include "application/qingjucanmanager.h"
#include "application/qingjurfidservice.h"
#include "application/qingjuotaservice.h"
#include "application/rs485worker.h"
#include <QProgressBar>
#include <QStackedWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QTableView>
#include <QVector>

namespace Ui {
class MainWindow;
}

class QSplitter;
class CanLogWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void requestRs485OpenPort(const QString &portName, int baudRate);
    void requestRs485ClosePort();
    void requestRs485SetProtocolMode(int mode);
    void requestRs485StartScan(int hostPollIntervalMs, int readMode);
    void requestRs485StopScan();
    void requestRs485QueryDeviceInfo();
    void requestRs485TriggerSingleQuery();
    void requestRs485SetPower(int powerRaw01Dbm);
    void requestRs485QueryPower();
    void requestRs485FfReboot();
    void requestRs485FfSetDemodulatorParams(int mixer, int ifAmp, int thrd);
    void requestRs485FfQueryDemodulatorParams();
    void requestRs485FfQueryCardSwitch();
    void requestRs485SetHlConfig(quint32 timeMs, int intervalMs, int savedCount, int clearAfter, int decrypt);
    void requestRs485HlWriteScanControl(int startStop, quint32 timeMs, int intervalMs, int savedCount, int clearAfter, int decrypt);
    void requestRs485HlRebootDevice();
    void requestRs485SendRawData(const QByteArray &data);
    void requestHlOtaQueryProgramStatus();
    void requestHlOtaStartUpgrade(const QString &firmwarePath);
    void requestHlOtaAbortUpgrade();
    void requestBbFfOtaStartUpgrade(const QString &firmwarePath, const QString &versionStr);
    void requestBbFfOtaAbortUpgrade();

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
    bool eventFilter(QObject *watched, QEvent *event);

protected:
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupRfidPanel();
    QWidget *createRfidMonitorTab(QWidget *parent);
    QWidget *createStressTestTab(QWidget *parent);
    QWidget *createOtaTab(QWidget *parent);
    QWidget *createProductionTestTab(QWidget *parent);
    QWidget *createTestExecutionTab(QWidget *parent);
    void handleRfidFrame(const CanFrame &frame);
    void updateRfidPanel(const RfidState &state);
    void updateStressTestPanel(const StressTestStats &stats);
    void updateProductionTestPanel(const ProductionTestState &state);
    void appendProductionTestLog(const QString &message);
    void startProductionTestFromInput();
    void startProductionTestFromSn(const QString &sn);
    void stopProductionTest();
    void clearProductionTestPanel();
    void prepareProductionSnInput();
    void processProductionScanText(const QString &text, bool showError);
    void sendProductionScanControl(bool enabled);
    void abortRfidDiagnosticTransferSilently();
    void sendRfidFrame(UINT canId, const QByteArray &payload);
    void logSentRfidFrame(UINT canId, const QByteArray &payload);
    void addCanFrameToList(const CanFrame &frame);
    void flushPendingLogRows();
    QString protocolDecodeText(const CanFrame &frame) const;
    QString qingjuAddressName(quint8 address) const;
    void setLabelValue(QLabel *label, const QString &value);
    bool parseQingjuAddress(const QString &text, quint8 *address, QString *error) const;
    void clearQingjuRfidPanel();
    void updateMeituanTopStatus();
    void updateQingjuOnlineStatus(bool clearOfflineData);
    void setupCanLogSaveButton();
    void setupStatusPanel();
    void setupCompactMainLayout();
    void updateCanControlState(bool deviceOpened, bool canInitialized, bool canStarted);
    void updateControlsState();
    void updateTestExecutionControls();
    void refreshTestCaseFilters();
    void refreshTestCaseDetail();
    void refreshTestCaseModel();
    void startSelectedTestCase();
    void saveSelectedTestCaseResult();
    void exportTestCaseResults();
    void exportTestCaseResultsExcel();
    void exportTestReportMarkdown();
    void exportTestReportPdf();
    void judgeSelectedTestCase();
    void safeRunAndJudgeSelectedTestCase();
    void bindStressStatsToSelectedTestCase();
    void runSelectedTestCaseAuto();
    void runSelectedTestCaseSemiAssist();
    void runFilteredTestCases();
    void retestFailedCases();
    void applyTestTemplatePreset(int index);
    void createTestSession();
    void openExistingTestSession();
    void openTestSessionDirectory();
    void importTestCases();
    void appendTestEvidenceFrame(const CanFrame &frame, const QString &decodedText);
    void resetCurrentCaseEvidenceView(const QString &message);
    void updateTestSessionStats();
    void updateTestExecutionSummary();
    void updateEvidenceSummary();
    void copySelectedTestCaseKeyFrames();
    void updateTestExecutionTabAvailability();
    void markTestResultDirty();
    bool confirmSaveOrDiscardTestResultChanges();
    QString protocolExpectationText(const TestCase &testCase) const;
    QString judgeTestCaseEvidence(const TestCase &testCase, TestResultStatus *status) const;
    bool saveCurrentTestCaseResult(QString *error);
    bool precheckTestJudgeContext(const TestCase &testCase, QString *reason) const;
    bool precheckTestExecution(const TestCase &testCase, QString *reason) const;
    bool confirmOverwriteEvidenceForCase(const QString &caseId, const QString &actionText);
    bool sendAutoTestCommand(const TestCase &testCase, QString *message);
    bool sendSemiAssistCommand(const TestCase &testCase, QString *message);
    QString readEvidenceText(const QString &caseId) const;
    void refreshProgressBoard();
    QString testReportMarkdown() const;
    QString testReportHtml() const;
    QString moduleStatsText() const;
    QString selectedCaseId() const;
    void updateRs485TopStatus();
    void updateManualSendPanelMode();
    bool isOtaRunning() const;
    void oneClickStartCan();
    void startStressTest();
    void stopStressTest(bool autoStopped);
    void refreshStressTestTick();
    bool exportStressSummary();
    void exportCanLogSnapshot();
    void loadAppConfig();
    void saveAppConfig();
    void restoreLayoutConfig(const AppConfigData &config);
    void applyCanLogCompact(bool compact);
    void openCanLogWindow();
    void syncCanLogWindowRows(const QVector<QStringList> &rows);

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
    ProductionTestService productionTestService;
    TestCaseService testCaseService;
    TestCaseJudge testCaseJudge;
    TestSummaryBuilder testSummaryBuilder;
    TestCaseModel *testCaseModel;
    AppConfig appConfig;
    LogService logService;
    int maxLogRows;
    QTimer *logFlushTimer;
    QVector<QStringList> pendingLogRows;
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
    QPushButton *compactCanLogButton;
    QPushButton *popCanLogButton;
    QSplitter *mainVerticalSplitter;
    QSplitter *testCaseSplitter;
    CanLogWindow *canLogWindow;
    bool canLogCompact;
    QPushButton *oneClickStartButton;
    QLabel *canDeviceStatusValue;
    QLabel *topCanStatusValue;
    QLabel *topRfidStatusValue;
    QLabel *topStressStatusValue;
    QLabel *manualSendIdLabel;
    QLabel *manualSendDataLabel;
    QLabel *manualSendHintLabel;
    QString logDirectory;
    OtaService otaService;
    RfidDiagnosticTransfer rfidDiagnosticTransfer;
    bool productionWritePending;
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
    bool testSavedRfidControlTimerActive;
    bool testSavedRfidScanning;
    bool testHasSavedRfidControlState;

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
    QWidget *productionTestTab;
    QWidget *testExecutionTab;
    QLineEdit *testProjectEdit;
    QLineEdit *testSoftwareVersionEdit;
    QLineEdit *testFirmwareVersionEdit;
    QLineEdit *testDeviceSnEdit;
    QLineEdit *testTesterEdit;
    QComboBox *testEnvironmentCombo;
    QTextEdit *testSessionRemarkEdit;
    QLabel *testSessionDirectoryValue;
    QComboBox *testModuleFilterCombo;
    QComboBox *testPriorityFilterCombo;
    QComboBox *testResultFilterCombo;
    QLineEdit *testSearchEdit;
    QLabel *testStatsValue;
    QTableView *testCaseTableView;
    QLabel *testCaseTitleValue;
    QTextEdit *testCaseDetailText;
    QTextEdit *testExpectationText;
    QTabWidget *testDetailTabs;
    QTextEdit *testExecutionSummaryText;
    QComboBox *testResultCombo;
    QTextEdit *testActualResultEdit;
    QLineEdit *testDefectIdEdit;
    QTextEdit *testCaseRemarkEdit;
    QLabel *testEvidenceSummaryValue;
    QTextEdit *testEvidenceLogText;
    QTextEdit *testModuleStatsText;
    QTextEdit *testProgressBoardText;
    QTextEdit *testRetestListText;
    QPushButton *testNewSessionBtn;
    QPushButton *testOpenSessionBtn;
    QPushButton *testStartCaseBtn;
    QPushButton *testJudgeCaseBtn;
    QPushButton *testSafeRunJudgeBtn;
    QPushButton *testRunAutoBtn;
    QPushButton *testRunFilteredBtn;
    QPushButton *testRetestFailedBtn;
    QPushButton *testBindStressBtn;
    QPushButton *testSaveResultBtn;
    QAction *testExportResultAction;
    QAction *testExportExcelAction;
    QAction *testExportMarkdownAction;
    QAction *testExportPdfAction;
    QPushButton *testCopyKeyFramesBtn;
    QPushButton *testOpenSessionDirBtn;
    QComboBox *testTemplatePresetCombo;
    QCheckBox *testFailPauseCheck;
    QCheckBox *autoCompactLogOnTestExecutionCheck;
    bool testResultDirty;
    bool loadingTestCaseDetail;
    bool testBatchOverwriteConfirmed;
    QLineEdit *productionSnEdit;
    QLineEdit *productionHwVerEdit;
    QLineEdit *productionMatChangeEdit;
    QLabel *productionMatChangeLabel;
    QPushButton *productionHwVerLockBtn;
    bool productionHwVerLocked;
    quint16 m_qingjuWritePendingRegister;
    quint16 m_qingjuWritePendingRegCount;
    QTimer *productionWriteTimer;
    QLabel *productionResultBanner;
    QLabel *productionStateValue;
    QLabel *productionSnValue;
    QLabel *productionWriteValue;
    QLabel *productionProgressValue;
    QLabel *productionSuccessValue;
    QLabel *productionFailureValue;
    QLabel *productionRateValue;
    QLabel *productionTagValue;
    QLabel *productionFailureReasonValue;
    QSpinBox *productionPassThresholdSpin;
    QProgressBar *productionProgressBar;
    QPushButton *productionStartBtn;
    QPushButton *productionStopBtn;
    QPushButton *productionClearBtn;
    QTextEdit *productionLogText;
    QTimer *productionScanStableTimer;
    QString productionScanBuffer;
    QDateTime productionLastScanKeyTime;
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
    QLabel *otaVersionLabel;
    QLineEdit *otaVersionEdit;

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
    QCheckBox *otaInjectVendorMismatchCheck;
    QCheckBox *otaInjectHwMismatchCheck;
    QCheckBox *otaInjectA2FirstFrameErrorCheck;
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
    void handleHlOtaStateChanged(HlOtaService::State state, const QString &message);
    void handleBbFfOtaStateChanged(BbFfOtaService::State state, const QString &message);
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

    // RS485 界面方法及槽函数
    void onSerialOpenCloseClicked();
    void onSerialRefreshClicked();
    void onSerialOneClickStartClicked();
    void updateRs485RfidPanel(const Rs485State &state);
    void addSerialFrameToList(bool isTx, const QByteArray &data, const QString &decodeText);
    void onRs485CommandFinished(bool success, const QString &message);
    void onRs485PortOpened(bool success, const QString &message);
    void onRs485PortClosed();
    void onRs485ScanStateChanged(bool scanning);
    QWidget *createBbRfidMonitorPanel(QWidget *parent);
    QWidget *createFfRfidMonitorPanel(QWidget *parent);
    QWidget *createHlRfidMonitorPanel(QWidget *parent);
    void updateHlRfidPanel(const Rs485State &state);
    QWidget *createRs485StressPanel(QWidget *parent);
    void updateRs485Ports();
    void handleRs485Disconnect();
    void syncHlConfigToService();
    QString hlOtaStateText() const;
    QString bbFfOtaStateText() const;

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

    // RS485 相关服务、状态与界面控件指针
    QThread *rs485Thread;
    Rs485Worker *rs485Worker;
    bool serialOpened;
    bool rs485Scanning;
    bool rs485StartAfterOpen;
    int rs485PendingIntervalMs;
    int rs485PendingReadMode;
    HlOtaService::State hlOtaState;
    QString hlOtaMessage;
    BbFfOtaService::State bbFfOtaState;
    QString bbFfOtaMessage;

    // 串口设备面板 (左侧)
    QWidget *devicePanel;
    QWidget *serialDevicePanel;
    QComboBox *serialPortCombo;
    QComboBox *serialBaudRateCombo;
    QPushButton *serialOpenCloseBtn;
    QPushButton *serialRefreshBtn;
    QLabel *serialStatusLabel;
    QPushButton *serialOneClickStartBtn;

    // BB 协议监控面板
    QWidget *bbRfidPanel;
    QPushButton *bbStartBtn;
    QPushButton *bbStopBtn;
    QPushButton *bbQueryOnceBtn;
    QComboBox *bbQueryModeCombo;
    QSpinBox *bbHostPollPeriodSpin;
    QPushButton *bbQueryInfoBtn;
    QLabel *bbRfidHwVerVal;
    QLabel *bbRfidSwVerVal;
    QLabel *bbRfidMfgVal;
    QLabel *bbRfidDevIdVal;
    QSpinBox *bbPowerSpin;
    QPushButton *bbSetPowerBtn;
    QPushButton *bbQueryPowerBtn;
    QLabel *bbPowerVal;
    QLabel *bbTagIdVal;
    QLabel *bbRssiVal;
    QLabel *bbPcVal;
    QLabel *bbCrcVal;

    // FF 协议监控面板
    QWidget *ffRfidPanel;
    QPushButton *ffStartBtn;
    QPushButton *ffStopBtn;
    QPushButton *ffQueryOnceBtn;
    QComboBox *ffQueryModeCombo;
    QSpinBox *ffHostPollPeriodSpin;
    QPushButton *ffRebootBtn;
    QPushButton *ffQuerySwitchBtn;
    QLabel *ffCardSwitchVal;
    QPushButton *ffQueryInfoBtn;
    QLabel *ffRfidHwVerVal;
    QLabel *ffRfidSwVerVal;
    QLabel *ffRfidMfgVal;
    QLabel *ffRfidDevIdVal;
    QSpinBox *ffPowerSpin;
    QPushButton *ffSetPowerBtn;
    QPushButton *ffQueryPowerBtn;
    QLabel *ffPowerVal;
    QSpinBox *ffMixerSpin;
    QSpinBox *ffIfAmpSpin;
    QSpinBox *ffThrdSpin;
    QPushButton *ffSetDemodBtn;
    QPushButton *ffQueryDemodBtn;
    QLabel *ffMixerVal;
    QLabel *ffIfAmpVal;
    QLabel *ffThrdVal;
    QLabel *ffTagIdVal;

    // Hellobike protocol monitor panel
    QWidget *hlRfidPanel;
    QPushButton *hlStartBtn;
    QPushButton *hlStopBtn;
    QPushButton *hlQueryOnceBtn;
    QComboBox *hlQueryModeCombo;
    QSpinBox *hlHostPollPeriodSpin;
    QPushButton *hlRebootBtn;
    QPushButton *hlQueryInfoBtn;
    QLabel *hlRfidHwVerVal;
    QLabel *hlRfidSwVerVal;
    QLabel *hlRfidMfgVal;
    QLabel *hlRfidDevIdVal;
    QLabel *hlRfidProtoVerVal;
    QLabel *hlRfidProjectNoVal;
    
    QSpinBox *hlScanTimeSpin;
    QSpinBox *hlScanIntervalSpin;
    QSpinBox *hlSavedCountSpin;
    QCheckBox *hlClearAfterReadCheck;
    QCheckBox *hlDecryptEnableCheck;
    QPushButton *hlSetControlBtn;
    QLabel *hlTagIdVal;
    QLabel *hlScanStateVal;
    QLabel *hlErrorCodeVal;

    // RS485 压力测试统计面板 (QStackedWidget 子面板)
    QWidget *rs485StressPanel;
    QLabel *rs485StressStateValue;
    QLabel *rs485StressElapsedValue;
    QLabel *rs485StressSuccessRateValue;
    QLabel *rs485StressTotalSamplesValue;
    QLabel *rs485StressPollSkippedValue;
    QLabel *rs485StressSuccessCountValue;
    QLabel *rs485StressNoTagCountValue;
    QLabel *rs485StressModuleFaultValue;
    QLabel *rs485StressCommunicationFaultValue;
    QLabel *rs485StressCurrentTagValue;
    QLabel *rs485StressLastSuccessTagValue;
    QLabel *rs485StressUniqueTagCountValue;
    QLabel *rs485StressMaxContinuousFailureValue;
    QLabel *rs485StressLastFailureReasonValue;
};

#endif // MAINWINDOW_H
