#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "canlogwindow.h"
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QGridLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QAbstractSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QDir>
#include <QTextStream>
#include <QIcon>
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QEventLoop>
#include <QKeyEvent>
#include <QScreen>
#include <QScrollArea>
#include <QRegularExpression>
#include <QScrollBar>
#include <QInputDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QPageSize>
#include <QPrinter>
#include <QStandardPaths>
#include <QTextDocument>
#include <QUrl>

namespace {
const QStringList DeviceTypeNames = {
    "ZQWL-UCANFD-100C", "ZQWL-UCAN-101C", "ZQWL-UCANFD-100K", "ZQWL-UCAN-101K",
    "ZQWL-UCANFD-100E", "ZQWL-UCAN-101E", "ZQWL-UCANFD-200U", "ZQWL-UCAN-201U",
    "ZQWL-UCANFD-200C", "ZQWL-UCAN-201C", "ZQWL-UCAN-401U", "ZQWL-UCANFD-400U"
};

const int DeviceTypeIndexes[] = {42, 3, 42, 3, 42, 3, 41, 4, 41, 4, 200, 201};
constexpr int MaxManualPayloadBytes = 64;
constexpr int LogFlushIntervalMs = 100;
constexpr int LogPruneBatchRows = 200;

QString normalizeHexText(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("0[xX]")), QString());
    text.replace(QStringLiteral(" "), QString());
    text.replace(QStringLiteral(","), QString());
    return text.trimmed();
}

bool parseHexUInt16(const QString &text, quint16 *value)
{
    QString normalized = normalizeHexText(text);
    if (normalized.isEmpty()) {
        return false;
    }
    bool ok = false;
    uint val = normalized.toUInt(&ok, 16);
    if (!ok || val > 0xFFFF) {
        return false;
    }
    if (value) {
        *value = static_cast<quint16>(val);
    }
    return true;
}

bool parseHexByteArray(const QString &text, QByteArray *data)
{
    QString normalized = normalizeHexText(text);
    if (normalized.isEmpty()) {
        if (data) {
            data->clear();
        }
        return true;
    }
    if (normalized.length() % 2 != 0) {
        return false;
    }
    QRegularExpression hexRegex(QStringLiteral("^[0-9a-fA-F]*$"));
    if (!hexRegex.match(normalized).hasMatch()) {
        return false;
    }
    if (data) {
        *data = QByteArray::fromHex(normalized.toLatin1());
    }
    return true;
}
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    canthread(nullptr),
    testCaseModel(new TestCaseModel(this)),
    maxLogRows(1000),
    logFlushTimer(new QTimer(this)),
    canStarted(false),
    stressRefreshTimer(new QTimer(this)),
    canAutoSaveCheckBox(nullptr),
    saveCanLogButton(nullptr),
    compactCanLogButton(nullptr),
    popCanLogButton(nullptr),
    layoutPresetCombo(nullptr),
    mainVerticalSplitter(nullptr),
    testCaseSplitter(nullptr),
    canLogWindow(nullptr),
    canLogCompact(false),
    oneClickStartButton(nullptr),
    canDeviceStatusValue(nullptr),
    topCanStatusValue(nullptr),
    topRfidStatusValue(nullptr),
    topStressStatusValue(nullptr),
    manualSendIdLabel(nullptr),
    manualSendDataLabel(nullptr),
    manualSendHintLabel(nullptr),
    logDirectory(),
    otaService(this),
    rfidDiagnosticTransfer(this),
    productionWritePending(false),
    testerPresentTimer(new QTimer(this)),
    rfidControlTimer(new QTimer(this)),
    rfidOnlineCheckTimer(new QTimer(this)),
    rfidOnlineStatusValue(nullptr),
    stressProgressBar(nullptr),
    stressRemainingLabel(nullptr),
    rfidScanning(false),
    deviceOpened(false),
    canInitialized(false),
    statusGroup(nullptr),
    rfidTabs(nullptr),
    rfidStartScanBtn(nullptr),
    rfidStopScanBtn(nullptr),
    rfidRestartBtn(nullptr),
    rfidSetPeriodBtn(nullptr),
    rfidControlEnabledCheck(nullptr),
    show0x207LogCheck(nullptr),
    stressStartBtn(nullptr),
    stressStopBtn(nullptr),
    stressResetBtn(nullptr),
    stressExportBtn(nullptr),
    stressStatsStackedWidget(nullptr),
    mtStressPanel(nullptr),
    qjStressPanel(nullptr),
    productionTestTab(nullptr),
    testExecutionTab(nullptr),
    testProjectEdit(nullptr),
    testSoftwareVersionEdit(nullptr),
    testFirmwareVersionEdit(nullptr),
    testDeviceSnEdit(nullptr),
    testTesterEdit(nullptr),
    testEnvironmentCombo(nullptr),
    testSessionRemarkEdit(nullptr),
    testSessionDirectoryValue(nullptr),
    testModuleFilterCombo(nullptr),
    testPriorityFilterCombo(nullptr),
    testResultFilterCombo(nullptr),
    testSearchEdit(nullptr),
    testStatsValue(nullptr),
    testCaseTableView(nullptr),
    testCaseTitleValue(nullptr),
    testCaseDetailText(nullptr),
    testExpectationText(nullptr),
    testResultCombo(nullptr),
    testActualResultEdit(nullptr),
    testDefectIdEdit(nullptr),
    testCaseRemarkEdit(nullptr),
    testEvidenceLogText(nullptr),
    testModuleStatsText(nullptr),
    testProgressBoardText(nullptr),
    testRetestListText(nullptr),
    testNewSessionBtn(nullptr),
    testStartCaseBtn(nullptr),
    testJudgeCaseBtn(nullptr),
    testSafeRunJudgeBtn(nullptr),
    testRunAutoBtn(nullptr),
    testRunFilteredBtn(nullptr),
    testRetestFailedBtn(nullptr),
    testBindStressBtn(nullptr),
    testSaveResultBtn(nullptr),
    testExportResultBtn(nullptr),
    testExportExcelBtn(nullptr),
    testExportMarkdownBtn(nullptr),
    testExportPdfBtn(nullptr),
    testOpenSessionDirBtn(nullptr),
    testTemplatePresetCombo(nullptr),
    testFailPauseCheck(nullptr),
    autoCompactLogOnTestExecutionCheck(nullptr),
    productionSnEdit(nullptr),
    productionResultBanner(nullptr),
    productionStateValue(nullptr),
    productionSnValue(nullptr),
    productionWriteValue(nullptr),
    productionProgressValue(nullptr),
    productionSuccessValue(nullptr),
    productionFailureValue(nullptr),
    productionRateValue(nullptr),
    productionTagValue(nullptr),
    productionFailureReasonValue(nullptr),
    productionPassThresholdSpin(nullptr),
    productionProgressBar(nullptr),
    productionStartBtn(nullptr),
    productionStopBtn(nullptr),
    productionClearBtn(nullptr),
    productionLogText(nullptr),
    productionScanStableTimer(new QTimer(this)),
    qjStressStateValue(nullptr),
    qjStressElapsedValue(nullptr),
    qjStressTotalSamplesValue(nullptr),
    qjStressSuccessCountValue(nullptr),
    qjStressSuccessRateValue(nullptr),
    qjStressCurrentUidValue(nullptr),
    qjStressLastSuccessUidValue(nullptr),
    qjStressUniqueUidCountValue(nullptr),
    qjStressNoTagCountValue(nullptr),
    qjStressUidReadErrorValue(nullptr),
    qjStressModuleFaultValue(nullptr),
    qjStressCommunicationFaultValue(nullptr),
    qjStressContentErrorValue(nullptr),
    qjStressMaxContinuousFailureValue(nullptr),
    qjStressLastFailureReasonValue(nullptr),
    otaQueryBtn(nullptr),
    otaStartUpgradeBtn(nullptr),
    otaAbortUpgradeBtn(nullptr),
    otaSelectFileBtn(nullptr),
    otaStressTestEnabledCheck(nullptr),
    otaStressCyclesSpin(nullptr),
    otaCooldownSpin(nullptr),
    otaStressSuspendLogCheck(nullptr),
    qjOtaTargetLabel(nullptr),
    qjOtaTargetCombo(nullptr),
    otaCurrentCycleLabel(nullptr),
    otaSuccessCyclesLabel(nullptr),
    otaFailureCyclesLabel(nullptr),
    otaStressSuccessRateLabel(nullptr),
    otaLastFailureReasonLabel(nullptr),
    otaErrorInjectionGroup(nullptr),
    otaStressGroup(nullptr),
    otaInjectMasterCheck(nullptr),
    otaInjectCrcErrorCheck(nullptr),
    otaInjectSeqErrorCheck(nullptr),
    otaInjectHwMismatchCheck(nullptr),
    otaInjectSilentTimeoutCheck(nullptr),
    otaInjectIgnoreFcCheck(nullptr),
    otaInjectIsoTpSnCheck(nullptr),
    otaInjectOutOfOrderCheck(nullptr),
    m_otaStressRunning(false),
    m_otaCurrentCycle(0),
    m_otaTargetCycles(0),
    m_otaSuccessCount(0),
    m_otaFailureCount(0),
    m_originalLogEnabled(true),
    m_otaCooldownTimer(new QTimer(this)),
    protocolModeCombo(nullptr),
    rfidStackedWidget(nullptr),
    mtRfidPanel(nullptr),
    qjRfidPanel(nullptr),
    qingjuCanManager(nullptr),
    qingjuRfidService(nullptr),
    qingjuOtaService(nullptr),
    qjRfidPeriodSpin(nullptr),
    qjStartBtn(nullptr),
    qjStopBtn(nullptr),
    qjAutoWritePwdCheckBox(nullptr),
    qjTargetDeviceCombo(nullptr),
    qjQueryModeCombo(nullptr),
    qjHostPollPeriodSpin(nullptr),
    qjDistanceQueryOnceBtn(nullptr),
    qjRfidAssetGroup(nullptr),
    qjRfidStatusGroup(nullptr),
    qjRfidAddrValue(nullptr),
    qjRfidResultValue(nullptr),
    qjRfidAppStatusValue(nullptr),
    qjRfidAlarmValue(nullptr),
    qjRfidUidValue(nullptr),
    qjRfidPwdValue(nullptr),
    qjRfidModelValue(nullptr),
    qjRfidSupplierValue(nullptr),
    qjRfidSerialValue(nullptr),
    qjRfidVendorValue(nullptr),
    qjRfidModelCodeValue(nullptr),
    qjRfidFwStrValue(nullptr),
    qjRfidHwStrValue(nullptr),
    qjRfidSnValue(nullptr),
    qjRfidFirmwareVerValue(nullptr),
    qjRfidHardwareVerValue(nullptr),
    qjRegisterPresetCombo(nullptr),
    qjDestAddrCombo(nullptr),
    qjRegAddrEdit(nullptr),
    qjRegValueEdit(nullptr),
    qjFuncCodeCombo(nullptr),
    qjCustomWriteBtn(nullptr),
    qjCustomReadBtn(nullptr),
    qjCustomLog(nullptr),
    qjCustomRequestPending(false),
    qjCustomExpectedSrc(0),
    qjCustomExpectedFunc(0),
    qjOtaAnomalyGroup(nullptr),
    qjOtaAnomalyCombo(nullptr),
    qjOtaAnomalyEnableCheck(nullptr)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/images/MT_RFID.png"));
    logFlushTimer->setSingleShot(true);
    logFlushTimer->setInterval(LogFlushIntervalMs);
    connect(logFlushTimer, &QTimer::timeout, this, &MainWindow::flushPendingLogRows);
    productionScanStableTimer->setSingleShot(true);
    productionScanStableTimer->setInterval(200);
    connect(productionScanStableTimer, &QTimer::timeout, this, [this]() {
        processProductionScanText(productionScanBuffer, false);
        productionScanBuffer.clear();
    });
    qApp->installEventFilter(this);

    // Instantiate background services first to avoid nullpointer dereferences during UI setup/loading config
    canthread = new CANThread();
    qingjuCanManager = new QingjuCanManager(canthread, this);
    qingjuRfidService = new QingjuRfidService(qingjuCanManager, this);
    qingjuOtaService = new QingjuOtaService(qingjuCanManager, this);
    qRegisterMetaType<Rs485State>("Rs485State");
    qRegisterMetaType<HlOtaService::State>("HlOtaService::State");
    qRegisterMetaType<BbFfOtaService::State>("BbFfOtaService::State");
    rs485Thread = new QThread(this);
    rs485Worker = new Rs485Worker();
    rs485Worker->moveToThread(rs485Thread);
    serialOpened = false;
    rs485Scanning = false;
    rs485StartAfterOpen = false;
    rs485PendingIntervalMs = 500;
    rs485PendingReadMode = 1;
    hlOtaState = HlOtaService::State::Idle;
    bbFfOtaState = BbFfOtaService::State::Idle;
    devicePanel = nullptr;
    serialDevicePanel = nullptr;

    connect(rs485Thread, &QThread::started, rs485Worker, &Rs485Worker::initialize);
    connect(this, &MainWindow::requestRs485OpenPort, rs485Worker, &Rs485Worker::openPort);
    connect(this, &MainWindow::requestRs485ClosePort, rs485Worker, &Rs485Worker::closePort);
    connect(this, &MainWindow::requestRs485SetProtocolMode, rs485Worker, &Rs485Worker::setProtocolMode);
    connect(this, &MainWindow::requestRs485StartScan, rs485Worker, &Rs485Worker::startScan);
    connect(this, &MainWindow::requestRs485StopScan, rs485Worker, &Rs485Worker::stopScan);
    connect(this, &MainWindow::requestRs485QueryDeviceInfo, rs485Worker, &Rs485Worker::queryDeviceInfo);
    connect(this, &MainWindow::requestRs485TriggerSingleQuery, rs485Worker, &Rs485Worker::triggerSingleQuery);
    connect(this, &MainWindow::requestRs485SetPower, rs485Worker, &Rs485Worker::setPower);
    connect(this, &MainWindow::requestRs485QueryPower, rs485Worker, &Rs485Worker::queryPower);
    connect(this, &MainWindow::requestRs485FfReboot, rs485Worker, &Rs485Worker::ffReboot);
    connect(this, &MainWindow::requestRs485FfSetDemodulatorParams, rs485Worker, &Rs485Worker::ffSetDemodulatorParams);
    connect(this, &MainWindow::requestRs485FfQueryDemodulatorParams, rs485Worker, &Rs485Worker::ffQueryDemodulatorParams);
    connect(this, &MainWindow::requestRs485FfQueryCardSwitch, rs485Worker, &Rs485Worker::ffQueryCardSwitch);
    connect(this, &MainWindow::requestRs485SetHlConfig, rs485Worker, &Rs485Worker::setHlConfig);
    connect(this, &MainWindow::requestRs485HlWriteScanControl, rs485Worker, &Rs485Worker::hlWriteScanControl);
    connect(this, &MainWindow::requestRs485HlRebootDevice, rs485Worker, &Rs485Worker::hlRebootDevice);
    connect(this, &MainWindow::requestRs485SendRawData, rs485Worker, &Rs485Worker::sendRawData);
    connect(this, &MainWindow::requestHlOtaQueryProgramStatus, rs485Worker, &Rs485Worker::hlOtaQueryProgramStatus);
    connect(this, &MainWindow::requestHlOtaStartUpgrade, rs485Worker, &Rs485Worker::hlOtaStartUpgrade);
    connect(this, &MainWindow::requestHlOtaAbortUpgrade, rs485Worker, &Rs485Worker::hlOtaAbortUpgrade);
    connect(this, &MainWindow::requestBbFfOtaStartUpgrade, rs485Worker, &Rs485Worker::bbFfOtaStartUpgrade);
    connect(this, &MainWindow::requestBbFfOtaAbortUpgrade, rs485Worker, &Rs485Worker::bbFfOtaAbortUpgrade);
    connect(rs485Worker, &Rs485Worker::portOpened, this, &MainWindow::onRs485PortOpened);
    connect(rs485Worker, &Rs485Worker::portClosed, this, &MainWindow::onRs485PortClosed);
    connect(rs485Worker, &Rs485Worker::scanStateChanged, this, &MainWindow::onRs485ScanStateChanged);
    connect(rs485Worker, &Rs485Worker::rawDataSent, this, [this](bool success, const QString &) {
        if (!success) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口发送失败，请检查串口状态！"));
        }
    });
    connect(rs485Worker, &Rs485Worker::frameReceived, this, [this](const QByteArray &data, const QString &decodeText) {
        addSerialFrameToList(false, data, decodeText);
    });
    connect(rs485Worker, &Rs485Worker::frameSent, this, [this](const QByteArray &data, const QString &decodeText) {
        addSerialFrameToList(true, data, decodeText);
    });
    connect(rs485Worker, &Rs485Worker::portDisconnected, this, &MainWindow::handleRs485Disconnect);
    connect(rs485Worker, &Rs485Worker::stateUpdated, this, &MainWindow::updateRs485RfidPanel);
    connect(rs485Worker, &Rs485Worker::pollSkipped, this, [this]() {
        if (stressTestService.stats().running && protocolModeCombo != nullptr && protocolModeCombo->currentIndex() >= 2) {
            stressTestService.recordRs485PollSkipped();
            updateStressTestPanel(stressTestService.stats());
        }
    });
    connect(rs485Worker, &Rs485Worker::commandFinished, this, &MainWindow::onRs485CommandFinished);
    connect(rs485Worker, &Rs485Worker::hlOtaStateChanged, this, &MainWindow::handleHlOtaStateChanged);
    connect(rs485Worker, &Rs485Worker::hlOtaProgress, this, [this](int percentage) {
        if (appConfig.load().protocolMode == 4) {
            if (otaProgressBar != nullptr) {
                otaProgressBar->setValue(percentage);
            }
        }
    });

    connect(rs485Worker, &Rs485Worker::bbFfOtaStateChanged, this, &MainWindow::handleBbFfOtaStateChanged);
    connect(rs485Worker, &Rs485Worker::bbFfOtaProgress, this, [this](int percentage) {
        int mode = appConfig.load().protocolMode;
        if (mode == 2 || mode == 3) {
            if (otaProgressBar != nullptr) {
                otaProgressBar->setValue(percentage);
            }
        }
    });
    rs485Thread->start();

    ui->filterModeCombo->setCurrentIndex(2);
    ui->ABIT1Combo->setCurrentIndex(2);
    ui->protocolCombo->setCurrentIndex(0);
    ui->frameTypeCombo->setCurrentIndex(0);

    QStringList listHeader;
    listHeader << "时间" << "通道" << "收/发" << "ID" << "Frame" << "类型" << "DLC" << "CAN-FD" << "数据" << "协议解析";

    ui->tableWidget->setColumnCount(listHeader.count());
    ui->tableWidget->setHorizontalHeaderLabels(listHeader);
    ui->tableWidget->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("时间")
        << QStringLiteral("通道")
        << QStringLiteral("收/发")
        << QStringLiteral("ID")
        << QStringLiteral("Frame")
        << QStringLiteral("类型")
        << QStringLiteral("DLC")
        << QStringLiteral("CAN-FD")
        << QStringLiteral("数据")
        << QStringLiteral("协议解析"));
    ui->tableWidget->setColumnWidth(0,80);
    ui->tableWidget->setColumnWidth(1,40);
    ui->tableWidget->setColumnWidth(2,60);
    ui->tableWidget->setColumnWidth(3,80);
    ui->tableWidget->setColumnWidth(4,90);
    ui->tableWidget->setColumnWidth(5,90);
    ui->tableWidget->setColumnWidth(6,80);
    ui->tableWidget->setColumnWidth(7,90);
    ui->tableWidget->setColumnWidth(8,200);
    ui->tableWidget->setColumnWidth(9,260);

    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->horizontalHeader()->setStretchLastSection(true);
    setupCanLogSaveButton();
    canAutoSaveCheckBox = new QCheckBox(QStringLiteral("自动保存日志"), ui->cleanListBtn->parentWidget());
    canAutoSaveCheckBox->setText(QStringLiteral("自动保存日志"));
    connect(canAutoSaveCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            bool needPrompt = logDirectory.isEmpty() || !QDir(logDirectory).exists();
            if (!needPrompt) {
                QMessageBox::StandardButton reply = QMessageBox::question(
                    this,
                    QStringLiteral("提示"),
                    QStringLiteral("当前已设置日志保存目录为：\n%1\n\n是否需要修改保存目录？").arg(logDirectory),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No
                );
                if (reply == QMessageBox::Yes) {
                    needPrompt = true;
                }
            }

            if (needPrompt) {
                QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择日志自动保存目录"), logDirectory);
                if (dir.isEmpty()) {
                    canAutoSaveCheckBox->blockSignals(true);
                    canAutoSaveCheckBox->setChecked(false);
                    canAutoSaveCheckBox->blockSignals(false);
                    logService.setCanAutoSaveEnabled(false);
                    return;
                }
                logDirectory = dir;
                logService.setLogDirectory(dir);
                saveAppConfig();
            } else {
                logService.setLogDirectory(logDirectory);
            }
        }
        logService.setCanAutoSaveEnabled(checked);
    });

    ui->deviceTypeCombo->clear();
    for (const QString &deviceTypeName : DeviceTypeNames) {
         ui->deviceTypeCombo->addItem(deviceTypeName, "");
    }

    setupRfidPanel();
    setupStatusPanel();
    setupCompactMainLayout();
    loadAppConfig();

    // 注册自定义类型，确保跨线程 QMetaType 序列化
    qRegisterMetaType<CanFrame>("CanFrame");
    qRegisterMetaType<QVector<CanFrame>>("QVector<CanFrame>");

    // 2s 定时发送诊断设备接管报文 (Tester Present - 0x21F)
    testerPresentTimer->setInterval(2000);
    connect(testerPresentTimer, &QTimer::timeout, this, [this]() {
        if (canStarted) {
            QByteArray payload(8, 0x55);
            payload[0] = 0x3E;
            const UINT channel = static_cast<UINT>(ui->sendPathCombo->currentIndex());
            canthread->sendClassicData(0x21F, channel, payload);
        }
    });

    // 100ms 定时广播 RFID 控制帧 (0x207)
    rfidControlTimer->setInterval(100);
    connect(rfidControlTimer, &QTimer::timeout, this, [this]() {
        if (canStarted) {
            sendRfidFrame(RfidProtocol::ControlFrameId, RfidProtocol::buildControlFrame(rfidScanning));
        }
    });

    // RFID 在线状态监测定时器 (1s 间隔)
    rfidOnlineCheckTimer->setInterval(1000);
    connect(rfidOnlineCheckTimer, &QTimer::timeout, this, [this]() {
        if (!canStarted) {
            rfidOnlineStatusValue->setText(QStringLiteral("未启动"));
            rfidOnlineStatusValue->setStyleSheet("color: gray; font-weight: bold;");
            updateMeituanTopStatus();
            return;
        }
        const bool qingjuMode = protocolModeCombo != nullptr && protocolModeCombo->currentIndex() == 1;
        if (qingjuMode) {
            updateQingjuOnlineStatus(true);
            return;
        }
        if (lastRfidFrameTime.isValid() && lastRfidFrameTime.msecsTo(QDateTime::currentDateTime()) < 1500) {
            rfidOnlineStatusValue->setText(QStringLiteral("在线"));
            rfidOnlineStatusValue->setStyleSheet("color: green; font-weight: bold;");
            updateMeituanTopStatus();
        } else {
            rfidOnlineStatusValue->setText(QStringLiteral("离线"));
            rfidOnlineStatusValue->setStyleSheet("color: red; font-weight: bold;");
            rfidService.reset();

            // 设备离线时清空美团协议显示数据为 -
            rfidWorkModeValue->setText("-");
            rfidCardStatusValue->setText("-");
            rfidFaultStatusValue->setText("-");
            rfidScanPeriodValue->setText("-");
            rfidTagValue->setText("-");
            rfidTagPart1Value->setText("-");
            rfidTagPart2Value->setText("-");
            rfidTagPart3Value->setText("-");
            rfidDeviceIdValue->setText("-");
            rfidVersionValue->setText("-");
            rfidResponseValue->setText("-");
            updateMeituanTopStatus();
        }
    });

    connect(canthread, &CANThread::recvedFrames, this, &MainWindow::handleRecvedFrames);

    connect(qingjuRfidService, &QingjuRfidService::stateUpdated, this, &MainWindow::updateQingjuRfidPanel);
    connect(qingjuCanManager, &QingjuCanManager::modbusPacketReceived, qingjuOtaService, &QingjuOtaService::handleIncomingModbusPacket);
    connect(qingjuCanManager, &QingjuCanManager::modbusPacketReceived, this, &MainWindow::handleQjCustomResponse);

    connect(qingjuOtaService, &QingjuOtaService::otaStateChanged, this, [this](QingjuOtaService::State state, const QString &message) {
        if (appConfig.load().protocolMode == 1) {
            otaStateValue->setText(qingjuOtaService->stateText());
            otaMessageValue->setText(message);
            handleQingjuOtaStateChangeForStressTest(state, message);
            updateControlsState();
        }
    });
    connect(qingjuOtaService, &QingjuOtaService::otaProgress, this, [this](int percentage) {
        if (appConfig.load().protocolMode == 1) {
            if (otaProgressBar != nullptr) {
                otaProgressBar->setValue(percentage);
            }
        }
    });
    stressRefreshTimer->setInterval(1000);
    connect(stressRefreshTimer, &QTimer::timeout, this, &MainWindow::refreshStressTestTick);

    // 冷却定时器设置
    m_otaCooldownTimer->setSingleShot(true);
    connect(m_otaCooldownTimer, &QTimer::timeout, this, [this]() {
        if (!m_otaStressRunning || !canStarted) {
            m_otaStressRunning = false;
            updateOtaStressUI();
            updateControlsState();
            return;
        }

        m_otaCurrentCycle++;
        updateOtaStressUI();

        const QString firmwarePath = otaFirmwarePathValue == nullptr ? QString() : otaFirmwarePathValue->text();
        if (appConfig.load().protocolMode == 1) {
            qingjuOtaService->startUpgrade(firmwarePath == "-" ? QString() : firmwarePath,
                                           selectedQingjuOtaTarget(),
                                           getQingjuOtaErrorConfig());
        } else {
            otaService.startUpgrade(firmwarePath == "-" ? QString() : firmwarePath, getOtaErrorConfig());
        }
    });

    // 连接 OTA 服务信号
    connect(&otaService, &OtaService::otaStateChanged, this, [this](OtaService::State state, const QString &message) {
        otaStateValue->setText(otaService.stateText());
        otaMessageValue->setText(message);
        handleOtaStateChangeForStressTest(state, message);
        updateControlsState();
    });
    connect(&otaService, &OtaService::otaProgress, this, [this](int percentage) {
        if (otaProgressBar != nullptr) {
            otaProgressBar->setValue(percentage);
        }
    });
    connect(&otaService, &OtaService::transmitFrame, this, [this](quint32 id, const QByteArray &payload) {
        sendRfidFrame(id, payload);
    });

    connect(&rfidDiagnosticTransfer, &RfidDiagnosticTransfer::frameReady, this, [this](quint32 id, const QByteArray &payload) {
        sendRfidFrame(static_cast<UINT>(id), payload);
    });
    connect(&rfidDiagnosticTransfer, &RfidDiagnosticTransfer::logMessage, this, [this](const QString &message) {
        logService.logRuntime(LogLevel::Info, message);
    });
    connect(&rfidDiagnosticTransfer, &RfidDiagnosticTransfer::finished, this, [this](bool success, const QString &message) {
        logService.logRuntime(success ? LogLevel::Info : LogLevel::Warning, message);
        if (productionWritePending) {
            productionWritePending = false;
            productionTestService.handleWriteFinished(success, message);
            return;
        }
        if (success) {
            QMessageBox::information(this, QStringLiteral("写入完成"), message);
        } else {
            QMessageBox::warning(this, QStringLiteral("写入失败"), message);
        }
    });

    connect(&productionTestService, &ProductionTestService::writeSnRequested, this, [this](quint16 did, const QByteArray &data) {
        productionWritePending = true;
        if (!rfidDiagnosticTransfer.startWriteNonVolatile(did, data)) {
            productionWritePending = false;
            productionTestService.handleWriteFinished(false, QStringLiteral("0x2E写入通道忙或请求无效"));
        }
    });
    connect(&productionTestService, &ProductionTestService::scanControlRequested, this, &MainWindow::sendProductionScanControl);
    connect(&productionTestService, &ProductionTestService::stateChanged, this, &MainWindow::updateProductionTestPanel);
    connect(&productionTestService, &ProductionTestService::logMessage, this, [this](const QString &message) {
        appendProductionTestLog(message);
        logService.logRuntime(LogLevel::Info, message);
    });
    connect(&productionTestService, &ProductionTestService::finished, this, [this](bool, const ProductionTestState &) {
        prepareProductionSnInput();
        updateControlsState();
    });
}

void MainWindow::setupRfidPanel()
{
    rfidTabs = new QTabWidget(ui->centralWidget);
    
    rfidStackedWidget = new QStackedWidget(rfidTabs);
    mtRfidPanel = createRfidMonitorTab(rfidStackedWidget);
    qjRfidPanel = createQjRfidMonitorPanel(rfidStackedWidget);
    bbRfidPanel = createBbRfidMonitorPanel(rfidStackedWidget);
    ffRfidPanel = createFfRfidMonitorPanel(rfidStackedWidget);
    hlRfidPanel = createHlRfidMonitorPanel(rfidStackedWidget);
    
    rfidStackedWidget->addWidget(mtRfidPanel);
    rfidStackedWidget->addWidget(qjRfidPanel);
    rfidStackedWidget->addWidget(bbRfidPanel);
    rfidStackedWidget->addWidget(ffRfidPanel);
    rfidStackedWidget->addWidget(hlRfidPanel);
    
    rfidTabs->addTab(rfidStackedWidget, QStringLiteral("RFID监控"));
    
    QWidget *t2 = createStressTestTab(rfidTabs);
    rfidTabs->addTab(t2, QStringLiteral("压力测试"));
    
    QWidget *t3 = createOtaTab(rfidTabs);
    rfidTabs->addTab(t3, QStringLiteral("OTA升级"));

    productionTestTab = createProductionTestTab(rfidTabs);
    testExecutionTab = createTestExecutionTab(rfidTabs);
    rfidTabs->addTab(productionTestTab, QStringLiteral("产线检测"));

    rfidTabs->addTab(testExecutionTab, QStringLiteral("测试执行"));

    connect(rfidTabs, &QTabWidget::currentChanged, this, [this](int) {
        if (rfidTabs != nullptr && productionTestTab != nullptr &&
            rfidTabs->currentWidget() == productionTestTab) {
            prepareProductionSnInput();
        }
        if (rfidTabs != nullptr && testExecutionTab != nullptr &&
            rfidTabs->currentWidget() == testExecutionTab &&
            autoCompactLogOnTestExecutionCheck != nullptr &&
            autoCompactLogOnTestExecutionCheck->isChecked()) {
            applyCanLogCompact(true);
        }
        updateTestExecutionControls();
    });
}

void MainWindow::setupStatusPanel()
{
    statusGroup = new QGroupBox(QStringLiteral("运行状态"), ui->centralWidget);

    QGridLayout *layout = new QGridLayout(statusGroup);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setHorizontalSpacing(12);

    topCanStatusValue = new QLabel(QStringLiteral("CAN：未启动"), statusGroup);
    topRfidStatusValue = new QLabel(QStringLiteral("RFID：未识别"), statusGroup);
    topStressStatusValue = new QLabel(QStringLiteral("压测：已停止"), statusGroup);
    topCanStatusValue->setMinimumHeight(22);
    topRfidStatusValue->setMinimumHeight(22);
    topStressStatusValue->setMinimumHeight(22);
    topRfidStatusValue->setMinimumWidth(520);

    layout->addWidget(topCanStatusValue, 0, 0);
    layout->addWidget(topRfidStatusValue, 0, 1);
    layout->addWidget(topStressStatusValue, 0, 2);
    layout->setColumnStretch(1, 1);
}

void MainWindow::setupCompactMainLayout()
{
    QRect availableGeometry(0, 0, 1280, 820);
    if (QApplication::primaryScreen() != nullptr) {
        availableGeometry = QApplication::primaryScreen()->availableGeometry();
    }
    const int availableWidth = qMax(900, availableGeometry.width() - 40);
    const int availableHeight = qMax(620, availableGeometry.height() - 60);
    const int minimumWidth = qMin(1180, availableWidth);
    const int minimumHeight = qMin(760, availableHeight);
    const int targetWidth = qMin(1440, qMax(minimumWidth, availableWidth));
    const int targetHeight = qMin(900, qMax(minimumHeight, availableHeight));
    setMinimumSize(minimumWidth, minimumHeight);
    resize(targetWidth, targetHeight);

    const QList<QWidget *> centralChildren = ui->centralWidget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : centralChildren) {
        if (child->objectName().startsWith(QLatin1String("layoutWidget"))) {
            child->hide();
        }
    }

    ui->groupBox->setTitle(QStringLiteral("设备控制"));
    ui->groupBox->setMinimumWidth(220);
    ui->groupBox->setMaximumWidth(320);
    ui->groupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    const QList<QWidget *> oldConfigChildren = ui->groupBox->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : oldConfigChildren) {
        child->hide();
    }

    // 协议模式选择区 (HeaderWidget)
    QWidget *configHeaderWidget = new QWidget(ui->groupBox);
    QGridLayout *headerLayout = new QGridLayout(configHeaderWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setHorizontalSpacing(6);
    headerLayout->setVerticalSpacing(6);

    protocolModeCombo = new QComboBox(configHeaderWidget);
    protocolModeCombo->addItem(QStringLiteral("美团协议"), 0);
    protocolModeCombo->addItem(QStringLiteral("青桔协议"), 1);
    protocolModeCombo->addItem(QStringLiteral("BB协议"), 2);
    protocolModeCombo->addItem(QStringLiteral("FF协议"), 3);
    protocolModeCombo->addItem(QStringLiteral("哈啰协议"), 4);
    protocolModeCombo->setMinimumWidth(160);
    connect(protocolModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onProtocolModeChanged);

    headerLayout->addWidget(new QLabel(QStringLiteral("协议模式"), configHeaderWidget), 0, 0);
    headerLayout->addWidget(protocolModeCombo, 0, 1);

    // CAN 设备配置面板
    devicePanel = new QWidget(ui->groupBox);
    QGridLayout *layout = new QGridLayout(devicePanel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(6);

    oneClickStartButton = new QPushButton(QStringLiteral("一键启动"), devicePanel);
    canDeviceStatusValue = new QLabel(QStringLiteral("未启动"), devicePanel);
    canDeviceStatusValue->setAlignment(Qt::AlignCenter);

    ui->deviceTypeCombo->setParent(devicePanel);
    ui->deviceIndexCombo->setParent(devicePanel);
    ui->sendPathCombo->setParent(devicePanel);
    ui->ABIT1Combo->setParent(devicePanel);
    ui->resistanceCheckBox->setParent(devicePanel);
    ui->openDeviceBtn->setParent(devicePanel);
    ui->initCANBtn->setParent(devicePanel);
    ui->StartCANBtn->setParent(devicePanel);
    ui->reSetCANBtn->setParent(devicePanel);
    ui->closeDeviceBtn->setParent(devicePanel);

    // 拓宽设备类型选择框，设置最小宽度并使其横向跨越3列
    ui->deviceTypeCombo->setMinimumWidth(160);
    ui->ABIT1Combo->setMinimumWidth(160);
    ui->resistanceCheckBox->setText(QStringLiteral("终端电阻使能"));
    ui->openDeviceBtn->setText(QStringLiteral("打开设备"));
    ui->initCANBtn->setText(QStringLiteral("初始化CAN"));
    ui->StartCANBtn->setText(QStringLiteral("启动CAN"));
    ui->reSetCANBtn->setText(QStringLiteral("复位"));
    ui->closeDeviceBtn->setText(QStringLiteral("关闭设备"));

    layout->addWidget(new QLabel(QStringLiteral("设备类型"), devicePanel), 0, 0);
    layout->addWidget(ui->deviceTypeCombo, 0, 1, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("设备索引"), devicePanel), 1, 0);
    layout->addWidget(ui->deviceIndexCombo, 1, 1);
    layout->addWidget(new QLabel(QStringLiteral("通道"), devicePanel), 1, 2);
    layout->addWidget(ui->sendPathCombo, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("波特率"), devicePanel), 2, 0);
    layout->addWidget(ui->ABIT1Combo, 2, 1, 1, 3);
    layout->addWidget(ui->resistanceCheckBox, 3, 0, 1, 4);
    layout->addWidget(oneClickStartButton, 4, 0, 1, 4);
    layout->addWidget(canDeviceStatusValue, 5, 0, 1, 4);
    layout->addWidget(ui->openDeviceBtn, 6, 0, 1, 2);
    layout->addWidget(ui->initCANBtn, 6, 2, 1, 2);
    layout->addWidget(ui->StartCANBtn, 7, 0, 1, 2);
    layout->addWidget(ui->reSetCANBtn, 7, 2);
    layout->addWidget(ui->closeDeviceBtn, 7, 3);

    // RS485 设备配置面板
    serialDevicePanel = new QWidget(ui->groupBox);
    QGridLayout *serialLayout = new QGridLayout(serialDevicePanel);
    serialLayout->setContentsMargins(0, 0, 0, 0);
    serialLayout->setHorizontalSpacing(6);
    serialLayout->setVerticalSpacing(6);

    serialPortCombo = new QComboBox(serialDevicePanel);
    serialBaudRateCombo = new QComboBox(serialDevicePanel);
    serialBaudRateCombo->addItem("9600", 9600);
    serialBaudRateCombo->addItem("19200", 19200);
    serialBaudRateCombo->addItem("38400", 38400);
    serialBaudRateCombo->addItem("57600", 57600);
    serialBaudRateCombo->addItem("115200", 115200);
    serialBaudRateCombo->setToolTip(QStringLiteral("协议示例主要使用 9600/115200；选择其他波特率前请确认设备支持。"));
    
    serialOpenCloseBtn = new QPushButton(QStringLiteral("打开串口"), serialDevicePanel);
    serialRefreshBtn = new QPushButton(QStringLiteral("刷新串口"), serialDevicePanel);
    serialOneClickStartBtn = new QPushButton(QStringLiteral("一键启动"), serialDevicePanel);
    serialStatusLabel = new QLabel(QStringLiteral("串口关闭"), serialDevicePanel);
    serialStatusLabel->setAlignment(Qt::AlignCenter);

    serialLayout->addWidget(new QLabel(QStringLiteral("串口选择"), serialDevicePanel), 0, 0);
    serialLayout->addWidget(serialPortCombo, 0, 1, 1, 2);
    serialLayout->addWidget(serialRefreshBtn, 0, 3);
    
    serialLayout->addWidget(new QLabel(QStringLiteral("波特率"), serialDevicePanel), 1, 0);
    serialLayout->addWidget(serialBaudRateCombo, 1, 1, 1, 3);
    
    serialLayout->addWidget(serialOneClickStartBtn, 2, 0, 1, 4);
    serialLayout->addWidget(serialStatusLabel, 3, 0, 1, 4);
    serialLayout->addWidget(serialOpenCloseBtn, 4, 0, 1, 4);

    connect(serialOpenCloseBtn, &QPushButton::clicked, this, &MainWindow::onSerialOpenCloseClicked);
    connect(serialRefreshBtn, &QPushButton::clicked, this, &MainWindow::onSerialRefreshClicked);
    connect(serialOneClickStartBtn, &QPushButton::clicked, this, &MainWindow::onSerialOneClickStartClicked);

    QVBoxLayout *groupLayout1 = new QVBoxLayout(ui->groupBox);
    groupLayout1->setContentsMargins(10, 22, 10, 10);
    groupLayout1->addWidget(configHeaderWidget);
    groupLayout1->addWidget(devicePanel);
    groupLayout1->addWidget(serialDevicePanel);

    ui->groupBox_2->setTitle(QStringLiteral("手动发送"));
    ui->groupBox_2->setMinimumWidth(220);
    ui->groupBox_2->setMaximumWidth(320);
    ui->groupBox_2->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    const QList<QWidget *> oldSendChildren = ui->groupBox_2->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : oldSendChildren) {
        child->hide();
    }

    QWidget *sendPanel = new QWidget(ui->groupBox_2);
    QGridLayout *sendLayout = new QGridLayout(sendPanel);
    sendLayout->setContentsMargins(0, 0, 0, 0);
    sendLayout->setHorizontalSpacing(6);
    sendLayout->setVerticalSpacing(6);

    ui->sendIDEdit->setParent(sendPanel);
    ui->frameTypeCombo->setParent(sendPanel);
    ui->protocolCombo->setParent(sendPanel);
    ui->CANFDaccCheck->setParent(sendPanel);
    ui->sendDataEdit->setParent(sendPanel);
    ui->sendBtn->setParent(sendPanel);
    manualSendIdLabel = new QLabel(QStringLiteral("ID"), sendPanel);
    manualSendDataLabel = new QLabel(QStringLiteral("数据"), sendPanel);
    manualSendHintLabel = new QLabel(QStringLiteral("CAN手动发送：输入 CAN ID 与数据字节"), sendPanel);
    manualSendHintLabel->setStyleSheet(QStringLiteral("color: gray;"));
    ui->sendBtn->setText(QStringLiteral("发送"));
    ui->CANFDaccCheck->setText(QStringLiteral("CANFD加速"));

    sendLayout->addWidget(manualSendIdLabel, 0, 0);
    sendLayout->addWidget(ui->sendIDEdit, 0, 1, 1, 2);
    sendLayout->addWidget(ui->sendBtn, 0, 3);
    sendLayout->addWidget(manualSendDataLabel, 1, 0);
    sendLayout->addWidget(ui->sendDataEdit, 1, 1, 1, 3);
    sendLayout->addWidget(ui->frameTypeCombo, 2, 0, 1, 2);
    sendLayout->addWidget(ui->protocolCombo, 2, 2);
    sendLayout->addWidget(ui->CANFDaccCheck, 2, 3);
    sendLayout->addWidget(manualSendHintLabel, 3, 0, 1, 4);

    QVBoxLayout *groupLayout2 = new QVBoxLayout(ui->groupBox_2);
    groupLayout2->setContentsMargins(10, 20, 10, 10);
    groupLayout2->addWidget(sendPanel);

    ui->groupBox_3->setTitle(QStringLiteral("实时CAN日志"));
    ui->groupBox_3->setMinimumHeight(240);
    ui->groupBox_3->setMaximumHeight(380);
    ui->groupBox_3->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    QHBoxLayout *logLayout = new QHBoxLayout(ui->groupBox_3);
    logLayout->setContentsMargins(10, 20, 10, 10);
    logLayout->setSpacing(10);
    logLayout->addWidget(ui->tableWidget);

    QGridLayout *logButtonsLayout = new QGridLayout();
    logButtonsLayout->setContentsMargins(0, 0, 0, 0);
    logButtonsLayout->setHorizontalSpacing(6);
    logButtonsLayout->setVerticalSpacing(6);
    layoutPresetCombo = new QComboBox(ui->groupBox_3);
    layoutPresetCombo->addItems(QStringList()
        << QStringLiteral("紧凑布局")
        << QStringLiteral("标准布局")
        << QStringLiteral("大屏布局")
        << QStringLiteral("自定义布局"));
    layoutPresetCombo->setMinimumWidth(120);
    logButtonsLayout->addWidget(layoutPresetCombo, 0, 0, 1, 2);
    connect(layoutPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        applyLayoutPreset(index);
        saveAppConfig();
    });
    compactCanLogButton = new QPushButton(QStringLiteral("收起日志"), ui->groupBox_3);
    logButtonsLayout->addWidget(compactCanLogButton, 1, 0, 1, 2);
    connect(compactCanLogButton, &QPushButton::clicked, this, [this]() {
        applyCanLogCompact(!canLogCompact);
        saveAppConfig();
    });
    if (canAutoSaveCheckBox != nullptr) {
        logButtonsLayout->addWidget(canAutoSaveCheckBox, 2, 0, 1, 2);
    }
    logButtonsLayout->addWidget(ui->checkBox_4, 3, 0);
    show0x207LogCheck = new QCheckBox(QStringLiteral("显示0x207"), ui->groupBox_3);
    show0x207LogCheck->setText(QStringLiteral("显示0x207"));
    logButtonsLayout->addWidget(show0x207LogCheck, 3, 1);
    connect(show0x207LogCheck, &QCheckBox::stateChanged, this, [this](int) {
        saveAppConfig();
    });
    logButtonsLayout->addWidget(ui->cleanListBtn, 4, 0);
    if (saveCanLogButton != nullptr) {
        logButtonsLayout->addWidget(saveCanLogButton, 4, 1);
    }
    popCanLogButton = new QPushButton(QStringLiteral("弹出日志窗口"), ui->groupBox_3);
    logButtonsLayout->addWidget(popCanLogButton, 5, 0, 1, 2);
    connect(popCanLogButton, &QPushButton::clicked, this, &MainWindow::openCanLogWindow);
    logLayout->addLayout(logButtonsLayout);

    // 左侧面板垂直容器布局
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    leftLayout->addWidget(ui->groupBox);
    leftLayout->addWidget(ui->groupBox_2);
    leftLayout->addStretch();

    QWidget *leftPanel = new QWidget(ui->centralWidget);
    leftPanel->setLayout(leftLayout);
    leftPanel->setMinimumWidth(230);

    QScrollArea *leftScrollArea = new QScrollArea(ui->centralWidget);
    leftScrollArea->setWidgetResizable(true);
    leftScrollArea->setFrameShape(QFrame::NoFrame);
    leftScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScrollArea->setWidget(leftPanel);
    leftScrollArea->setMinimumWidth(240);
    leftScrollArea->setMaximumWidth(340);

    QWidget *topContentWidget = new QWidget(ui->centralWidget);
    QGridLayout *topLayout = new QGridLayout(topContentWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setHorizontalSpacing(10);
    topLayout->setVerticalSpacing(0);
    topLayout->addWidget(leftScrollArea, 0, 0);

    // 建立整个主界面的动态 Grid 布局
    QVBoxLayout *mainLayout = new QVBoxLayout(ui->centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    if (statusGroup != nullptr) {
        mainLayout->addWidget(statusGroup);
    }
    if (rfidTabs != nullptr) {
        rfidTabs->setMinimumHeight(420);
        topLayout->addWidget(rfidTabs, 0, 1);
    }
    topLayout->setColumnStretch(0, 0);
    topLayout->setColumnStretch(1, 1);

    mainVerticalSplitter = new QSplitter(Qt::Vertical, ui->centralWidget);
    mainVerticalSplitter->setChildrenCollapsible(false);
    mainVerticalSplitter->addWidget(topContentWidget);
    mainVerticalSplitter->addWidget(ui->groupBox_3);
    mainVerticalSplitter->setStretchFactor(0, 3);
    mainVerticalSplitter->setStretchFactor(1, 1);
    connect(mainVerticalSplitter, &QSplitter::splitterMoved, this, [this]() {
        if (layoutPresetCombo != nullptr && layoutPresetCombo->currentIndex() != 3) {
            layoutPresetCombo->blockSignals(true);
            layoutPresetCombo->setCurrentIndex(3);
            layoutPresetCombo->blockSignals(false);
        }
        if (!canLogCompact) {
            saveAppConfig();
        }
    });
    mainLayout->addWidget(mainVerticalSplitter, 1);

    connect(oneClickStartButton, &QPushButton::clicked, this, &MainWindow::oneClickStartCan);
    updateCanControlState(false, false, false);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)

    QWidget *focusWidget = QApplication::focusWidget();
    if (event->type() != QEvent::KeyPress ||
        productionTestTab == nullptr ||
        rfidTabs == nullptr ||
        rfidTabs->currentWidget() != productionTestTab ||
        productionTestService.isRunning() ||
        protocolModeCombo == nullptr ||
        protocolModeCombo->currentIndex() != 0 ||
        focusWidget == productionSnEdit) {
        return QMainWindow::eventFilter(watched, event);
    }

    QComboBox *focusComboBox = qobject_cast<QComboBox *>(focusWidget);
    if (qobject_cast<QLineEdit *>(focusWidget) != nullptr ||
        qobject_cast<QAbstractSpinBox *>(focusWidget) != nullptr ||
        qobject_cast<QTextEdit *>(focusWidget) != nullptr ||
        qobject_cast<QPlainTextEdit *>(focusWidget) != nullptr ||
        (focusComboBox != nullptr && focusComboBox->isEditable())) {
        return QMainWindow::eventFilter(watched, event);
    }

    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->isAutoRepeat()) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
        if (!productionScanBuffer.isEmpty()) {
            const QString scanText = productionScanBuffer;
            productionScanBuffer.clear();
            productionScanStableTimer->stop();
            processProductionScanText(scanText, true);
            return true;
        }
        return QMainWindow::eventFilter(watched, event);
    }

    const QString text = keyEvent->text();
    if (text.size() != 1 || !text.at(0).isLetterOrNumber()) {
        return QMainWindow::eventFilter(watched, event);
    }

    const QChar ch = text.at(0).toUpper();
    const QDateTime now = QDateTime::currentDateTime();
    if (!productionLastScanKeyTime.isValid() ||
        productionLastScanKeyTime.msecsTo(now) > 60 ||
        productionScanBuffer.size() >= 32) {
        productionScanBuffer.clear();
    }
    productionLastScanKeyTime = now;

    if (productionScanBuffer.isEmpty() && ch != QLatin1Char('R')) {
        return QMainWindow::eventFilter(watched, event);
    }

    productionScanBuffer.append(ch);
    productionScanStableTimer->start(200);
    return true;
}

void MainWindow::updateCanControlState(bool deviceOpened, bool canInitialized, bool canStarted)
{
    this->deviceOpened = deviceOpened;
    this->canInitialized = canInitialized;
    this->canStarted = canStarted;
    if (canStarted) {
        testerPresentTimer->start();
        if (rfidControlEnabledCheck != nullptr && rfidControlEnabledCheck->isChecked()) {
            rfidControlTimer->start();
        }
        rfidOnlineCheckTimer->start();
        lastRfidFrameTime = QDateTime();
        lastQingjuNpkFrameTime = QDateTime();
        lastQingjuRfrFrameTime = QDateTime();
    } else {
        if (productionTestService.isRunning()) {
            productionTestService.stop(QStringLiteral("CAN未启动"));
        }
        abortRfidDiagnosticTransferSilently();
        productionWritePending = false;
        testerPresentTimer->stop();
        rfidControlTimer->stop();
        rfidOnlineCheckTimer->stop();
        rfidScanning = false;
        lastRfidFrameTime = QDateTime();
        lastQingjuNpkFrameTime = QDateTime();
        lastQingjuRfrFrameTime = QDateTime();
        if (rfidOnlineStatusValue != nullptr) {
            rfidOnlineStatusValue->setText(QStringLiteral("未启动"));
            rfidOnlineStatusValue->setStyleSheet("color: gray; font-weight: bold;");
        }
    }
    ui->openDeviceBtn->setEnabled(!deviceOpened);
    ui->initCANBtn->setEnabled(deviceOpened && !canInitialized);
    ui->StartCANBtn->setEnabled(canInitialized && !canStarted);
    ui->reSetCANBtn->setEnabled(canStarted);
    ui->closeDeviceBtn->setEnabled(deviceOpened);
    ui->sendBtn->setEnabled(canStarted);
    if (oneClickStartButton != nullptr) {
        oneClickStartButton->setEnabled(!canStarted);
    }
    if (canDeviceStatusValue != nullptr) {
        if (canStarted) {
            canDeviceStatusValue->setText(QStringLiteral("状态：CAN已启动"));
        } else if (canInitialized) {
            canDeviceStatusValue->setText(QStringLiteral("状态：已初始化"));
        } else if (deviceOpened) {
            canDeviceStatusValue->setText(QStringLiteral("状态：设备已打开"));
        } else {
            canDeviceStatusValue->setText(QStringLiteral("状态：未启动"));
        }
    }
    if (topCanStatusValue != nullptr) {
        if (canStarted) {
            topCanStatusValue->setText(QStringLiteral("CAN：已启动"));
            topCanStatusValue->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        } else if (canInitialized) {
            topCanStatusValue->setText(QStringLiteral("CAN：已初始化"));
            topCanStatusValue->setStyleSheet(QStringLiteral("color: #B26A00; font-weight: bold;"));
        } else if (deviceOpened) {
            topCanStatusValue->setText(QStringLiteral("CAN：设备已打开"));
            topCanStatusValue->setStyleSheet(QStringLiteral("color: #B26A00; font-weight: bold;"));
        } else {
            topCanStatusValue->setText(QStringLiteral("CAN：未启动"));
            topCanStatusValue->setStyleSheet(QStringLiteral("color: gray; font-weight: bold;"));
        }
    }
    if (topRfidStatusValue != nullptr) {
        const bool qingjuMode = protocolModeCombo != nullptr && protocolModeCombo->currentIndex() == 1;
        if (qingjuMode) {
            if (canStarted) {
                updateQingjuOnlineStatus(false);
            } else {
                topRfidStatusValue->setText(QStringLiteral("协议：青桔  NPK：未启动  RFR：未启动"));
                topRfidStatusValue->setStyleSheet(QStringLiteral("color: gray; font-weight: bold;"));
            }
        } else if (!canStarted) {
            updateMeituanTopStatus();
        } else {
            updateMeituanTopStatus();
        }
    }
    if (!canStarted && stressTestService.stats().running) {
        stopStressTest(false);
        if (stressRemainingLabel != nullptr) {
            stressRemainingLabel->setText(QStringLiteral("测试中止：CAN未启动"));
        }
        logService.logRuntime(LogLevel::Warning, QStringLiteral("Stress test stopped because CAN is not started"));
    }
    updateManualSendPanelMode();
    updateControlsState();
}

void MainWindow::updateManualSendPanelMode()
{
    const int protocolMode = protocolModeCombo != nullptr ? protocolModeCombo->currentIndex() : 0;
    const bool is485Mode = (protocolMode == 2 || protocolMode == 3 || protocolMode == 4);

    ui->groupBox_2->setTitle(is485Mode ? QStringLiteral("串口手动发送") : QStringLiteral("手动发送"));
    if (manualSendIdLabel != nullptr) manualSendIdLabel->setVisible(!is485Mode);
    ui->sendIDEdit->setVisible(!is485Mode);
    ui->frameTypeCombo->setVisible(!is485Mode);
    ui->protocolCombo->setVisible(!is485Mode);
    ui->CANFDaccCheck->setVisible(!is485Mode);

    if (manualSendDataLabel != nullptr) {
        manualSendDataLabel->setText(is485Mode ? QStringLiteral("串口HEX") : QStringLiteral("数据"));
    }
    if (manualSendHintLabel != nullptr) {
        manualSendHintLabel->setText(is485Mode
            ? QStringLiteral("串口原始帧发送：请输入完整 HEX 字节，如 BB 00 22 00 00 22 7E")
            : QStringLiteral("CAN手动发送：输入 CAN ID 与数据字节"));
    }
    ui->sendDataEdit->setPlaceholderText(is485Mode
        ? QStringLiteral("完整串口帧 HEX，例如 BB 00 22 00 00 22 7E")
        : QString());
}

bool MainWindow::isOtaRunning() const
{
    if (m_otaStressRunning) {
        return true;
    }
    int protocolMode = appConfig.load().protocolMode;
    if (protocolMode == 1) {
        QingjuOtaService::State state = qingjuOtaService->state();
        return state == QingjuOtaService::State::QueryProgram ||
               state == QingjuOtaService::State::StartUpgrade ||
               state == QingjuOtaService::State::SendData ||
               state == QingjuOtaService::State::FinishUpgrade;
    } else if (protocolMode == 4) {
        HlOtaService::State state = hlOtaState;
        return state == HlOtaService::State::QueryProgram ||
               state == HlOtaService::State::StartUpgrade ||
               state == HlOtaService::State::SendData ||
               state == HlOtaService::State::FinishUpgrade;
    } else if (protocolMode == 2 || protocolMode == 3) {
        BbFfOtaService::State state = bbFfOtaState;
        return state == BbFfOtaService::State::StartUpgrade ||
               state == BbFfOtaService::State::SendData ||
               state == BbFfOtaService::State::FinishUpgrade;
    } else {
        OtaService::State state = otaService.state();
        return state == OtaService::State::QueryProgram ||
               state == OtaService::State::StartUpgrade ||
               state == OtaService::State::SendData ||
               state == OtaService::State::FinishUpgrade;
    }
}

void MainWindow::updateControlsState()
{
    const bool stressRunning = stressTestService.stats().running;
    const bool otaRunning = isOtaRunning();
    const bool productionRunning = productionTestService.isRunning();
    const int protocolMode = protocolModeCombo != nullptr ? protocolModeCombo->currentIndex() : 0;
    const bool is485Mode = (protocolMode == 2 || protocolMode == 3 || protocolMode == 4);

    if (is485Mode) {
        updateRs485TopStatus();
        
        // 串口面板
        if (serialPortCombo != nullptr) serialPortCombo->setEnabled(!serialOpened);
        if (serialBaudRateCombo != nullptr) serialBaudRateCombo->setEnabled(!serialOpened);
        if (serialRefreshBtn != nullptr) serialRefreshBtn->setEnabled(!serialOpened);
        if (serialOpenCloseBtn != nullptr) serialOpenCloseBtn->setEnabled(!stressRunning && !otaRunning);
        if (serialOneClickStartBtn != nullptr) serialOneClickStartBtn->setEnabled(!rs485Scanning && !otaRunning);

        // BB 面板
        if (bbStartBtn != nullptr) bbStartBtn->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (bbStopBtn != nullptr) bbStopBtn->setEnabled(serialOpened && rs485Scanning && !otaRunning);
        if (bbQueryOnceBtn != nullptr) bbQueryOnceBtn->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (bbQueryModeCombo != nullptr) bbQueryModeCombo->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (bbHostPollPeriodSpin != nullptr) bbHostPollPeriodSpin->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (bbQueryInfoBtn != nullptr) bbQueryInfoBtn->setEnabled(serialOpened && !otaRunning);
        if (bbSetPowerBtn != nullptr) bbSetPowerBtn->setEnabled(serialOpened && !otaRunning);
        if (bbQueryPowerBtn != nullptr) bbQueryPowerBtn->setEnabled(serialOpened && !otaRunning);
        if (bbPowerSpin != nullptr) bbPowerSpin->setEnabled(serialOpened && !otaRunning);

        // FF 面板
        if (ffStartBtn != nullptr) ffStartBtn->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (ffStopBtn != nullptr) ffStopBtn->setEnabled(serialOpened && rs485Scanning && !otaRunning);
        if (ffQueryOnceBtn != nullptr) ffQueryOnceBtn->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (ffQueryModeCombo != nullptr) ffQueryModeCombo->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (ffHostPollPeriodSpin != nullptr) ffHostPollPeriodSpin->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (ffQueryInfoBtn != nullptr) ffQueryInfoBtn->setEnabled(serialOpened && !otaRunning);
        if (ffRebootBtn != nullptr) ffRebootBtn->setEnabled(serialOpened && !otaRunning);
        if (ffQuerySwitchBtn != nullptr) ffQuerySwitchBtn->setEnabled(serialOpened && !otaRunning);
        if (ffSetPowerBtn != nullptr) ffSetPowerBtn->setEnabled(serialOpened && !otaRunning);
        if (ffQueryPowerBtn != nullptr) ffQueryPowerBtn->setEnabled(serialOpened && !otaRunning);
        if (ffPowerSpin != nullptr) ffPowerSpin->setEnabled(serialOpened && !otaRunning);
        if (ffMixerSpin != nullptr) ffMixerSpin->setEnabled(serialOpened && !otaRunning);
        if (ffIfAmpSpin != nullptr) ffIfAmpSpin->setEnabled(serialOpened && !otaRunning);
        if (ffThrdSpin != nullptr) ffThrdSpin->setEnabled(serialOpened && !otaRunning);
        if (ffSetDemodBtn != nullptr) ffSetDemodBtn->setEnabled(serialOpened && !otaRunning);
        if (ffQueryDemodBtn != nullptr) ffQueryDemodBtn->setEnabled(serialOpened && !otaRunning);

        // Hellobike 面板
        if (hlStartBtn != nullptr) hlStartBtn->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (hlStopBtn != nullptr) hlStopBtn->setEnabled(serialOpened && rs485Scanning && !otaRunning);
        if (hlQueryOnceBtn != nullptr) hlQueryOnceBtn->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (hlQueryModeCombo != nullptr) hlQueryModeCombo->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (hlHostPollPeriodSpin != nullptr) hlHostPollPeriodSpin->setEnabled(serialOpened && !rs485Scanning && !otaRunning);
        if (hlQueryInfoBtn != nullptr) hlQueryInfoBtn->setEnabled(serialOpened && !otaRunning);
        if (hlRebootBtn != nullptr) hlRebootBtn->setEnabled(serialOpened && !otaRunning);
        if (hlScanTimeSpin != nullptr) hlScanTimeSpin->setEnabled(serialOpened && !otaRunning);
        if (hlScanIntervalSpin != nullptr) hlScanIntervalSpin->setEnabled(serialOpened && !otaRunning);
        if (hlSavedCountSpin != nullptr) hlSavedCountSpin->setEnabled(serialOpened && !otaRunning);
        if (hlClearAfterReadCheck != nullptr) hlClearAfterReadCheck->setEnabled(serialOpened && !otaRunning);
        if (hlDecryptEnableCheck != nullptr) hlDecryptEnableCheck->setEnabled(serialOpened && !otaRunning);
        if (hlSetControlBtn != nullptr) hlSetControlBtn->setEnabled(serialOpened && !otaRunning);

        // 压测面板
        if (stressRunning) {
            if (stressStartBtn != nullptr) stressStartBtn->setEnabled(false);
            if (stressStopBtn != nullptr) stressStopBtn->setEnabled(true);
            if (stressResetBtn != nullptr) stressResetBtn->setEnabled(false);
            if (stressExportBtn != nullptr) stressExportBtn->setEnabled(false);
            if (stressDurationSecondsSpin != nullptr) stressDurationSecondsSpin->setEnabled(false);
            if (stressTargetSamplesSpin != nullptr) stressTargetSamplesSpin->setEnabled(false);
            if (stressAutoSaveCheckBox != nullptr) stressAutoSaveCheckBox->setEnabled(false);
            if (stressAutoExportSummaryCheckBox != nullptr) stressAutoExportSummaryCheckBox->setEnabled(false);
        } else {
            if (stressStartBtn != nullptr) stressStartBtn->setEnabled(serialOpened);
            if (stressStopBtn != nullptr) stressStopBtn->setEnabled(false);
            if (stressResetBtn != nullptr) stressResetBtn->setEnabled(true);
            if (stressExportBtn != nullptr) stressExportBtn->setEnabled(true);
            if (stressDurationSecondsSpin != nullptr) stressDurationSecondsSpin->setEnabled(true);
            if (stressTargetSamplesSpin != nullptr) stressTargetSamplesSpin->setEnabled(true);
            if (stressAutoSaveCheckBox != nullptr) stressAutoSaveCheckBox->setEnabled(true);
            if (stressAutoExportSummaryCheckBox != nullptr) stressAutoExportSummaryCheckBox->setEnabled(true);
        }

        // 强制禁用 CAN 按钮和 OTA 按钮
        ui->openDeviceBtn->setEnabled(false);
        ui->initCANBtn->setEnabled(false);
        ui->StartCANBtn->setEnabled(false);
        ui->reSetCANBtn->setEnabled(false);
        ui->closeDeviceBtn->setEnabled(false);
        ui->sendBtn->setEnabled(serialOpened && !rs485Scanning && !stressRunning);
        if (oneClickStartButton != nullptr) oneClickStartButton->setEnabled(false);
        
        if (protocolMode == 4 || protocolMode == 2 || protocolMode == 3) {
            if (otaRunning) {
                if (otaQueryBtn != nullptr) otaQueryBtn->setEnabled(false);
                if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setEnabled(false);
                if (otaSelectFileBtn != nullptr) otaSelectFileBtn->setEnabled(false);
                if (otaVersionEdit != nullptr) otaVersionEdit->setEnabled(false);
                if (otaAbortUpgradeBtn != nullptr) otaAbortUpgradeBtn->setEnabled(true);
            } else if (stressRunning) {
                if (otaQueryBtn != nullptr) otaQueryBtn->setEnabled(false);
                if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setEnabled(false);
                if (otaSelectFileBtn != nullptr) otaSelectFileBtn->setEnabled(false);
                if (otaVersionEdit != nullptr) otaVersionEdit->setEnabled(false);
                if (otaAbortUpgradeBtn != nullptr) otaAbortUpgradeBtn->setEnabled(false);
            } else {
                if (otaQueryBtn != nullptr) otaQueryBtn->setEnabled(serialOpened);
                if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setEnabled(serialOpened);
                if (otaSelectFileBtn != nullptr) otaSelectFileBtn->setEnabled(true);
                if (otaVersionEdit != nullptr) otaVersionEdit->setEnabled(true);
                if (otaAbortUpgradeBtn != nullptr) otaAbortUpgradeBtn->setEnabled(false);
            }
            if (otaErrorInjectionGroup != nullptr) otaErrorInjectionGroup->setEnabled(false);
            if (qjOtaAnomalyGroup != nullptr) qjOtaAnomalyGroup->setEnabled(false);
            if (qjOtaTargetCombo != nullptr) qjOtaTargetCombo->setEnabled(false);
        } else {
            if (otaQueryBtn != nullptr) otaQueryBtn->setEnabled(false);
            if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setEnabled(false);
            if (otaSelectFileBtn != nullptr) otaSelectFileBtn->setEnabled(false);
            if (otaAbortUpgradeBtn != nullptr) otaAbortUpgradeBtn->setEnabled(false);
            if (otaErrorInjectionGroup != nullptr) otaErrorInjectionGroup->setEnabled(false);
            if (qjOtaAnomalyGroup != nullptr) qjOtaAnomalyGroup->setEnabled(false);
            if (qjOtaTargetCombo != nullptr) qjOtaTargetCombo->setEnabled(false);
        }
    }
    else if (productionRunning) {
        ui->openDeviceBtn->setEnabled(false);
        ui->initCANBtn->setEnabled(false);
        ui->StartCANBtn->setEnabled(false);
        ui->reSetCANBtn->setEnabled(false);
        ui->closeDeviceBtn->setEnabled(false);
        ui->sendBtn->setEnabled(false);
        if (oneClickStartButton != nullptr) oneClickStartButton->setEnabled(false);

        if (rfidStartScanBtn != nullptr) rfidStartScanBtn->setEnabled(false);
        if (rfidStopScanBtn != nullptr) rfidStopScanBtn->setEnabled(false);
        if (rfidRestartBtn != nullptr) rfidRestartBtn->setEnabled(false);
        if (rfidSetPeriodBtn != nullptr) rfidSetPeriodBtn->setEnabled(false);
        if (rfidScanPeriodSpin != nullptr) rfidScanPeriodSpin->setEnabled(false);

        if (stressStartBtn != nullptr) stressStartBtn->setEnabled(false);
        if (stressStopBtn != nullptr) stressStopBtn->setEnabled(false);
        if (stressResetBtn != nullptr) stressResetBtn->setEnabled(false);
        if (stressExportBtn != nullptr) stressExportBtn->setEnabled(false);
        if (stressDurationSecondsSpin != nullptr) stressDurationSecondsSpin->setEnabled(false);
        if (stressTargetSamplesSpin != nullptr) stressTargetSamplesSpin->setEnabled(false);
        if (stressAutoSaveCheckBox != nullptr) stressAutoSaveCheckBox->setEnabled(false);
        if (stressAutoExportSummaryCheckBox != nullptr) stressAutoExportSummaryCheckBox->setEnabled(false);

        if (otaQueryBtn != nullptr) otaQueryBtn->setEnabled(false);
        if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setEnabled(false);
        if (otaSelectFileBtn != nullptr) otaSelectFileBtn->setEnabled(false);
        if (otaAbortUpgradeBtn != nullptr) otaAbortUpgradeBtn->setEnabled(false);
        if (otaErrorInjectionGroup != nullptr) otaErrorInjectionGroup->setEnabled(false);
        if (qjOtaAnomalyGroup != nullptr) qjOtaAnomalyGroup->setEnabled(false);
        if (qjOtaTargetCombo != nullptr) qjOtaTargetCombo->setEnabled(false);
    }
    else if (otaRunning) {
        // 1. OTA 升级期间：
        // 禁用 CAN 连接配置与启闭
        ui->openDeviceBtn->setEnabled(false);
        ui->initCANBtn->setEnabled(false);
        ui->StartCANBtn->setEnabled(false);
        ui->reSetCANBtn->setEnabled(false);
        ui->closeDeviceBtn->setEnabled(false);
        if (oneClickStartButton != nullptr) {
            oneClickStartButton->setEnabled(false);
        }
        ui->sendBtn->setEnabled(false);

        // 禁用 RFID 定位器控制
        if (rfidStartScanBtn != nullptr) rfidStartScanBtn->setEnabled(false);
        if (rfidStopScanBtn != nullptr) rfidStopScanBtn->setEnabled(false);
        if (rfidRestartBtn != nullptr) rfidRestartBtn->setEnabled(false);
        if (rfidSetPeriodBtn != nullptr) rfidSetPeriodBtn->setEnabled(false);
        if (rfidScanPeriodSpin != nullptr) rfidScanPeriodSpin->setEnabled(false);

        if (qjStartBtn != nullptr) qjStartBtn->setEnabled(false);
        if (qjStopBtn != nullptr) qjStopBtn->setEnabled(false);
        if (qjRfidPeriodSpin != nullptr) qjRfidPeriodSpin->setEnabled(false);
        if (qjQueryModeCombo != nullptr) qjQueryModeCombo->setEnabled(false);
        if (qjHostPollPeriodSpin != nullptr) qjHostPollPeriodSpin->setEnabled(false);
        if (qjDistanceQueryOnceBtn != nullptr) qjDistanceQueryOnceBtn->setEnabled(false);

        // 禁用压力测试控制
        if (stressStartBtn != nullptr) stressStartBtn->setEnabled(false);
        if (stressStopBtn != nullptr) stressStopBtn->setEnabled(false);
        if (stressResetBtn != nullptr) stressResetBtn->setEnabled(false);
        if (stressExportBtn != nullptr) stressExportBtn->setEnabled(false);
        if (stressDurationSecondsSpin != nullptr) stressDurationSecondsSpin->setEnabled(false);
        if (stressTargetSamplesSpin != nullptr) stressTargetSamplesSpin->setEnabled(false);
        if (stressAutoSaveCheckBox != nullptr) stressAutoSaveCheckBox->setEnabled(false);
        if (stressAutoExportSummaryCheckBox != nullptr) stressAutoExportSummaryCheckBox->setEnabled(false);

        // 限制 OTA 本身操作：禁用开始/查询/选择固件，仅使能中止升级
        if (otaQueryBtn != nullptr) otaQueryBtn->setEnabled(false);
        if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setEnabled(false);
        if (otaSelectFileBtn != nullptr) otaSelectFileBtn->setEnabled(false);
        if (otaAbortUpgradeBtn != nullptr) otaAbortUpgradeBtn->setEnabled(true);
        if (otaErrorInjectionGroup != nullptr) otaErrorInjectionGroup->setEnabled(false);
        if (qjOtaAnomalyGroup != nullptr) qjOtaAnomalyGroup->setEnabled(false);
        if (qjOtaTargetCombo != nullptr) qjOtaTargetCombo->setEnabled(false);
    }
    else if (stressRunning) {
        // 2. 压力测试期间：
        // 禁用影响测试完整性的 CAN 断连、复位与发送
        ui->openDeviceBtn->setEnabled(false);
        ui->initCANBtn->setEnabled(false);
        ui->StartCANBtn->setEnabled(false);
        ui->reSetCANBtn->setEnabled(false);
        ui->closeDeviceBtn->setEnabled(false);
        if (oneClickStartButton != nullptr) {
            oneClickStartButton->setEnabled(false);
        }
        ui->sendBtn->setEnabled(false);

        // 禁用 RFID 控制防周期指令冲突
        if (rfidStartScanBtn != nullptr) rfidStartScanBtn->setEnabled(false);
        if (rfidStopScanBtn != nullptr) rfidStopScanBtn->setEnabled(false);
        if (rfidRestartBtn != nullptr) rfidRestartBtn->setEnabled(false);
        if (rfidSetPeriodBtn != nullptr) rfidSetPeriodBtn->setEnabled(false);
        if (rfidScanPeriodSpin != nullptr) rfidScanPeriodSpin->setEnabled(false);

        if (qjStartBtn != nullptr) qjStartBtn->setEnabled(false);
        if (qjStopBtn != nullptr) qjStopBtn->setEnabled(false);
        if (qjRfidPeriodSpin != nullptr) qjRfidPeriodSpin->setEnabled(false);
        if (qjQueryModeCombo != nullptr) qjQueryModeCombo->setEnabled(false);
        if (qjHostPollPeriodSpin != nullptr) qjHostPollPeriodSpin->setEnabled(false);
        if (qjDistanceQueryOnceBtn != nullptr) qjDistanceQueryOnceBtn->setEnabled(false);

        // 限制压力测试操作：仅允许停止，锁定参数配置与重置
        if (stressStartBtn != nullptr) stressStartBtn->setEnabled(false);
        if (stressStopBtn != nullptr) stressStopBtn->setEnabled(true);
        if (stressResetBtn != nullptr) stressResetBtn->setEnabled(false);
        if (stressExportBtn != nullptr) stressExportBtn->setEnabled(false);
        if (stressDurationSecondsSpin != nullptr) stressDurationSecondsSpin->setEnabled(false);
        if (stressTargetSamplesSpin != nullptr) stressTargetSamplesSpin->setEnabled(false);
        if (stressAutoSaveCheckBox != nullptr) stressAutoSaveCheckBox->setEnabled(false);
        if (stressAutoExportSummaryCheckBox != nullptr) stressAutoExportSummaryCheckBox->setEnabled(false);

        // 禁用 OTA 升级
        if (otaQueryBtn != nullptr) otaQueryBtn->setEnabled(false);
        if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setEnabled(false);
        if (otaSelectFileBtn != nullptr) otaSelectFileBtn->setEnabled(false);
        if (otaAbortUpgradeBtn != nullptr) otaAbortUpgradeBtn->setEnabled(false);
        if (otaErrorInjectionGroup != nullptr) otaErrorInjectionGroup->setEnabled(false);
        if (qjOtaAnomalyGroup != nullptr) qjOtaAnomalyGroup->setEnabled(false);
        if (qjOtaTargetCombo != nullptr) qjOtaTargetCombo->setEnabled(false);
    }
    else {
        // 3. 常规空闲状态：
        // 恢复 CAN 物理启闭状态按钮
        ui->openDeviceBtn->setEnabled(!deviceOpened);
        ui->initCANBtn->setEnabled(deviceOpened && !canInitialized);
        ui->StartCANBtn->setEnabled(canInitialized && !canStarted);
        ui->reSetCANBtn->setEnabled(canStarted);
        ui->closeDeviceBtn->setEnabled(deviceOpened);
        ui->sendBtn->setEnabled(canStarted);
        if (oneClickStartButton != nullptr) {
            oneClickStartButton->setEnabled(!canStarted);
        }

        // 根据 CAN 启动状态使能 RFID 控制
        if (rfidStartScanBtn != nullptr) rfidStartScanBtn->setEnabled(canStarted);
        if (rfidStopScanBtn != nullptr) rfidStopScanBtn->setEnabled(canStarted);
        if (rfidRestartBtn != nullptr) rfidRestartBtn->setEnabled(canStarted);
        if (rfidSetPeriodBtn != nullptr) rfidSetPeriodBtn->setEnabled(canStarted);
        if (rfidScanPeriodSpin != nullptr) rfidScanPeriodSpin->setEnabled(canStarted);

        // 青桔 RFID 控制
        const bool qjScanning = qingjuRfidService->isScanning();
        if (qjStartBtn != nullptr) qjStartBtn->setEnabled(canStarted && !qjScanning);
        if (qjStopBtn != nullptr) qjStopBtn->setEnabled(canStarted && qjScanning);
        if (qjRfidPeriodSpin != nullptr) qjRfidPeriodSpin->setEnabled(canStarted && !qjScanning);

        bool isAutoPoll = true;
        if (qjQueryModeCombo != nullptr) {
            isAutoPoll = (qjQueryModeCombo->currentIndex() == 0);
            qjQueryModeCombo->setEnabled(canStarted && !qjScanning);
        }
        if (qjHostPollPeriodSpin != nullptr) {
            qjHostPollPeriodSpin->setEnabled(canStarted && !qjScanning && isAutoPoll);
        }
        if (qjDistanceQueryOnceBtn != nullptr) {
            qjDistanceQueryOnceBtn->setEnabled(canStarted && qjScanning && !isAutoPoll);
        }

        // 根据 CAN 启动状态使能压力测试控制
        if (stressStartBtn != nullptr) stressStartBtn->setEnabled(canStarted);
        if (stressStopBtn != nullptr) stressStopBtn->setEnabled(false); // 停止置灰
        if (stressResetBtn != nullptr) stressResetBtn->setEnabled(true);
        if (stressExportBtn != nullptr) stressExportBtn->setEnabled(true);
        if (stressDurationSecondsSpin != nullptr) stressDurationSecondsSpin->setEnabled(true);
        if (stressTargetSamplesSpin != nullptr) stressTargetSamplesSpin->setEnabled(true);
        if (stressAutoSaveCheckBox != nullptr) stressAutoSaveCheckBox->setEnabled(true);
        if (stressAutoExportSummaryCheckBox != nullptr) stressAutoExportSummaryCheckBox->setEnabled(true);

        // 根据 CAN 启动状态使能 OTA 控制
        if (otaQueryBtn != nullptr) otaQueryBtn->setEnabled(canStarted);
        if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setEnabled(canStarted);
        if (otaSelectFileBtn != nullptr) otaSelectFileBtn->setEnabled(true); // 可以断连时选择文件
        if (otaAbortUpgradeBtn != nullptr) otaAbortUpgradeBtn->setEnabled(false);
        if (otaErrorInjectionGroup != nullptr) otaErrorInjectionGroup->setEnabled(true);
        if (qjOtaAnomalyGroup != nullptr) qjOtaAnomalyGroup->setEnabled(true);
        if (qjOtaTargetCombo != nullptr) qjOtaTargetCombo->setEnabled(true);
    }

    if (rfidTabs != nullptr && productionTestTab != nullptr) {
        const int productionIndex = rfidTabs->indexOf(productionTestTab);
        if (productionIndex >= 0) {
            rfidTabs->setTabEnabled(productionIndex, protocolMode == 0 || productionRunning);
        }
    }
    updateProductionTestPanel(productionTestService.state());
    updateTestExecutionControls();
}


void MainWindow::oneClickStartCan()
{
    if (ui->openDeviceBtn->isEnabled()) {
        on_openDeviceBtn_clicked();
        if (ui->openDeviceBtn->isEnabled()) {
            if (canDeviceStatusValue != nullptr) {
                canDeviceStatusValue->setText(QStringLiteral("状态：打开失败"));
            }
            if (topCanStatusValue != nullptr) {
                topCanStatusValue->setText(QStringLiteral("CAN：打开失败"));
            }
            return;
        }
        if (canDeviceStatusValue != nullptr) {
            canDeviceStatusValue->setText(QStringLiteral("状态：设备已打开"));
        }
    }

    if (ui->initCANBtn->isEnabled()) {
        on_initCANBtn_clicked();
        if (ui->initCANBtn->isEnabled()) {
            if (canDeviceStatusValue != nullptr) {
                canDeviceStatusValue->setText(QStringLiteral("状态：初始化失败"));
            }
            if (topCanStatusValue != nullptr) {
                topCanStatusValue->setText(QStringLiteral("CAN：初始化失败"));
            }
            return;
        }
        if (canDeviceStatusValue != nullptr) {
            canDeviceStatusValue->setText(QStringLiteral("状态：已初始化"));
        }
    }

    if (ui->StartCANBtn->isEnabled()) {
        on_StartCANBtn_clicked();
        if (ui->StartCANBtn->isEnabled()) {
            if (canDeviceStatusValue != nullptr) {
                canDeviceStatusValue->setText(QStringLiteral("状态：启动失败"));
            }
            if (topCanStatusValue != nullptr) {
                topCanStatusValue->setText(QStringLiteral("CAN：启动失败"));
            }
            return;
        }
        if (canDeviceStatusValue != nullptr) {
            canDeviceStatusValue->setText(QStringLiteral("状态：CAN已启动"));
        }
        if (oneClickStartButton != nullptr) {
            oneClickStartButton->setEnabled(false);
        }
    }
}

void MainWindow::startStressTest()
{
    const int protocolMode = protocolModeCombo != nullptr ? protocolModeCombo->currentIndex() : 0;
    const bool is485Mode = (protocolMode == 2 || protocolMode == 3 || protocolMode == 4);

    if (is485Mode) {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先打开串口，再开始压力测试。"));
            return;
        }
    } else {
        if (!canStarted) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先启动CAN，再开始压力测试。"));
            logService.logRuntime(LogLevel::Warning, QStringLiteral("Stress test start blocked: CAN is not started"));
            return;
        }
    }

    const StressTestStats currentStats = stressTestService.stats();
    if (currentStats.totalSamples > 0 || currentStats.successCount > 0) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            QStringLiteral("确认"),
            QStringLiteral("当前已有压力测试统计数据，是否清零后开始新的测试？"));
        if (answer != QMessageBox::Yes) {
            return;
        }
        stressTestService.reset();
    }

    if (stressProgressBar != nullptr) {
        stressProgressBar->setValue(0);
    }
    if (stressRemainingLabel != nullptr) {
        stressRemainingLabel->setText(QStringLiteral("正在进行中..."));
    }

    if (is485Mode) {
        int intervalMs = 500;
        if (protocolMode == 2) { // BB
            intervalMs = bbHostPollPeriodSpin->value();
        } else if (protocolMode == 3) { // FF
            intervalMs = ffHostPollPeriodSpin->value();
        } else if (protocolMode == 4) { // Hellobike
            intervalMs = hlHostPollPeriodSpin->value();
            syncHlConfigToService();
        }
        constexpr int AutoPollReadMode = 1;
        emit requestRs485StartScan(intervalMs, AutoPollReadMode);
        logService.logRuntime(LogLevel::Info,
                              QString("RS485 stress test polling interval: %1 ms").arg(intervalMs));
    } else {
        const bool qingjuMode = (protocolMode == 1);
        if (qingjuMode) {
            const int intervalMs = qjRfidPeriodSpin == nullptr ? 100 : qjRfidPeriodSpin->value();
            qingjuRfidService->startScan(intervalMs);
        } else {
            rfidScanning = true; // 开启周期发送以支持持续读卡
            sendRfidFrame(RfidProtocol::ControlFrameId, RfidProtocol::buildControlFrame(true));
            rfidControlTimer->start(); // 开启压测时同步启动 0x207 周期发送
        }
    }
    
    stressTestService.start();
    stressRefreshTimer->start();
    updateStressTestPanel(stressTestService.stats());
    
    if (is485Mode) {
        logService.logRuntime(LogLevel::Info, QStringLiteral("RS485 stress test started"));
    } else {
        logService.logRuntime(LogLevel::Info, (protocolMode == 1) ? QStringLiteral("Qingju stress test started")
                                                                 : QStringLiteral("Stress test started"));
    }
    updateControlsState();
}

void MainWindow::stopStressTest(bool autoStopped)
{
    if (!stressTestService.stats().running) {
        return;
    }

    const int protocolMode = protocolModeCombo != nullptr ? protocolModeCombo->currentIndex() : 0;
    const bool is485Mode = (protocolMode == 2 || protocolMode == 3 || protocolMode == 4);

    stressTestService.stop();
    stressRefreshTimer->stop();

    if (is485Mode) {
        emit requestRs485StopScan();
    } else {
        const bool qingjuMode = (protocolMode == 1);
        if (qingjuMode) {
            qingjuRfidService->stopScan();
        } else {
            rfidScanning = false; // 关闭周期发送，切回空闲状态
            sendRfidFrame(RfidProtocol::ControlFrameId, RfidProtocol::buildControlFrame(false));
            if (canStarted && rfidControlEnabledCheck != nullptr && rfidControlEnabledCheck->isChecked()) {
                rfidControlTimer->start();
            } else {
                rfidControlTimer->stop();
            }
        }
    }
    
    updateStressTestPanel(stressTestService.stats());
    logService.logRuntime(LogLevel::Info, autoStopped ? QStringLiteral("Stress test auto stopped")
                                                      : QStringLiteral("Stress test stopped"));

    if (autoStopped) {
        if (stressProgressBar != nullptr) {
            stressProgressBar->setValue(100);
        }
        if (stressRemainingLabel != nullptr) {
            stressRemainingLabel->setText(QStringLiteral("测试完成"));
        }
    } else {
        if (stressRemainingLabel != nullptr) {
            stressRemainingLabel->setText(QStringLiteral("测试已手动停止"));
        }
    }

    if (autoStopped && stressAutoExportSummaryCheckBox != nullptr &&
        stressAutoExportSummaryCheckBox->isChecked()) {
        exportStressSummary();
    }
    updateControlsState();
}

void MainWindow::refreshStressTestTick()
{
    const StressTestStats stats = stressTestService.stats();
    if (!stats.running) {
        stressRefreshTimer->stop();
        updateStressTestPanel(stats);
        return;
    }

    updateStressTestPanel(stats);
    
    const int durationSeconds = stressDurationSecondsSpin == nullptr ? 0 : stressDurationSecondsSpin->value();
    const int targetSamples = stressTargetSamplesSpin == nullptr ? 0 : stressTargetSamplesSpin->value();

    // 容错与进度百分比计算
    int progressPercent = 0;
    QString remainingText = QStringLiteral("剩余目标：不限");

    if (durationSeconds > 0 || targetSamples > 0) {
        double durPercent = 0.0;
        double samplePercent = 0.0;

        if (durationSeconds > 0) {
            durPercent = (static_cast<double>(stats.elapsedSeconds) / durationSeconds) * 100.0;
            qint64 remSec = qMax(0LL, static_cast<qint64>(durationSeconds) - stats.elapsedSeconds);
            remainingText = QStringLiteral("剩余时间：%1 秒").arg(remSec);
        }
        if (targetSamples > 0) {
            samplePercent = (static_cast<double>(stats.totalSamples) / targetSamples) * 100.0;
            qint64 remSamples = qMax(0LL, static_cast<qint64>(targetSamples) - static_cast<qint64>(stats.totalSamples));
            if (durationSeconds > 0) {
                remainingText += QStringLiteral(" | 剩余次数：%1 次").arg(remSamples);
            } else {
                remainingText = QStringLiteral("剩余次数：%1 次").arg(remSamples);
            }
        }

        progressPercent = static_cast<int>(qMax(durPercent, samplePercent));
        progressPercent = qBound(0, progressPercent, 100);
    }

    if (stressProgressBar != nullptr) {
        stressProgressBar->setValue(progressPercent);
    }
    if (stressRemainingLabel != nullptr) {
        stressRemainingLabel->setText(remainingText);
    }

    if ((durationSeconds > 0 && stats.elapsedSeconds >= durationSeconds) ||
        (targetSamples > 0 && stats.totalSamples >= static_cast<quint64>(targetSamples))) {
        stopStressTest(true);
    }
}

bool MainWindow::exportStressSummary()
{
    const QString filePath = QDir(logDirectory).filePath(
        QString("stress_summary_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    if (stressTestService.exportSummary(filePath)) {
        logService.logRuntime(LogLevel::Info, QString("Stress summary exported: %1").arg(filePath));
        return true;
    }
    logService.logRuntime(LogLevel::Error, QString("Failed to export stress summary: %1").arg(filePath));
    return false;
}

QWidget *MainWindow::createRfidMonitorTab(QWidget *parent)
{
    QScrollArea *scrollArea = new QScrollArea(parent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QGroupBox *rfidGroup = new QGroupBox(QStringLiteral("RFID定位器"), parent);
    rfidGroup->setMinimumWidth(900);

    QGridLayout *layout = new QGridLayout(rfidGroup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setHorizontalSpacing(10);
    layout->setVerticalSpacing(6);

    rfidStartScanBtn = new QPushButton(QStringLiteral("开始检测"), rfidGroup);
    rfidStopScanBtn = new QPushButton(QStringLiteral("停止检测"), rfidGroup);
    rfidRestartBtn = new QPushButton(QStringLiteral("模块重启"), rfidGroup);
    rfidScanPeriodSpin = new QSpinBox(rfidGroup);
    rfidScanPeriodSpin->setRange(10, 2550);
    rfidScanPeriodSpin->setSingleStep(10);
    rfidScanPeriodSpin->setSuffix(QStringLiteral(" ms"));
    rfidScanPeriodSpin->setMinimumWidth(80);
    rfidSetPeriodBtn = new QPushButton(QStringLiteral("设置周期"), rfidGroup);

    rfidWorkModeValue = new QLabel("-", rfidGroup);
    rfidCardStatusValue = new QLabel("-", rfidGroup);
    rfidFaultStatusValue = new QLabel("-", rfidGroup);
    rfidScanPeriodValue = new QLabel("-", rfidGroup);

    rfidTagValue = new QLabel("-", rfidGroup);
    rfidTagPart1Value = new QLabel("-", rfidGroup);
    rfidTagPart2Value = new QLabel("-", rfidGroup);
    rfidTagPart3Value = new QLabel("-", rfidGroup);
    rfidDeviceIdValue = new QLabel("-", rfidGroup);
    rfidVersionValue = new QLabel("-", rfidGroup);
    rfidResponseValue = new QLabel("-", rfidGroup);
    rfidOnlineStatusValue = new QLabel("-", rfidGroup);
    rfidOnlineStatusValue->setStyleSheet("color: gray; font-weight: bold;");

    rfidTagValue->setWordWrap(true);
    rfidTagPart1Value->setWordWrap(true);
    rfidTagPart2Value->setWordWrap(true);
    rfidTagPart3Value->setWordWrap(true);
    rfidDeviceIdValue->setWordWrap(true);
    rfidVersionValue->setWordWrap(true);
    rfidResponseValue->setWordWrap(true);

    QGroupBox *controlGroup = new QGroupBox(QStringLiteral("控制"), rfidGroup);
    QGridLayout *controlLayout = new QGridLayout(controlGroup);
    controlLayout->setContentsMargins(6, 6, 6, 6);
    controlLayout->setHorizontalSpacing(6);
    controlLayout->setVerticalSpacing(4);
    rfidControlEnabledCheck = new QCheckBox(QStringLiteral("启用RFID控制 (0x207)"), rfidGroup);
    controlLayout->addWidget(rfidStartScanBtn, 0, 0);
    controlLayout->addWidget(rfidStopScanBtn, 0, 1);
    controlLayout->addWidget(rfidScanPeriodSpin, 1, 0);
    controlLayout->addWidget(rfidSetPeriodBtn, 1, 1);
    controlLayout->addWidget(rfidRestartBtn, 2, 0, 1, 2);
    controlLayout->addWidget(rfidControlEnabledCheck, 3, 0, 1, 2);

    QGroupBox *tagGroup = new QGroupBox(QStringLiteral("TAG"), rfidGroup);
    QGridLayout *tagLayout = new QGridLayout(tagGroup);
    tagLayout->setContentsMargins(6, 6, 6, 6);
    tagLayout->setHorizontalSpacing(8);
    tagLayout->setVerticalSpacing(3);
    tagLayout->addWidget(new QLabel(QStringLiteral("完整TAG"), tagGroup), 0, 0);
    tagLayout->addWidget(rfidTagValue, 0, 1);
    tagLayout->addWidget(new QLabel(QStringLiteral("0x2C1"), tagGroup), 1, 0);
    tagLayout->addWidget(rfidTagPart1Value, 1, 1);
    tagLayout->addWidget(new QLabel(QStringLiteral("0x2C2"), tagGroup), 2, 0);
    tagLayout->addWidget(rfidTagPart2Value, 2, 1);
    tagLayout->addWidget(new QLabel(QStringLiteral("0x2C3"), tagGroup), 3, 0);
    tagLayout->addWidget(rfidTagPart3Value, 3, 1);
    tagLayout->setColumnStretch(0, 0);
    tagLayout->setColumnStretch(1, 1);

    QGroupBox *statusGroup = new QGroupBox(QStringLiteral("状态信息"), rfidGroup);
    QGridLayout *statusLayout = new QGridLayout(statusGroup);
    statusLayout->setContentsMargins(6, 6, 6, 6);
    statusLayout->setHorizontalSpacing(8);
    statusLayout->setVerticalSpacing(3);
    statusLayout->addWidget(new QLabel(QStringLiteral("模块状态"), statusGroup), 0, 0);
    statusLayout->addWidget(rfidOnlineStatusValue, 0, 1);
    statusLayout->addWidget(new QLabel(QStringLiteral("工作模式"), statusGroup), 1, 0);
    statusLayout->addWidget(rfidWorkModeValue, 1, 1);
    statusLayout->addWidget(new QLabel(QStringLiteral("卡状态"), statusGroup), 2, 0);
    statusLayout->addWidget(rfidCardStatusValue, 2, 1);
    statusLayout->addWidget(new QLabel(QStringLiteral("模块故障码"), statusGroup), 3, 0);
    statusLayout->addWidget(rfidFaultStatusValue, 3, 1);
    statusLayout->addWidget(new QLabel(QStringLiteral("扫描周期"), statusGroup), 4, 0);
    statusLayout->addWidget(rfidScanPeriodValue, 4, 1);

    QGroupBox *deviceGroup = new QGroupBox(QStringLiteral("设备信息"), rfidGroup);
    QGridLayout *deviceLayout = new QGridLayout(deviceGroup);
    deviceLayout->setContentsMargins(6, 6, 6, 6);
    deviceLayout->setHorizontalSpacing(8);
    deviceLayout->setVerticalSpacing(3);
    deviceLayout->addWidget(new QLabel(QStringLiteral("设备ID"), deviceGroup), 0, 0);
    deviceLayout->addWidget(rfidDeviceIdValue, 0, 1);
    deviceLayout->addWidget(new QLabel(QStringLiteral("版本"), deviceGroup), 1, 0);
    deviceLayout->addWidget(rfidVersionValue, 1, 1);
    deviceLayout->addWidget(new QLabel(QStringLiteral("响应"), deviceGroup), 2, 0);
    deviceLayout->addWidget(rfidResponseValue, 2, 1);
    // 通用诊断控制分组
    QGroupBox *diagnosticGroup = new QGroupBox(QStringLiteral("通用诊断控制"), rfidGroup);
    QGridLayout *diagLayout = new QGridLayout(diagnosticGroup);
    diagLayout->setContentsMargins(6, 6, 6, 6);
    diagLayout->setHorizontalSpacing(6);
    diagLayout->setVerticalSpacing(4);

    // Row 0: 0x10 跳转控制
    diagLayout->addWidget(new QLabel(QStringLiteral("0x10 跳转"), diagnosticGroup), 0, 0);
    QComboBox *diagJumpCombo = new QComboBox(diagnosticGroup);
    diagJumpCombo->addItem(QStringLiteral("APP"), 0x01);
    diagJumpCombo->addItem(QStringLiteral("BOOT"), 0x02);
    diagLayout->addWidget(diagJumpCombo, 0, 1);
    QPushButton *diagJumpBtn = new QPushButton(QStringLiteral("执行跳转"), diagnosticGroup);
    diagLayout->addWidget(diagJumpBtn, 0, 2);

    // Row 1: 0x11 软件复位
    diagLayout->addWidget(new QLabel(QStringLiteral("0x11 复位"), diagnosticGroup), 1, 0);
    QPushButton *diagResetBtn = new QPushButton(QStringLiteral("软件复位"), diagnosticGroup);
    diagLayout->addWidget(diagResetBtn, 1, 1, 1, 2);

    // Row 2: 0x28 通信控制
    diagLayout->addWidget(new QLabel(QStringLiteral("0x28 广播控制"), diagnosticGroup), 2, 0);
    QComboBox *diagBroadcastCombo = new QComboBox(diagnosticGroup);
    diagBroadcastCombo->addItem(QStringLiteral("禁止周期发送"), 0x00);
    diagBroadcastCombo->addItem(QStringLiteral("使能周期发送"), 0x01);
    diagLayout->addWidget(diagBroadcastCombo, 2, 1);
    QPushButton *diagBroadcastBtn = new QPushButton(QStringLiteral("应用控制"), diagnosticGroup);
    diagLayout->addWidget(diagBroadcastBtn, 2, 2);

    // Row 3: 0x29 广播周期配置
    diagLayout->addWidget(new QLabel(QStringLiteral("0x29 周期配置"), diagnosticGroup), 3, 0);
    QHBoxLayout *periodInputLayout = new QHBoxLayout();
    QComboBox *diagPeriodIdCombo = new QComboBox(diagnosticGroup);
    diagPeriodIdCombo->setEditable(true);
    diagPeriodIdCombo->setToolTip(QStringLiteral("可选择 0x2C0~0x2C6，或手动输入 0x000~0x7FF 标准帧ID。"));
    for (quint16 canId = 0x2C0; canId <= 0x2C6; ++canId) {
        diagPeriodIdCombo->addItem(QStringLiteral("0x%1")
                                       .arg(canId, 3, 16, QChar('0')).toUpper());
    }
    QSpinBox *diagPeriodValSpin = new QSpinBox(diagnosticGroup);
    diagPeriodValSpin->setRange(16, 65535);
    diagPeriodValSpin->setValue(100);
    diagPeriodValSpin->setSuffix(QStringLiteral(" ms"));
    diagPeriodValSpin->setToolTip(QStringLiteral("16~65535 ms; 65535为禁止发送"));
    periodInputLayout->addWidget(diagPeriodIdCombo);
    periodInputLayout->addWidget(diagPeriodValSpin);
    diagLayout->addLayout(periodInputLayout, 3, 1);
    QPushButton *diagPeriodBtn = new QPushButton(QStringLiteral("设置周期"), diagnosticGroup);
    diagLayout->addWidget(diagPeriodBtn, 3, 2);

    // Row 4: 0x85 通信故障诊断
    diagLayout->addWidget(new QLabel(QStringLiteral("0x85 故障诊断"), diagnosticGroup), 4, 0);
    QComboBox *diagDiagCombo = new QComboBox(diagnosticGroup);
    diagDiagCombo->addItem(QStringLiteral("禁用诊断"), 0x00);
    diagDiagCombo->addItem(QStringLiteral("启用诊断"), 0x01);
    diagLayout->addWidget(diagDiagCombo, 4, 1);
    QPushButton *diagDiagBtn = new QPushButton(QStringLiteral("应用诊断"), diagnosticGroup);
    diagLayout->addWidget(diagDiagBtn, 4, 2);

    // Row 5: 0x2E 写非易失
    diagLayout->addWidget(new QLabel(QStringLiteral("0x2E 写非易失"), diagnosticGroup), 5, 0);
    QHBoxLayout *writeInputLayout = new QHBoxLayout();
    QLineEdit *diagWriteDidEdit = new QLineEdit(diagnosticGroup);
    diagWriteDidEdit->setPlaceholderText(QStringLiteral("DID (HEX)"));
    diagWriteDidEdit->setToolTip(QStringLiteral("例如: 0x1234"));
    QComboBox *diagWriteFormatCombo = new QComboBox(diagnosticGroup);
    diagWriteFormatCombo->addItem(QStringLiteral("HEX"), 0);
    diagWriteFormatCombo->addItem(QStringLiteral("ASCII"), 1);
    QLineEdit *diagWriteDataEdit = new QLineEdit(diagnosticGroup);
    diagWriteDataEdit->setPlaceholderText(QStringLiteral("数据 (HEX)"));
    diagWriteDataEdit->setToolTip(QStringLiteral("HEX: AB CD 01；ASCII: SN1234567890。超过4字节自动使用ISO-TP多帧。"));
    writeInputLayout->addWidget(diagWriteDidEdit);
    writeInputLayout->addWidget(diagWriteFormatCombo);
    writeInputLayout->addWidget(diagWriteDataEdit);
    diagLayout->addLayout(writeInputLayout, 5, 1);
    QPushButton *diagWriteBtn = new QPushButton(QStringLiteral("写入"), diagnosticGroup);
    diagLayout->addWidget(diagWriteBtn, 5, 2);

    // Row 6: 0x2E 常用 DID 快捷写入
    diagLayout->addWidget(new QLabel(QStringLiteral("0x2E 快捷写入"), diagnosticGroup), 6, 0);
    QHBoxLayout *quickWriteLayout = new QHBoxLayout();
    QComboBox *diagQuickWriteCombo = new QComboBox(diagnosticGroup);
    diagQuickWriteCombo->addItem(QStringLiteral("硬件版本 0xE7E0"), 0xE7E0);
    diagQuickWriteCombo->addItem(QStringLiteral("ID号 0xE7E1"), 0xE7E1);
    QLineEdit *diagQuickWriteEdit = new QLineEdit(diagnosticGroup);
    diagQuickWriteEdit->setPlaceholderText(QStringLiteral("1.0.1 或 0x0101"));
    diagQuickWriteEdit->setToolTip(QStringLiteral("硬件版本: A.0.B -> AA BB；ID号: 16字节ASCII，例如 R2A3A02625000001"));
    quickWriteLayout->addWidget(diagQuickWriteCombo);
    quickWriteLayout->addWidget(diagQuickWriteEdit);
    diagLayout->addLayout(quickWriteLayout, 6, 1);
    QPushButton *diagQuickWriteBtn = new QPushButton(QStringLiteral("快捷写入"), diagnosticGroup);
    diagLayout->addWidget(diagQuickWriteBtn, 6, 2);

    layout->addWidget(controlGroup, 0, 0);
    layout->addWidget(diagnosticGroup, 1, 0);
    layout->addWidget(tagGroup, 2, 0);
    layout->addWidget(statusGroup, 0, 1);
    layout->addWidget(deviceGroup, 1, 1, 2, 1);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);

    auto ensureCanStarted = [this]() -> bool {
        if (!canStarted) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先一键启动或启动 CAN！"));
            return false;
        }
        return true;
    };

    connect(rfidStartScanBtn, &QPushButton::clicked, this, [this]() {
        rfidScanning = true;
        updateMeituanTopStatus();
        sendRfidFrame(RfidProtocol::ControlFrameId, RfidProtocol::buildControlFrame(true));
    });
    connect(rfidStopScanBtn, &QPushButton::clicked, this, [this]() {
        rfidScanning = false;
        updateMeituanTopStatus();
        sendRfidFrame(RfidProtocol::ControlFrameId, RfidProtocol::buildControlFrame(false));
    });
    connect(rfidControlEnabledCheck, &QCheckBox::stateChanged, this, [this](int state) {
        if (canStarted && state == Qt::Checked) {
            rfidControlTimer->start();
        } else {
            rfidControlTimer->stop();
        }
        saveAppConfig();
    });
    connect(rfidSetPeriodBtn, &QPushButton::clicked, this, [this]() {
        sendRfidFrame(RfidProtocol::RequestFrameId,
                      RfidProtocol::buildSetScanPeriodFrame(static_cast<quint8>(rfidScanPeriodSpin->value() / 10)));
    });
    connect(rfidRestartBtn, &QPushButton::clicked, this, [this]() {
        sendRfidFrame(RfidProtocol::RequestFrameId, RfidProtocol::buildRestartFrame());
    });

    // 绑定诊断面板事件
    connect(diagJumpBtn, &QPushButton::clicked, this, [this, diagJumpCombo, ensureCanStarted]() {
        if (!ensureCanStarted()) return;
        quint8 targetMode = static_cast<quint8>(diagJumpCombo->currentData().toUInt());
        QString modeText = diagJumpCombo->currentText();
        
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, QStringLiteral("跳转确认"),
            QStringLiteral("确认执行 %1 跳转吗？设备可能会短时离线。").arg(modeText),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
            sendRfidFrame(RfidProtocol::RequestFrameId, RfidProtocol::buildBootAppJumpFrame(targetMode));
        }
    });

    connect(diagResetBtn, &QPushButton::clicked, this, [this, ensureCanStarted]() {
        if (!ensureCanStarted()) return;
        
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, QStringLiteral("复位确认"),
            QStringLiteral("确认执行软件复位吗？设备会重启并短时无响应。"),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
            sendRfidFrame(RfidProtocol::RequestFrameId, RfidProtocol::buildSoftwareResetFrame());
        }
    });

    connect(diagBroadcastBtn, &QPushButton::clicked, this, [this, diagBroadcastCombo, ensureCanStarted]() {
        if (!ensureCanStarted()) return;
        bool enableBroadcast = diagBroadcastCombo->currentData().toBool();
        sendRfidFrame(RfidProtocol::RequestFrameId, RfidProtocol::buildCommunicationControlFrame(enableBroadcast));
    });

    connect(diagPeriodBtn, &QPushButton::clicked, this, [this, diagPeriodIdCombo, diagPeriodValSpin, ensureCanStarted]() {
        if (!ensureCanStarted()) return;
        
        quint16 canId = 0;
        if (!parseHexUInt16(diagPeriodIdCombo->currentText(), &canId)) {
            QMessageBox::warning(this, QStringLiteral("格式错误"), 
                QStringLiteral("目标ID格式无效，请输入 0x000~0x7FF 范围内的十六进制标准帧ID。"));
            return;
        }
        if (canId > 0x7FF) {
            QMessageBox::warning(this, QStringLiteral("超出范围"), 
                QStringLiteral("目标ID格式无效，请输入 0x000~0x7FF 范围内的十六进制标准帧ID。"));
            return;
        }
        
        if (canId < 0x2C0 || canId > 0x2DF) {
            QMessageBox::StandardButton reply = QMessageBox::question(
                this, QStringLiteral("设置确认"),
                QStringLiteral("目标 ID 0x%1 不在默认的 RFID 广播范围 (0x2C0~0x2DF) 内，确认仍要设置吗？")
                    .arg(QString::number(canId, 16).toUpper()),
                QMessageBox::Yes | QMessageBox::No
            );
            if (reply != QMessageBox::Yes) {
                return;
            }
        }
        
        quint16 periodMs = static_cast<quint16>(diagPeriodValSpin->value());
        sendRfidFrame(RfidProtocol::RequestFrameId, RfidProtocol::buildSetBroadcastPeriodFrame(canId, periodMs));
    });

    connect(diagDiagBtn, &QPushButton::clicked, this, [this, diagDiagCombo, ensureCanStarted]() {
        if (!ensureCanStarted()) return;
        bool enableDiag = diagDiagCombo->currentData().toBool();
        sendRfidFrame(RfidProtocol::RequestFrameId, RfidProtocol::buildCommunicationDiagnosticFrame(enableDiag));
    });

    connect(diagWriteFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [diagWriteFormatCombo, diagWriteDataEdit](int) {
        const bool asciiMode = diagWriteFormatCombo->currentData().toInt() == 1;
        diagWriteDataEdit->setPlaceholderText(asciiMode ? QStringLiteral("SN1234567890") : QStringLiteral("数据 (HEX)"));
    });

    connect(diagQuickWriteCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [diagQuickWriteCombo, diagQuickWriteEdit](int) {
        const quint16 did = static_cast<quint16>(diagQuickWriteCombo->currentData().toUInt());
        if (did == 0xE7E0) {
            diagQuickWriteEdit->setPlaceholderText(QStringLiteral("1.0.1 或 0x0101"));
        } else {
            diagQuickWriteEdit->setPlaceholderText(QStringLiteral("R2A3A02625000001"));
        }
    });

    connect(diagWriteBtn, &QPushButton::clicked, this, [this, diagWriteDidEdit, diagWriteFormatCombo, diagWriteDataEdit, ensureCanStarted]() {
        if (!ensureCanStarted()) return;
        
        quint16 did = 0;
        if (!parseHexUInt16(diagWriteDidEdit->text(), &did)) {
            QMessageBox::warning(this, QStringLiteral("格式错误"), 
                QStringLiteral("DID格式无效，请输入 0x0000~0xFFFF 范围内的十六进制值。"));
            return;
        }
        
        QByteArray data;
        const bool asciiMode = diagWriteFormatCombo->currentData().toInt() == 1;
        if (asciiMode) {
            const QString text = diagWriteDataEdit->text().trimmed();
            if (text.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("格式错误"),
                    QStringLiteral("ASCII写入数据不能为空。"));
                return;
            }
            data = text.toLatin1();
            if (QString::fromLatin1(data) != text) {
                QMessageBox::warning(this, QStringLiteral("格式错误"),
                    QStringLiteral("ASCII模式仅支持可按Latin-1保存的字符，请确认SN码内容。"));
                return;
            }
        } else {
            if (!parseHexByteArray(diagWriteDataEdit->text(), &data)) {
                QMessageBox::warning(this, QStringLiteral("格式错误"),
                    QStringLiteral("写入数据格式无效，请输入完整HEX字节，例如 AB CD 01。"));
                return;
            }
        }
        
        const QString transferModeText = data.size() > 4 ? QStringLiteral("ISO-TP多帧") : QStringLiteral("单帧");
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, QStringLiteral("写入确认"),
            QStringLiteral("确认写入非易失存储区吗？\nDID=0x%1\n长度=%2字节\n方式=%3\n\n该操作可能改变设备持久化配置。")
                .arg(did, 4, 16, QChar('0')).toUpper()
                .arg(data.size())
                .arg(transferModeText),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
            rfidDiagnosticTransfer.startWriteNonVolatile(did, data);
        }
    });

    connect(diagQuickWriteBtn, &QPushButton::clicked, this, [this, diagQuickWriteCombo, diagQuickWriteEdit, ensureCanStarted]() {
        if (!ensureCanStarted()) return;

        const quint16 did = static_cast<quint16>(diagQuickWriteCombo->currentData().toUInt());
        const QString inputText = diagQuickWriteEdit->text().trimmed();
        if (inputText.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("格式错误"), QStringLiteral("快捷写入内容不能为空。"));
            return;
        }

        QByteArray data;
        QString valueText;
        if (did == 0xE7E0) {
            quint16 versionValue = 0;
            const QRegularExpression versionPattern(QStringLiteral("^(\\d+)\\.0\\.(\\d+)$"));
            const QRegularExpressionMatch versionMatch = versionPattern.match(inputText);
            if (versionMatch.hasMatch()) {
                bool majorOk = false;
                bool revisionOk = false;
                const int major = versionMatch.captured(1).toInt(&majorOk);
                const int revision = versionMatch.captured(2).toInt(&revisionOk);
                if (!majorOk || !revisionOk || major < 0 || major > 0xFF || revision < 0 || revision > 0xFF) {
                    QMessageBox::warning(this, QStringLiteral("格式错误"),
                        QStringLiteral("硬件版本范围无效，请输入 A.0.B，A/B 范围为 0~255，例如 1.0.1。"));
                    return;
                }
                versionValue = static_cast<quint16>((major << 8) | revision);
                valueText = QStringLiteral("%1.0.%2").arg(major).arg(revision);
            } else if (parseHexUInt16(inputText, &versionValue)) {
                valueText = QStringLiteral("0x%1")
                    .arg(versionValue, 4, 16, QChar('0')).toUpper();
            } else {
                QMessageBox::warning(this, QStringLiteral("格式错误"),
                    QStringLiteral("硬件版本请输入 A.0.B 格式，例如 1.0.1；也可输入 0x0101。"));
                return;
            }

            data.append(static_cast<char>((versionValue >> 8) & 0xFF));
            data.append(static_cast<char>(versionValue & 0xFF));
        } else if (did == 0xE7E1) {
            data = inputText.toLatin1();
            if (QString::fromLatin1(data) != inputText) {
                QMessageBox::warning(this, QStringLiteral("格式错误"),
                    QStringLiteral("ID号仅支持ASCII字符。"));
                return;
            }
            if (data.size() != 16) {
                QMessageBox::warning(this, QStringLiteral("格式错误"),
                    QStringLiteral("ID号必须正好为16个ASCII字符，例如 R2A3A02625000001。"));
                return;
            }
            valueText = inputText;
        } else {
            QMessageBox::warning(this, QStringLiteral("格式错误"), QStringLiteral("未知快捷写入项。"));
            return;
        }

        const QString didText = QStringLiteral("0x%1").arg(did, 4, 16, QChar('0')).toUpper();
        const QString dataText = QString::fromLatin1(data.toHex(' ').toUpper());
        const QString transferModeText = data.size() > 4 ? QStringLiteral("ISO-TP多帧") : QStringLiteral("单帧");
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, QStringLiteral("快捷写入确认"),
            QStringLiteral("确认执行快捷写入吗？\n项目=%1\nDID=%2\n值=%3\n数据=%4\n长度=%5字节\n方式=%6\n\n该操作会写入非易失存储区。")
                .arg(diagQuickWriteCombo->currentText())
                .arg(didText)
                .arg(valueText)
                .arg(dataText)
                .arg(data.size())
                .arg(transferModeText),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
            rfidDiagnosticTransfer.startWriteNonVolatile(did, data);
        }
    });

    scrollArea->setWidget(rfidGroup);
    return scrollArea;
}

QWidget *MainWindow::createProductionTestTab(QWidget *parent)
{
    QScrollArea *scrollArea = new QScrollArea(parent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *widget = new QWidget(scrollArea);
    widget->setMinimumWidth(900);
    QGridLayout *layout = new QGridLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    QGroupBox *inputGroup = new QGroupBox(QStringLiteral("扫码输入"), widget);
    QGridLayout *inputLayout = new QGridLayout(inputGroup);
    productionSnEdit = new QLineEdit(inputGroup);
    productionSnEdit->setPlaceholderText(QStringLiteral("扫码输入16位SN，例如 R2A3A02625000001"));
    productionSnEdit->setMaxLength(32);
    productionSnEdit->setClearButtonEnabled(true);
    productionPassThresholdSpin = new QSpinBox(inputGroup);
    productionPassThresholdSpin->setRange(0, 100);
    productionPassThresholdSpin->setValue(95);
    productionPassThresholdSpin->setSuffix(QStringLiteral(" %"));
    productionStartBtn = new QPushButton(QStringLiteral("开始检测"), inputGroup);
    productionStopBtn = new QPushButton(QStringLiteral("停止"), inputGroup);
    productionClearBtn = new QPushButton(QStringLiteral("清空/下一台"), inputGroup);

    inputLayout->addWidget(new QLabel(QStringLiteral("终端SN"), inputGroup), 0, 0);
    inputLayout->addWidget(productionSnEdit, 0, 1, 1, 4);
    inputLayout->addWidget(new QLabel(QStringLiteral("通过阈值"), inputGroup), 1, 0);
    inputLayout->addWidget(productionPassThresholdSpin, 1, 1);
    inputLayout->addWidget(productionStartBtn, 1, 2);
    inputLayout->addWidget(productionStopBtn, 1, 3);
    inputLayout->addWidget(productionClearBtn, 1, 4);
    inputLayout->setColumnStretch(1, 1);

    QGroupBox *resultGroup = new QGroupBox(QStringLiteral("检测结果"), widget);
    QGridLayout *resultLayout = new QGridLayout(resultGroup);
    resultLayout->setContentsMargins(8, 16, 8, 8);
    resultLayout->setVerticalSpacing(7);
    resultLayout->setHorizontalSpacing(18);
    resultGroup->setMinimumHeight(280);
    productionResultBanner = new QLabel(QStringLiteral("待扫码"), resultGroup);
    productionResultBanner->setAlignment(Qt::AlignCenter);
    productionResultBanner->setMinimumHeight(72);
    productionResultBanner->setMaximumHeight(72);
    productionResultBanner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QFont bannerFont = productionResultBanner->font();
    bannerFont.setPointSize(22);
    bannerFont.setBold(true);
    productionResultBanner->setFont(bannerFont);
    productionResultBanner->setStyleSheet(QStringLiteral("background:#E5E7EB;color:#374151;border-radius:6px;"));
    productionProgressBar = new QProgressBar(resultGroup);
    productionProgressBar->setRange(0, 100);
    productionProgressBar->setValue(0);
    productionProgressBar->setMinimumHeight(22);
    productionProgressBar->setMaximumHeight(22);

    productionStateValue = new QLabel(QStringLiteral("待扫码"), resultGroup);
    productionSnValue = new QLabel(QStringLiteral("-"), resultGroup);
    productionWriteValue = new QLabel(QStringLiteral("未写入"), resultGroup);
    productionProgressValue = new QLabel(QStringLiteral("0 / 100"), resultGroup);
    productionSuccessValue = new QLabel(QStringLiteral("0"), resultGroup);
    productionFailureValue = new QLabel(QStringLiteral("0"), resultGroup);
    productionRateValue = new QLabel(QStringLiteral("0.00%"), resultGroup);
    productionTagValue = new QLabel(QStringLiteral("-"), resultGroup);
    productionFailureReasonValue = new QLabel(QStringLiteral("-"), resultGroup);
    productionTagValue->setWordWrap(true);
    productionFailureReasonValue->setWordWrap(true);

    resultLayout->addWidget(productionResultBanner, 0, 0, 1, 4);
    resultLayout->addWidget(productionProgressBar, 1, 0, 1, 4);
    resultLayout->addWidget(new QLabel(QStringLiteral("状态"), resultGroup), 2, 0);
    resultLayout->addWidget(productionStateValue, 2, 1);
    resultLayout->addWidget(new QLabel(QStringLiteral("SN"), resultGroup), 2, 2);
    resultLayout->addWidget(productionSnValue, 2, 3);
    resultLayout->addWidget(new QLabel(QStringLiteral("写入"), resultGroup), 3, 0);
    resultLayout->addWidget(productionWriteValue, 3, 1);
    resultLayout->addWidget(new QLabel(QStringLiteral("进度"), resultGroup), 3, 2);
    resultLayout->addWidget(productionProgressValue, 3, 3);
    resultLayout->addWidget(new QLabel(QStringLiteral("成功"), resultGroup), 4, 0);
    resultLayout->addWidget(productionSuccessValue, 4, 1);
    resultLayout->addWidget(new QLabel(QStringLiteral("失败"), resultGroup), 4, 2);
    resultLayout->addWidget(productionFailureValue, 4, 3);
    resultLayout->addWidget(new QLabel(QStringLiteral("成功率"), resultGroup), 5, 0);
    resultLayout->addWidget(productionRateValue, 5, 1);
    resultLayout->addWidget(new QLabel(QStringLiteral("当前TAG"), resultGroup), 6, 0);
    resultLayout->addWidget(productionTagValue, 6, 1, 1, 3);
    resultLayout->addWidget(new QLabel(QStringLiteral("失败原因"), resultGroup), 7, 0);
    resultLayout->addWidget(productionFailureReasonValue, 7, 1, 1, 3);
    resultLayout->setRowMinimumHeight(0, 72);
    resultLayout->setRowMinimumHeight(1, 22);
    for (int row = 2; row <= 7; ++row) {
        resultLayout->setRowMinimumHeight(row, 20);
    }
    resultLayout->setColumnMinimumWidth(0, 72);
    resultLayout->setColumnMinimumWidth(2, 72);
    resultLayout->setColumnStretch(1, 1);
    resultLayout->setColumnStretch(3, 1);

    QGroupBox *logGroup = new QGroupBox(QStringLiteral("过程日志"), widget);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    productionLogText = new QTextEdit(logGroup);
    productionLogText->setReadOnly(true);
    productionLogText->setMinimumHeight(180);
    logLayout->addWidget(productionLogText);

    layout->addWidget(inputGroup, 0, 0);
    layout->addWidget(resultGroup, 1, 0);
    layout->addWidget(logGroup, 2, 0);
    layout->setRowMinimumHeight(1, 280);
    layout->setRowStretch(2, 1);

    connect(productionSnEdit, &QLineEdit::returnPressed, this, &MainWindow::startProductionTestFromInput);
    connect(productionSnEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (productionTestService.isRunning()) {
            return;
        }
        QString error;
        if (ProductionTestService::validateSn(text.trimmed().toUpper(), &error)) {
            QTimer::singleShot(200, this, [this, text]() {
                if (!productionTestService.isRunning() &&
                    productionSnEdit != nullptr &&
                    productionSnEdit->text().trimmed().compare(text.trimmed(), Qt::CaseInsensitive) == 0) {
                    processProductionScanText(text, false);
                }
            });
        }
    });
    connect(productionStartBtn, &QPushButton::clicked, this, &MainWindow::startProductionTestFromInput);
    connect(productionStopBtn, &QPushButton::clicked, this, &MainWindow::stopProductionTest);
    connect(productionClearBtn, &QPushButton::clicked, this, &MainWindow::clearProductionTestPanel);

    updateProductionTestPanel(productionTestService.state());
    scrollArea->setWidget(widget);
    return scrollArea;
}

QWidget *MainWindow::createTestExecutionTab(QWidget *parent)
{
    QScrollArea *scrollArea = new QScrollArea(parent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *widget = new QWidget(scrollArea);
    QVBoxLayout *mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    QGroupBox *sessionGroup = new QGroupBox(QStringLiteral("测试会话信息"), widget);
    QGridLayout *sessionLayout = new QGridLayout(sessionGroup);
    sessionLayout->setContentsMargins(8, 8, 8, 8);
    sessionLayout->setHorizontalSpacing(8);
    sessionLayout->setVerticalSpacing(6);

    testProjectEdit = new QLineEdit(QStringLiteral("美团RFID CAN通信"), sessionGroup);
    testSoftwareVersionEdit = new QLineEdit(sessionGroup);
    testFirmwareVersionEdit = new QLineEdit(sessionGroup);
    testDeviceSnEdit = new QLineEdit(sessionGroup);
    testTesterEdit = new QLineEdit(sessionGroup);
    testEnvironmentCombo = new QComboBox(sessionGroup);
    testEnvironmentCombo->setEditable(true);
    testEnvironmentCombo->addItems(QStringList() << QStringLiteral("台架") << QStringLiteral("整车") << QStringLiteral("产线") << QStringLiteral("实验室"));
    testSessionRemarkEdit = new QTextEdit(sessionGroup);
    testSessionRemarkEdit->setFixedHeight(44);
    testSessionDirectoryValue = new QLabel(QStringLiteral("-"), sessionGroup);
    testSessionDirectoryValue->setTextInteractionFlags(Qt::TextSelectableByMouse);

    sessionLayout->addWidget(new QLabel(QStringLiteral("项目名称"), sessionGroup), 0, 0);
    sessionLayout->addWidget(testProjectEdit, 0, 1);
    sessionLayout->addWidget(new QLabel(QStringLiteral("软件版本"), sessionGroup), 0, 2);
    sessionLayout->addWidget(testSoftwareVersionEdit, 0, 3);
    sessionLayout->addWidget(new QLabel(QStringLiteral("固件版本"), sessionGroup), 0, 4);
    sessionLayout->addWidget(testFirmwareVersionEdit, 0, 5);
    sessionLayout->addWidget(new QLabel(QStringLiteral("设备SN"), sessionGroup), 1, 0);
    sessionLayout->addWidget(testDeviceSnEdit, 1, 1);
    sessionLayout->addWidget(new QLabel(QStringLiteral("测试人员"), sessionGroup), 1, 2);
    sessionLayout->addWidget(testTesterEdit, 1, 3);
    sessionLayout->addWidget(new QLabel(QStringLiteral("测试环境"), sessionGroup), 1, 4);
    sessionLayout->addWidget(testEnvironmentCombo, 1, 5);
    sessionLayout->addWidget(new QLabel(QStringLiteral("备注"), sessionGroup), 2, 0);
    sessionLayout->addWidget(testSessionRemarkEdit, 2, 1, 1, 5);
    sessionLayout->addWidget(new QLabel(QStringLiteral("会话目录"), sessionGroup), 3, 0);
    sessionLayout->addWidget(testSessionDirectoryValue, 3, 1, 1, 3);

    testNewSessionBtn = new QPushButton(QStringLiteral("新建会话"), sessionGroup);
    testOpenSessionDirBtn = new QPushButton(QStringLiteral("打开目录"), sessionGroup);
    QPushButton *testSessionToggleBtn = new QPushButton(sessionGroup);
    sessionLayout->addWidget(testNewSessionBtn, 3, 4);
    sessionLayout->addWidget(testOpenSessionDirBtn, 3, 5);
    sessionLayout->addWidget(testSessionToggleBtn, 2, 5);
    QList<QWidget *> sessionDetailWidgets;
    const QList<QPair<int, int> > detailLabelPositions = QList<QPair<int, int> >()
        << qMakePair(0, 2) << qMakePair(0, 4) << qMakePair(1, 4)
        << qMakePair(2, 0) << qMakePair(3, 0);
    for (const QPair<int, int> &position : detailLabelPositions) {
        QLayoutItem *item = sessionLayout->itemAtPosition(position.first, position.second);
        if (item != nullptr && item->widget() != nullptr) {
            sessionDetailWidgets.append(item->widget());
        }
    }
    sessionDetailWidgets << testSoftwareVersionEdit
                         << testFirmwareVersionEdit
                         << testEnvironmentCombo
                         << testSessionRemarkEdit
                         << testSessionDirectoryValue;
    auto setSessionCollapsed = [testSessionToggleBtn, sessionDetailWidgets](bool collapsed) {
        for (QWidget *detailWidget : sessionDetailWidgets) {
            if (detailWidget != nullptr) {
                detailWidget->setVisible(!collapsed);
            }
        }
        testSessionToggleBtn->setText(collapsed ? QStringLiteral("展开会话信息") : QStringLiteral("收起会话信息"));
    };
    bool sessionCollapsed = appConfig.load().testSessionCollapsed;
    setSessionCollapsed(sessionCollapsed);
    connect(testSessionToggleBtn, &QPushButton::clicked, this, [this, setSessionCollapsed, testSessionToggleBtn]() {
        const bool collapsed = testSessionToggleBtn->text().contains(QStringLiteral("收起"));
        setSessionCollapsed(collapsed);
        AppConfigData config = appConfig.load();
        config.testSessionCollapsed = collapsed;
        appConfig.save(config);
    });
    mainLayout->addWidget(sessionGroup);

    QGroupBox *filterGroup = new QGroupBox(QStringLiteral("筛选与操作"), widget);
    QVBoxLayout *filterLayout = new QVBoxLayout(filterGroup);
    filterLayout->setContentsMargins(8, 8, 8, 8);
    filterLayout->setSpacing(6);
    testModuleFilterCombo = new QComboBox(filterGroup);
    testPriorityFilterCombo = new QComboBox(filterGroup);
    testResultFilterCombo = new QComboBox(filterGroup);
    testSearchEdit = new QLineEdit(filterGroup);
    testSearchEdit->setPlaceholderText(QStringLiteral("搜索用例ID/关键字"));
    QPushButton *importBtn = new QPushButton(QStringLiteral("导入用例"), filterGroup);
    testExportResultBtn = new QPushButton(QStringLiteral("导出结果"), filterGroup);
    testExportExcelBtn = new QPushButton(QStringLiteral("导出Excel副本"), filterGroup);
    testExportMarkdownBtn = new QPushButton(QStringLiteral("导出Markdown报告"), filterGroup);
    testExportPdfBtn = new QPushButton(QStringLiteral("导出PDF报告"), filterGroup);
    testRunFilteredBtn = new QPushButton(QStringLiteral("执行筛选项"), filterGroup);
    testRetestFailedBtn = new QPushButton(QStringLiteral("复测失败/阻塞"), filterGroup);
    testTemplatePresetCombo = new QComboBox(filterGroup);
    testTemplatePresetCombo->addItems(QStringList()
        << QStringLiteral("默认模板")
        << QStringLiteral("P0冒烟")
        << QStringLiteral("失败复测")
        << QStringLiteral("广播观察"));
    testFailPauseCheck = new QCheckBox(QStringLiteral("失败/阻塞时暂停批量"), filterGroup);
    testFailPauseCheck->setChecked(true);
    testStatsValue = new QLabel(QStringLiteral("总数 0，已执行 0，通过 0，失败 0，阻塞 0，通过率 0.00%"), filterGroup);
    testStatsValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    testModuleFilterCombo->addItem(QStringLiteral("全部"));
    testPriorityFilterCombo->addItems(QStringList() << QStringLiteral("全部") << QStringLiteral("P0") << QStringLiteral("P1") << QStringLiteral("P2"));
    testResultFilterCombo->addItems(QStringList() << QStringLiteral("全部") << QStringLiteral("未执行") << QStringLiteral("通过") << QStringLiteral("失败") << QStringLiteral("阻塞") << QStringLiteral("不适用"));
    filterLayout->addWidget(new QLabel(QStringLiteral("模块"), filterGroup));
    filterLayout->addWidget(testModuleFilterCombo);
    filterLayout->addWidget(new QLabel(QStringLiteral("优先级"), filterGroup));
    filterLayout->addWidget(testPriorityFilterCombo);
    filterLayout->addWidget(new QLabel(QStringLiteral("结果"), filterGroup));
    filterLayout->addWidget(testResultFilterCombo);
    filterLayout->addWidget(testSearchEdit, 1);
    filterLayout->addWidget(testStatsValue);
    filterLayout->addWidget(importBtn);
    filterLayout->addWidget(testExportResultBtn);
    filterLayout->addWidget(testExportExcelBtn);
    filterLayout->addWidget(testExportMarkdownBtn);
    filterLayout->addWidget(testExportPdfBtn);
    filterLayout->addWidget(testRunFilteredBtn);
    filterLayout->addWidget(testRetestFailedBtn);
    filterLayout->addWidget(testTemplatePresetCombo);
    filterLayout->addWidget(testFailPauseCheck);
    while (QLayoutItem *item = filterLayout->takeAt(0)) {
        if (QWidget *widgetItem = item->widget()) {
            QLabel *label = qobject_cast<QLabel *>(widgetItem);
            if (label != nullptr && label != testStatsValue) {
                label->deleteLater();
            }
        }
        delete item;
    }
    QHBoxLayout *filterTopLayout = new QHBoxLayout();
    filterTopLayout->setSpacing(6);
    filterTopLayout->addWidget(new QLabel(QStringLiteral("模块"), filterGroup));
    filterTopLayout->addWidget(testModuleFilterCombo);
    filterTopLayout->addWidget(new QLabel(QStringLiteral("优先级"), filterGroup));
    filterTopLayout->addWidget(testPriorityFilterCombo);
    filterTopLayout->addWidget(new QLabel(QStringLiteral("结果"), filterGroup));
    filterTopLayout->addWidget(testResultFilterCombo);
    filterTopLayout->addWidget(testSearchEdit, 1);
    filterTopLayout->addWidget(testStatsValue);
    filterLayout->addLayout(filterTopLayout);

    QHBoxLayout *filterPresetLayout = new QHBoxLayout();
    filterPresetLayout->setSpacing(6);
    filterPresetLayout->addWidget(new QLabel(QStringLiteral("执行模板"), filterGroup));
    filterPresetLayout->addWidget(testTemplatePresetCombo);
    filterPresetLayout->addWidget(testFailPauseCheck);
    filterPresetLayout->addStretch();
    filterLayout->addLayout(filterPresetLayout);

    QHBoxLayout *filterActionLayout = new QHBoxLayout();
    filterActionLayout->setSpacing(6);
    filterActionLayout->addStretch();
    filterActionLayout->addWidget(testRunFilteredBtn);
    filterActionLayout->addWidget(testRetestFailedBtn);
    filterActionLayout->addWidget(importBtn);
    filterActionLayout->addWidget(testExportResultBtn);
    filterActionLayout->addWidget(testExportExcelBtn);
    filterActionLayout->addWidget(testExportMarkdownBtn);
    filterActionLayout->addWidget(testExportPdfBtn);
    autoCompactLogOnTestExecutionCheck = new QCheckBox(QStringLiteral("测试执行时自动收起CAN日志"), filterGroup);
    filterActionLayout->addWidget(autoCompactLogOnTestExecutionCheck);
    connect(autoCompactLogOnTestExecutionCheck, &QCheckBox::toggled, this, [this]() {
        saveAppConfig();
    });
    filterLayout->addLayout(filterActionLayout);
    mainLayout->addWidget(filterGroup);

    testCaseSplitter = new QSplitter(Qt::Horizontal, widget);
    testCaseSplitter->setChildrenCollapsible(false);
    connect(testCaseSplitter, &QSplitter::splitterMoved, this, [this]() {
        if (layoutPresetCombo != nullptr && layoutPresetCombo->currentIndex() != 3) {
            layoutPresetCombo->blockSignals(true);
            layoutPresetCombo->setCurrentIndex(3);
            layoutPresetCombo->blockSignals(false);
        }
        saveAppConfig();
    });
    testCaseTableView = new QTableView(testCaseSplitter);
    testCaseTableView->setModel(testCaseModel);
    testCaseTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    testCaseTableView->setSelectionMode(QAbstractItemView::SingleSelection);
    testCaseTableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    testCaseTableView->horizontalHeader()->setStretchLastSection(true);
    testCaseTableView->verticalHeader()->setVisible(false);
    testCaseTableView->setAlternatingRowColors(true);

    QWidget *detailPanel = new QWidget(testCaseSplitter);
    QVBoxLayout *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(6, 0, 0, 0);
    testCaseTitleValue = new QLabel(QStringLiteral("请选择用例"), detailPanel);
    testCaseTitleValue->setStyleSheet(QStringLiteral("font-weight: bold;"));
    testCaseDetailText = new QTextEdit(detailPanel);
    testCaseDetailText->setReadOnly(true);
    testCaseDetailText->setMinimumHeight(140);
    testExpectationText = new QTextEdit(detailPanel);
    testExpectationText->setReadOnly(true);
    testExpectationText->setMinimumHeight(180);

    QGridLayout *resultLayout = new QGridLayout();
    testResultCombo = new QComboBox(detailPanel);
    testResultCombo->addItems(QStringList() << QStringLiteral("未执行") << QStringLiteral("通过") << QStringLiteral("失败") << QStringLiteral("阻塞") << QStringLiteral("不适用"));
    testActualResultEdit = new QTextEdit(detailPanel);
    testActualResultEdit->setMinimumHeight(120);
    testDefectIdEdit = new QLineEdit(detailPanel);
    testCaseRemarkEdit = new QTextEdit(detailPanel);
    testCaseRemarkEdit->setMinimumHeight(90);
    testStartCaseBtn = new QPushButton(QStringLiteral("开始执行"), detailPanel);
    testRunAutoBtn = new QPushButton(QStringLiteral("自动执行本用例"), detailPanel);
    testJudgeCaseBtn = new QPushButton(QStringLiteral("自动判定"), detailPanel);
    testSafeRunJudgeBtn = new QPushButton(QStringLiteral("安全执行/判定"), detailPanel);
    testBindStressBtn = new QPushButton(QStringLiteral("绑定压力统计"), detailPanel);
    testSaveResultBtn = new QPushButton(QStringLiteral("保存记录"), detailPanel);
    resultLayout->addWidget(new QLabel(QStringLiteral("执行结果"), detailPanel), 0, 0);
    resultLayout->addWidget(testResultCombo, 0, 1);
    resultLayout->addWidget(new QLabel(QStringLiteral("缺陷编号"), detailPanel), 0, 2);
    resultLayout->addWidget(testDefectIdEdit, 0, 3);
    resultLayout->addWidget(new QLabel(QStringLiteral("实际结果"), detailPanel), 1, 0);
    resultLayout->addWidget(testActualResultEdit, 1, 1, 1, 3);
    resultLayout->addWidget(new QLabel(QStringLiteral("备注"), detailPanel), 2, 0);
    resultLayout->addWidget(testCaseRemarkEdit, 2, 1, 1, 3);
    resultLayout->addWidget(testStartCaseBtn, 3, 0);
    resultLayout->addWidget(testRunAutoBtn, 3, 1);
    resultLayout->addWidget(testJudgeCaseBtn, 3, 2);
    resultLayout->addWidget(testSaveResultBtn, 3, 3);
    resultLayout->addWidget(testSafeRunJudgeBtn, 4, 1);
    resultLayout->addWidget(testBindStressBtn, 4, 2);
    testResultCombo->clear();
    testResultCombo->addItems(QStringList()
        << QStringLiteral("未执行")
        << QStringLiteral("通过")
        << QStringLiteral("失败")
        << QStringLiteral("阻塞")
        << QStringLiteral("不适用"));
    testStartCaseBtn->setText(QStringLiteral("开始执行"));
    testRunAutoBtn->setText(QStringLiteral("自动执行本用例"));
    testJudgeCaseBtn->setText(QStringLiteral("自动判定"));
    testSafeRunJudgeBtn->setText(QStringLiteral("安全执行/判定"));
    testBindStressBtn->setText(QStringLiteral("绑定压力统计"));
    testSaveResultBtn->setText(QStringLiteral("保存记录"));
    if (QLayoutItem *item = resultLayout->itemAtPosition(0, 0)) {
        if (QLabel *label = qobject_cast<QLabel *>(item->widget())) label->setText(QStringLiteral("执行结果"));
    }
    if (QLayoutItem *item = resultLayout->itemAtPosition(0, 2)) {
        if (QLabel *label = qobject_cast<QLabel *>(item->widget())) label->setText(QStringLiteral("缺陷编号"));
    }
    if (QLayoutItem *item = resultLayout->itemAtPosition(1, 0)) {
        if (QLabel *label = qobject_cast<QLabel *>(item->widget())) label->setText(QStringLiteral("实际结果"));
    }
    if (QLayoutItem *item = resultLayout->itemAtPosition(2, 0)) {
        if (QLabel *label = qobject_cast<QLabel *>(item->widget())) label->setText(QStringLiteral("备注"));
    }
    resultLayout->setColumnStretch(1, 1);
    resultLayout->setColumnStretch(3, 1);
    resultLayout->setRowStretch(1, 1);
    resultLayout->setRowStretch(2, 1);
    detailLayout->addWidget(testCaseTitleValue);
    QTabWidget *detailTabs = new QTabWidget(detailPanel);

    QWidget *caseInfoPage = new QWidget(detailTabs);
    QVBoxLayout *caseInfoLayout = new QVBoxLayout(caseInfoPage);
    caseInfoLayout->setContentsMargins(4, 4, 4, 4);
    caseInfoLayout->addWidget(testCaseDetailText);
    detailTabs->addTab(caseInfoPage, QStringLiteral("用例说明"));

    QWidget *expectationPage = new QWidget(detailTabs);
    QVBoxLayout *expectationLayout = new QVBoxLayout(expectationPage);
    expectationLayout->setContentsMargins(4, 4, 4, 4);
    expectationLayout->addWidget(testExpectationText);
    detailTabs->addTab(expectationPage, QStringLiteral("协议期望"));

    QScrollArea *resultScrollArea = new QScrollArea(detailTabs);
    resultScrollArea->setWidgetResizable(true);
    resultScrollArea->setFrameShape(QFrame::NoFrame);
    QWidget *resultPage = new QWidget(resultScrollArea);
    QVBoxLayout *resultPageLayout = new QVBoxLayout(resultPage);
    resultPageLayout->setContentsMargins(4, 4, 4, 4);
    resultPageLayout->addLayout(resultLayout);
    resultPageLayout->addStretch();
    resultScrollArea->setWidget(resultPage);
    detailTabs->addTab(resultScrollArea, QStringLiteral("结果记录"));

    detailLayout->addWidget(detailTabs, 1);
    testCaseSplitter->addWidget(testCaseTableView);
    testCaseSplitter->addWidget(detailPanel);
    testCaseSplitter->setStretchFactor(0, 3);
    testCaseSplitter->setStretchFactor(1, 4);
    testCaseSplitter->setMinimumHeight(280);
    mainLayout->addWidget(testCaseSplitter, 1);

    QTabWidget *bottomTabs = new QTabWidget(widget);

    QGroupBox *evidenceGroup = new QGroupBox(QStringLiteral("当前用例证据日志"), widget);
    QVBoxLayout *evidenceLayout = new QVBoxLayout(evidenceGroup);
    testEvidenceLogText = new QTextEdit(evidenceGroup);
    testEvidenceLogText->setReadOnly(true);
    testEvidenceLogText->setMinimumHeight(120);
    evidenceLayout->addWidget(testEvidenceLogText);
    bottomTabs->addTab(evidenceGroup, QStringLiteral("证据日志"));

    QGroupBox *moduleStatsGroup = new QGroupBox(QStringLiteral("模块/缺陷统计"), widget);
    QVBoxLayout *moduleStatsLayout = new QVBoxLayout(moduleStatsGroup);
    testModuleStatsText = new QTextEdit(moduleStatsGroup);
    testModuleStatsText->setReadOnly(true);
    testModuleStatsText->setMinimumHeight(110);
    moduleStatsLayout->addWidget(testModuleStatsText);
    bottomTabs->addTab(moduleStatsGroup, QStringLiteral("模块/缺陷统计"));

    QGroupBox *progressGroup = new QGroupBox(QStringLiteral("进度看板"), widget);
    QVBoxLayout *progressLayout = new QVBoxLayout(progressGroup);
    testProgressBoardText = new QTextEdit(progressGroup);
    testProgressBoardText->setReadOnly(true);
    testProgressBoardText->setMinimumHeight(110);
    progressLayout->addWidget(testProgressBoardText);
    bottomTabs->addTab(progressGroup, QStringLiteral("进度看板"));

    QGroupBox *retestGroup = new QGroupBox(QStringLiteral("失败复测清单"), widget);
    QVBoxLayout *retestLayout = new QVBoxLayout(retestGroup);
    testRetestListText = new QTextEdit(retestGroup);
    testRetestListText->setReadOnly(true);
    testRetestListText->setMinimumHeight(110);
    retestLayout->addWidget(testRetestListText);
    bottomTabs->addTab(retestGroup, QStringLiteral("复测清单"));
    bottomTabs->setMinimumHeight(150);
    mainLayout->addWidget(bottomTabs);

    QString loadError;
    QFile builtInCases(QStringLiteral(":/resources/testcases/meituan_rfid_can_testcases.json"));
    if (builtInCases.open(QIODevice::ReadOnly)) {
        if (!testCaseService.loadCasesFromJsonData(builtInCases.readAll(), &loadError)) {
            QMessageBox::warning(this, QStringLiteral("测试用例"), loadError);
        }
    } else {
        QMessageBox::warning(this, QStringLiteral("测试用例"), QStringLiteral("无法加载内置测试用例"));
    }
    refreshTestCaseFilters();
    refreshTestCaseModel();

    connect(testNewSessionBtn, &QPushButton::clicked, this, &MainWindow::createTestSession);
    connect(testOpenSessionDirBtn, &QPushButton::clicked, this, &MainWindow::openTestSessionDirectory);
    connect(importBtn, &QPushButton::clicked, this, &MainWindow::importTestCases);
    connect(testExportResultBtn, &QPushButton::clicked, this, &MainWindow::exportTestCaseResults);
    connect(testExportExcelBtn, &QPushButton::clicked, this, &MainWindow::exportTestCaseResultsExcel);
    connect(testExportMarkdownBtn, &QPushButton::clicked, this, &MainWindow::exportTestReportMarkdown);
    connect(testExportPdfBtn, &QPushButton::clicked, this, &MainWindow::exportTestReportPdf);
    connect(testStartCaseBtn, &QPushButton::clicked, this, &MainWindow::startSelectedTestCase);
    connect(testRunAutoBtn, &QPushButton::clicked, this, &MainWindow::runSelectedTestCaseAuto);
    connect(testJudgeCaseBtn, &QPushButton::clicked, this, &MainWindow::judgeSelectedTestCase);
    connect(testSafeRunJudgeBtn, &QPushButton::clicked, this, &MainWindow::safeRunAndJudgeSelectedTestCase);
    connect(testBindStressBtn, &QPushButton::clicked, this, &MainWindow::bindStressStatsToSelectedTestCase);
    connect(testSaveResultBtn, &QPushButton::clicked, this, &MainWindow::saveSelectedTestCaseResult);
    connect(testRunFilteredBtn, &QPushButton::clicked, this, &MainWindow::runFilteredTestCases);
    connect(testRetestFailedBtn, &QPushButton::clicked, this, &MainWindow::retestFailedCases);
    connect(testTemplatePresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::applyTestTemplatePreset);
    connect(testModuleFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::refreshTestCaseModel);
    connect(testPriorityFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::refreshTestCaseModel);
    connect(testResultFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::refreshTestCaseModel);
    connect(testSearchEdit, &QLineEdit::textChanged, this, &MainWindow::refreshTestCaseModel);
    connect(testCaseTableView->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex &, const QModelIndex &) {
        refreshTestCaseDetail();
    });

    if (testCaseModel->rowCount() > 0) {
        testCaseTableView->selectRow(0);
    }
    updateTestExecutionControls();
    scrollArea->setWidget(widget);
    return scrollArea;
}

void MainWindow::refreshTestCaseFilters()
{
    if (testModuleFilterCombo == nullptr) {
        return;
    }

    const QString currentModule = testModuleFilterCombo->currentText();
    testModuleFilterCombo->blockSignals(true);
    testModuleFilterCombo->clear();
    testModuleFilterCombo->addItem(QStringLiteral("全部"));
    testModuleFilterCombo->addItems(testCaseService.modules());
    const int index = testModuleFilterCombo->findText(currentModule);
    testModuleFilterCombo->setCurrentIndex(index >= 0 ? index : 0);
    testModuleFilterCombo->blockSignals(false);
}

void MainWindow::refreshTestCaseModel()
{
    if (testCaseModel == nullptr) {
        return;
    }

    QString selectedCaseId;
    if (testCaseTableView != nullptr && testCaseTableView->currentIndex().isValid()) {
        selectedCaseId = testCaseModel->caseAt(testCaseTableView->currentIndex().row()).id;
    }

    testCaseModel->setCases(testCaseService.cases());
    testCaseModel->setResults(testCaseService.results());
    testCaseModel->setFilters(
        testModuleFilterCombo == nullptr ? QString() : testModuleFilterCombo->currentText(),
        testPriorityFilterCombo == nullptr ? QString() : testPriorityFilterCombo->currentText(),
        testResultFilterCombo == nullptr ? QString() : testResultFilterCombo->currentText(),
        testSearchEdit == nullptr ? QString() : testSearchEdit->text());

    if (testCaseTableView != nullptr) {
        testCaseTableView->resizeColumnsToContents();
        int rowToSelect = -1;
        for (int row = 0; row < testCaseModel->rowCount(); ++row) {
            if (!selectedCaseId.isEmpty() && testCaseModel->caseAt(row).id == selectedCaseId) {
                rowToSelect = row;
                break;
            }
        }
        if (rowToSelect < 0 && testCaseModel->rowCount() > 0) {
            rowToSelect = 0;
        }
        if (rowToSelect >= 0) {
            testCaseTableView->selectRow(rowToSelect);
        }
    }
    refreshTestCaseDetail();
    updateTestExecutionControls();
    updateTestSessionStats();
}

void MainWindow::refreshTestCaseDetail()
{
    if (testCaseTableView == nullptr || testCaseTitleValue == nullptr || testCaseDetailText == nullptr) {
        return;
    }

    const QModelIndex current = testCaseTableView->currentIndex();
    if (!current.isValid()) {
        testCaseTitleValue->setText(QStringLiteral("请选择用例"));
        testCaseDetailText->clear();
        if (testExpectationText != nullptr) {
            testExpectationText->clear();
        }
        return;
    }

    const TestCase testCase = testCaseModel->caseAt(current.row());
    const TestCaseResult result = testCaseService.resultForCase(testCase.id);
    testCaseTitleValue->setText(QStringLiteral("%1  %2  %3").arg(testCase.id, testCase.module, testCase.priority));
    testCaseDetailText->setPlainText(QStringLiteral(
        "测试类型：%1\n执行模式：%2\n命令模板：%3\n判定模板：%4\n超时/重试：%5 ms / %6 次\n依据：%7\n\n前置条件：\n%8\n\n测试数据：\n%9\n\n操作步骤：\n%10\n\n预期结果：\n%11\n\n人工提示：\n%12")
        .arg(testCase.type,
             testCase.executionMode,
             testCase.commandTemplate.isEmpty() ? QStringLiteral("-") : testCase.commandTemplate,
             testCase.judgeTemplate.isEmpty() ? QStringLiteral("-") : testCase.judgeTemplate)
        .arg(testCase.timeoutMs)
        .arg(testCase.retryCount)
        .arg(testCase.basis,
             testCase.precondition,
             testCase.testData,
             testCase.steps,
             testCase.expectedResult,
             testCase.manualPrompt.isEmpty() ? QStringLiteral("-") : testCase.manualPrompt));
    if (testExpectationText != nullptr) {
        testExpectationText->setPlainText(protocolExpectationText(testCase));
    }

    if (testResultCombo != nullptr) {
        const int index = testResultCombo->findText(testResultStatusText(result.status));
        testResultCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (testActualResultEdit != nullptr) {
        testActualResultEdit->setPlainText(result.actualResult);
    }
    if (testDefectIdEdit != nullptr) {
        testDefectIdEdit->setText(result.defectId);
    }
    if (testCaseRemarkEdit != nullptr) {
        testCaseRemarkEdit->setPlainText(result.remark);
    }
}

void MainWindow::updateTestExecutionControls()
{
    const bool hasCase = testCaseTableView != nullptr && testCaseTableView->currentIndex().isValid();
    const bool busy = stressTestService.stats().running || isOtaRunning() || productionTestService.isRunning();
    const bool canStartCase = hasCase && testCaseService.hasSession() && canStarted && !busy;

    if (testStartCaseBtn != nullptr) {
        testStartCaseBtn->setEnabled(canStartCase);
    }
    if (testSaveResultBtn != nullptr) {
        testSaveResultBtn->setEnabled(hasCase && testCaseService.hasSession());
    }
    if (testExportResultBtn != nullptr) {
        testExportResultBtn->setEnabled(!testCaseService.cases().isEmpty());
    }
    if (testExportExcelBtn != nullptr) {
        testExportExcelBtn->setEnabled(!testCaseService.cases().isEmpty());
    }
    if (testExportMarkdownBtn != nullptr) {
        testExportMarkdownBtn->setEnabled(!testCaseService.cases().isEmpty());
    }
    if (testExportPdfBtn != nullptr) {
        testExportPdfBtn->setEnabled(!testCaseService.cases().isEmpty());
    }
    if (testJudgeCaseBtn != nullptr) {
        testJudgeCaseBtn->setEnabled(hasCase && testCaseService.hasSession());
    }
    if (testSafeRunJudgeBtn != nullptr) {
        testSafeRunJudgeBtn->setEnabled(hasCase && testCaseService.hasSession());
    }
    if (testRunAutoBtn != nullptr) {
        testRunAutoBtn->setEnabled(canStartCase);
    }
    if (testRunFilteredBtn != nullptr) {
        testRunFilteredBtn->setEnabled(testCaseService.hasSession() && canStarted && !busy && testCaseModel != nullptr && testCaseModel->rowCount() > 0);
    }
    if (testRetestFailedBtn != nullptr) {
        testRetestFailedBtn->setEnabled(testCaseService.hasSession() && canStarted && !busy);
    }
    if (testBindStressBtn != nullptr) {
        testBindStressBtn->setEnabled(hasCase && testCaseService.hasSession());
    }
    if (testOpenSessionDirBtn != nullptr) {
        testOpenSessionDirBtn->setEnabled(testCaseService.hasSession());
    }
}

void MainWindow::updateTestSessionStats()
{
    if (testStatsValue == nullptr) {
        return;
    }

    int passed = 0;
    int failed = 0;
    int blocked = 0;
    int notApplicable = 0;
    int executed = 0;
    const QMap<QString, TestCaseResult> results = testCaseService.results();
    for (auto it = results.constBegin(); it != results.constEnd(); ++it) {
        switch (it.value().status) {
        case TestResultStatus::Passed:
            ++passed;
            ++executed;
            break;
        case TestResultStatus::Failed:
            ++failed;
            ++executed;
            break;
        case TestResultStatus::Blocked:
            ++blocked;
            ++executed;
            break;
        case TestResultStatus::NotApplicable:
            ++notApplicable;
            ++executed;
            break;
        case TestResultStatus::NotRun:
        default:
            break;
        }
    }

    const int total = testCaseService.cases().size();
    const double passRate = executed == 0 ? 0.0 : (static_cast<double>(passed) * 100.0 / executed);
    testStatsValue->setText(QStringLiteral("总数 %1，已执行 %2，通过 %3，失败 %4，阻塞 %5，不适用 %6，通过率 %7%")
        .arg(total)
        .arg(executed)
        .arg(passed)
        .arg(failed)
        .arg(blocked)
        .arg(notApplicable)
        .arg(passRate, 0, 'f', 2));

    if (testModuleStatsText != nullptr) {
        testModuleStatsText->setPlainText(moduleStatsText());
    }
    refreshProgressBoard();
}

void MainWindow::createTestSession()
{
    if (testTesterEdit == nullptr || testSoftwareVersionEdit == nullptr || testEnvironmentCombo == nullptr) {
        return;
    }

    const QString tester = testTesterEdit->text().trimmed();
    const QString softwareVersion = testSoftwareVersionEdit->text().trimmed();
    const QString environment = testEnvironmentCombo->currentText().trimmed();
    if (tester.isEmpty() || softwareVersion.isEmpty() || environment.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("测试会话"), QStringLiteral("请填写测试人员、软件版本和测试环境。"));
        return;
    }

    QString sn = testDeviceSnEdit == nullptr ? QString() : testDeviceSnEdit->text().trimmed().toUpper();
    sn.replace(QRegularExpression(QStringLiteral("[^A-Z0-9_-]")), QString());
    if (sn.isEmpty()) {
        sn = QStringLiteral("UNKNOWN");
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString sessionId = now.toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString sessionName = QStringLiteral("%1_%2").arg(sessionId, sn);
    const QString sessionDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("test_sessions/%1").arg(sessionName));

    TestSession session;
    session.sessionId = sessionId;
    session.projectName = testProjectEdit == nullptr ? QStringLiteral("美团RFID CAN通信") : testProjectEdit->text().trimmed();
    session.softwareVersion = softwareVersion;
    session.firmwareVersion = testFirmwareVersionEdit == nullptr ? QString() : testFirmwareVersionEdit->text().trimmed();
    session.deviceSn = testDeviceSnEdit == nullptr ? QString() : testDeviceSnEdit->text().trimmed();
    session.tester = tester;
    session.environment = environment;
    session.remark = testSessionRemarkEdit == nullptr ? QString() : testSessionRemarkEdit->toPlainText().trimmed();
    session.createdAt = now;
    session.sessionDirectory = sessionDir;

    QString error;
    if (!testCaseService.createSession(session, &error)) {
        QMessageBox::warning(this, QStringLiteral("测试会话"), error);
        return;
    }
    if (testSessionDirectoryValue != nullptr) {
        testSessionDirectoryValue->setText(sessionDir);
    }
    if (testEvidenceLogText != nullptr) {
        testEvidenceLogText->clear();
        testEvidenceLogText->append(QStringLiteral("会话已创建：%1").arg(sessionDir));
    }
    refreshTestCaseModel();
}

void MainWindow::openTestSessionDirectory()
{
    if (!testCaseService.hasSession()) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(testCaseService.session().sessionDirectory));
}

void MainWindow::importTestCases()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("导入测试用例 JSON"),
        QDir::homePath(),
        QStringLiteral("JSON 文件 (*.json)"));
    if (filePath.isEmpty()) {
        return;
    }

    QString error;
    if (!testCaseService.loadCasesFromJsonFile(filePath, &error)) {
        QMessageBox::warning(this, QStringLiteral("导入用例"), error);
        return;
    }
    refreshTestCaseFilters();
    refreshTestCaseModel();
    QMessageBox::information(this, QStringLiteral("导入用例"), QStringLiteral("已导入 %1 条测试用例。").arg(testCaseService.cases().size()));
}

void MainWindow::startSelectedTestCase()
{
    if (testCaseTableView == nullptr || !testCaseTableView->currentIndex().isValid()) {
        return;
    }
    if (stressTestService.stats().running || isOtaRunning() || productionTestService.isRunning()) {
        QMessageBox::warning(this, QStringLiteral("测试执行"), QStringLiteral("压力测试、OTA 或产线检测运行中，不能开始普通用例执行。"));
        return;
    }
    if (!canStarted) {
        QMessageBox::warning(this, QStringLiteral("测试执行"), QStringLiteral("请先启动 CAN。"));
        return;
    }

    const TestCase testCase = testCaseModel->caseAt(testCaseTableView->currentIndex().row());
    QString error;
    if (!testCaseService.startCase(testCase.id, &error)) {
        QMessageBox::warning(this, QStringLiteral("测试执行"), error);
        return;
    }
    if (testEvidenceLogText != nullptr) {
        testEvidenceLogText->clear();
        testEvidenceLogText->append(QStringLiteral("开始执行用例：%1").arg(testCase.id));
    }
    refreshTestCaseModel();
}

void MainWindow::saveSelectedTestCaseResult()
{
    if (testCaseTableView == nullptr || !testCaseTableView->currentIndex().isValid()) {
        return;
    }
    const TestCase testCase = testCaseModel->caseAt(testCaseTableView->currentIndex().row());
    TestCaseResult result = testCaseService.resultForCase(testCase.id);
    result.caseId = testCase.id;
    result.status = testResultStatusFromText(testResultCombo == nullptr ? QString() : testResultCombo->currentText());
    result.actualResult = testActualResultEdit == nullptr ? QString() : testActualResultEdit->toPlainText().trimmed();
    result.defectId = testDefectIdEdit == nullptr ? QString() : testDefectIdEdit->text().trimmed();
    result.remark = testCaseRemarkEdit == nullptr ? QString() : testCaseRemarkEdit->toPlainText().trimmed();

    QString error;
    if (!testCaseService.saveResult(result, &error)) {
        QMessageBox::warning(this, QStringLiteral("测试执行"), error);
        return;
    }
    testCaseService.finishActiveCase();
    refreshTestCaseModel();
}

void MainWindow::exportTestCaseResults()
{
    const QString defaultPath = testCaseService.hasSession()
        ? QDir(testCaseService.session().sessionDirectory).filePath(QStringLiteral("results_export.csv"))
        : QDir::homePath() + QStringLiteral("/results_export.csv");
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出测试结果"),
        defaultPath,
        QStringLiteral("CSV 文件 (*.csv)"));
    if (filePath.isEmpty()) {
        return;
    }
    QString error;
    if (!testCaseService.exportResultsCsv(filePath, &error)) {
        QMessageBox::warning(this, QStringLiteral("导出测试结果"), error);
        return;
    }
    QMessageBox::information(this, QStringLiteral("导出测试结果"), QStringLiteral("导出完成：%1").arg(filePath));
}

void MainWindow::exportTestCaseResultsExcel()
{
    const QString defaultPath = testCaseService.hasSession()
        ? QDir(testCaseService.session().sessionDirectory).filePath(QStringLiteral("results_export.xls"))
        : QDir::homePath() + QStringLiteral("/results_export.xls");
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出Excel结果副本"),
        defaultPath,
        QStringLiteral("Excel 文件 (*.xls)"));
    if (filePath.isEmpty()) {
        return;
    }
    QString error;
    if (!testCaseService.exportResultsExcelHtml(filePath, &error)) {
        QMessageBox::warning(this, QStringLiteral("导出Excel结果副本"), error);
        return;
    }
    QMessageBox::information(this, QStringLiteral("导出Excel结果副本"), QStringLiteral("导出完成：%1").arg(filePath));
}

QString MainWindow::selectedCaseId() const
{
    if (testCaseTableView == nullptr || !testCaseTableView->currentIndex().isValid()) {
        return QString();
    }
    return testCaseModel->caseAt(testCaseTableView->currentIndex().row()).id;
}

QString MainWindow::moduleStatsText() const
{
    struct Counts {
        int total = 0;
        int passed = 0;
        int failed = 0;
        int blocked = 0;
        int notApplicable = 0;
        int notRun = 0;
    };

    QMap<QString, Counts> stats;
    const QMap<QString, TestCaseResult> results = testCaseService.results();
    for (const TestCase &testCase : testCaseService.cases()) {
        Counts counts = stats.value(testCase.module);
        ++counts.total;
        const TestResultStatus status = results.value(testCase.id).status;
        switch (status) {
        case TestResultStatus::Passed: ++counts.passed; break;
        case TestResultStatus::Failed: ++counts.failed; break;
        case TestResultStatus::Blocked: ++counts.blocked; break;
        case TestResultStatus::NotApplicable: ++counts.notApplicable; break;
        case TestResultStatus::NotRun:
        default: ++counts.notRun; break;
        }
        stats.insert(testCase.module, counts);
    }

    QStringList lines;
    int defectCount = 0;
    for (const TestCaseResult &result : results) {
        if (!result.defectId.trimmed().isEmpty()) {
            ++defectCount;
        }
    }
    lines << QStringLiteral("缺陷关联用例数：%1").arg(defectCount);
    for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) {
        const Counts counts = it.value();
        const int executed = counts.passed + counts.failed + counts.blocked + counts.notApplicable;
        const double passRate = executed == 0 ? 0.0 : static_cast<double>(counts.passed) * 100.0 / executed;
        lines << QStringLiteral("%1：总数%2，已执行%3，通过%4，失败%5，阻塞%6，不适用%7，未执行%8，通过率%9%")
            .arg(it.key())
            .arg(counts.total)
            .arg(executed)
            .arg(counts.passed)
            .arg(counts.failed)
            .arg(counts.blocked)
            .arg(counts.notApplicable)
            .arg(counts.notRun)
            .arg(passRate, 0, 'f', 2);
    }
    return lines.join(QStringLiteral("\n"));
}

void MainWindow::refreshProgressBoard()
{
    const QMap<QString, TestCaseResult> results = testCaseService.results();
    if (testProgressBoardText != nullptr) {
        testProgressBoardText->setPlainText(testSummaryBuilder.progressText(testCaseService.cases(), results));
    }
    if (testRetestListText != nullptr) {
        testRetestListText->setPlainText(testSummaryBuilder.retestListText(testCaseService.cases(), results));
    }
}

QString MainWindow::testReportMarkdown() const
{
    QString report;
    QTextStream stream(&report);
    stream.setCodec("UTF-8");

    stream << "# 美团 RFID CAN 通信软件测试报告\n\n";
    if (testCaseService.hasSession()) {
        const TestSession session = testCaseService.session();
        stream << "## 测试会话\n\n";
        stream << "- 项目名称：" << session.projectName << "\n";
        stream << "- 软件版本：" << session.softwareVersion << "\n";
        stream << "- 固件版本：" << session.firmwareVersion << "\n";
        stream << "- 设备SN：" << session.deviceSn << "\n";
        stream << "- 测试人员：" << session.tester << "\n";
        stream << "- 测试环境：" << session.environment << "\n";
        stream << "- 创建时间：" << session.createdAt.toString("yyyy-MM-dd hh:mm:ss") << "\n";
        stream << "- 会话目录：" << session.sessionDirectory << "\n\n";
        if (!session.remark.trimmed().isEmpty()) {
            stream << "备注：" << session.remark << "\n\n";
        }
    }

    stream << "## 统计概览\n\n";
    stream << moduleStatsText() << "\n\n";
    stream << "## 测试结论\n\n";
    stream << testSummaryBuilder.reportConclusion(testCaseService.cases(), testCaseService.results()) << "\n\n";

    stream << "## 失败/阻塞用例\n\n";
    const QMap<QString, TestCaseResult> results = testCaseService.results();
    stream << testSummaryBuilder.failureSummaryMarkdown(testCaseService.cases(), results);

    stream << "\n## 全量结果\n\n";
    stream << "| 用例ID | 模块 | 优先级 | 类型 | 结果 | 缺陷编号 | 证据日志 |\n";
    stream << "| --- | --- | --- | --- | --- | --- | --- |\n";
    for (const TestCase &testCase : testCaseService.cases()) {
        const TestCaseResult result = results.value(testCase.id);
        stream << "| " << testCase.id << " | " << testCase.module << " | "
               << testCase.priority << " | " << testCase.type << " | "
               << testResultStatusText(result.status) << " | " << result.defectId << " | "
               << result.evidenceLogPath << " |\n";
    }
    return report;
}

QString MainWindow::testReportHtml() const
{
    QString html;
    QTextStream stream(&html);
    stream.setCodec("UTF-8");
    stream << "<html><head><meta charset=\"utf-8\"><style>"
              "body{font-family:SimSun,serif;font-size:10.5pt;}"
              "h1,h2{font-family:SimHei,sans-serif;}"
              "table{border-collapse:collapse;width:100%;}"
              "td,th{border:1px solid #BFBFBF;padding:4px;vertical-align:top;}"
              "th{background:#D9EAF7;color:#1F4E78;}"
              ".fail{background:#FCE4D6}.block{background:#FFF2CC}.pass{background:#E2F0D9}"
              "</style></head><body>";
    stream << "<h1>美团 RFID CAN 通信软件测试报告</h1>";
    if (testCaseService.hasSession()) {
        const TestSession session = testCaseService.session();
        stream << "<h2>测试会话</h2><p>"
               << "项目名称：" << session.projectName.toHtmlEscaped() << "<br>"
               << "软件版本：" << session.softwareVersion.toHtmlEscaped() << "<br>"
               << "固件版本：" << session.firmwareVersion.toHtmlEscaped() << "<br>"
               << "设备SN：" << session.deviceSn.toHtmlEscaped() << "<br>"
               << "测试人员：" << session.tester.toHtmlEscaped() << "<br>"
               << "测试环境：" << session.environment.toHtmlEscaped() << "<br>"
               << "创建时间：" << session.createdAt.toString("yyyy-MM-dd hh:mm:ss").toHtmlEscaped()
               << "</p>";
    }
    stream << "<h2>统计概览</h2><pre>" << moduleStatsText().toHtmlEscaped() << "</pre>";
    stream << "<h2>测试结论</h2><p>" << testSummaryBuilder.reportConclusion(testCaseService.cases(), testCaseService.results()).toHtmlEscaped() << "</p>";
    stream << "<h2>全量结果</h2><table><tr><th>用例ID</th><th>模块</th><th>优先级</th><th>类型</th><th>结果</th><th>缺陷编号</th><th>实际结果</th><th>证据日志</th></tr>";
    const QMap<QString, TestCaseResult> results = testCaseService.results();
    for (const TestCase &testCase : testCaseService.cases()) {
        const TestCaseResult result = results.value(testCase.id);
        QString cssClass;
        if (result.status == TestResultStatus::Passed) cssClass = QStringLiteral("pass");
        if (result.status == TestResultStatus::Failed) cssClass = QStringLiteral("fail");
        if (result.status == TestResultStatus::Blocked) cssClass = QStringLiteral("block");
        stream << "<tr class=\"" << cssClass << "\"><td>" << testCase.id.toHtmlEscaped()
               << "</td><td>" << testCase.module.toHtmlEscaped()
               << "</td><td>" << testCase.priority.toHtmlEscaped()
               << "</td><td>" << testCase.type.toHtmlEscaped()
               << "</td><td>" << testResultStatusText(result.status).toHtmlEscaped()
               << "</td><td>" << result.defectId.toHtmlEscaped()
               << "</td><td>" << result.actualResult.toHtmlEscaped().replace("\n", "<br>")
               << "</td><td>" << result.evidenceLogPath.toHtmlEscaped()
               << "</td></tr>";
    }
    stream << "</table></body></html>";
    return html;
}

void MainWindow::exportTestReportMarkdown()
{
    const QString defaultPath = testCaseService.hasSession()
        ? QDir(testCaseService.session().sessionDirectory).filePath(QStringLiteral("test_report.md"))
        : QDir::homePath() + QStringLiteral("/test_report.md");
    const QString filePath = QFileDialog::getSaveFileName(this, QStringLiteral("导出Markdown测试报告"), defaultPath, QStringLiteral("Markdown 文件 (*.md)"));
    if (filePath.isEmpty()) {
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出测试报告"), QStringLiteral("无法写入文件：%1").arg(filePath));
        return;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << testReportMarkdown();
    QMessageBox::information(this, QStringLiteral("导出测试报告"), QStringLiteral("导出完成：%1").arg(filePath));
}

void MainWindow::exportTestReportPdf()
{
    const QString defaultPath = testCaseService.hasSession()
        ? QDir(testCaseService.session().sessionDirectory).filePath(QStringLiteral("test_report.pdf"))
        : QDir::homePath() + QStringLiteral("/test_report.pdf");
    const QString filePath = QFileDialog::getSaveFileName(this, QStringLiteral("导出PDF测试报告"), defaultPath, QStringLiteral("PDF 文件 (*.pdf)"));
    if (filePath.isEmpty()) {
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filePath);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(12, 12, 12, 12));

    QTextDocument document;
    document.setHtml(testReportHtml());
    document.print(&printer);
    QMessageBox::information(this, QStringLiteral("导出测试报告"), QStringLiteral("导出完成：%1").arg(filePath));
}

QString MainWindow::protocolExpectationText(const TestCase &testCase) const
{
    return testCaseJudge.expectationText(testCase);
}

QString MainWindow::judgeTestCaseEvidence(const TestCase &testCase, TestResultStatus *status) const
{
    const TestJudgeResult result = testCaseJudge.judge(testCase, readEvidenceText(testCase.id));
    if (status != nullptr) {
        *status = result.status;
    }
    return result.reason;
}

QString MainWindow::readEvidenceText(const QString &caseId) const
{
    const QString evidencePath = testCaseService.evidencePathForCase(caseId);
    QFile file(evidencePath);
    if (evidencePath.isEmpty() || !file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

bool MainWindow::precheckTestExecution(const TestCase &testCase, QString *reason) const
{
    if (!testCaseService.hasSession()) {
        if (reason) *reason = QStringLiteral("请先新建测试会话。");
        return false;
    }
    if (!canStarted) {
        if (reason) *reason = QStringLiteral("请先启动 CAN。");
        return false;
    }
    if (stressTestService.stats().running || isOtaRunning() || productionTestService.isRunning()) {
        if (reason) *reason = QStringLiteral("压力测试、OTA 或产线检测运行中，不能执行测试用例。");
        return false;
    }
    if (testCase.id.isEmpty()) {
        if (reason) *reason = QStringLiteral("未选择有效用例。");
        return false;
    }
    return true;
}

bool MainWindow::sendAutoTestCommand(const TestCase &testCase, QString *message)
{
    if (testCase.commandTemplate == QStringLiteral("mt.sid_0x01_set_scan_period")) {
        sendRfidFrame(RfidProtocol::RequestFrameId, RfidProtocol::buildSetScanPeriodFrame(0x28));
        if (message) *message = QStringLiteral("已发送扫描周期配置 400ms：02 01 28 55 55 55 55 55");
        return true;
    }
    if (testCase.commandTemplate == QStringLiteral("mt.sid_0x02_reboot")) {
        sendRfidFrame(RfidProtocol::RequestFrameId, RfidProtocol::buildRestartFrame());
        if (message) *message = QStringLiteral("已发送重启指令：01 02 55 55 55 55 55 55");
        return true;
    }
    if (testCase.commandTemplate == QStringLiteral("mt.sid_0x29_period_config")) {
        sendRfidFrame(RfidProtocol::RequestFrameId, RfidProtocol::buildSetBroadcastPeriodFrame(RfidProtocol::StatusFrameId, 1000));
        if (message) *message = QStringLiteral("已发送 0x29 周期配置：目标 0x2C0，周期 1000ms。");
        return true;
    }

    if (message) {
        *message = testCase.manualPrompt.isEmpty()
            ? QStringLiteral("该用例未配置可安全自动发送的命令，请手动执行步骤后使用自动判定。")
            : testCase.manualPrompt;
    }
    return false;
}

void MainWindow::runSelectedTestCaseAuto()
{
    if (testCaseTableView == nullptr || !testCaseTableView->currentIndex().isValid()) {
        return;
    }

    const TestCase testCase = testCaseModel->caseAt(testCaseTableView->currentIndex().row());
    QString reason;
    if (!precheckTestExecution(testCase, &reason)) {
        QMessageBox::warning(this, QStringLiteral("自动执行"), reason);
        return;
    }

    QString error;
    if (!testCaseService.startCase(testCase.id, &error)) {
        QMessageBox::warning(this, QStringLiteral("自动执行"), error);
        return;
    }

    QString commandMessage;
    const bool commandSent = sendAutoTestCommand(testCase, &commandMessage);
    if (testEvidenceLogText != nullptr) {
        testEvidenceLogText->append(QStringLiteral("[自动执行] %1").arg(commandMessage));
    }
    if (!commandSent && testCase.executionMode == QStringLiteral("manual")) {
        QMessageBox::information(this, QStringLiteral("自动执行"), commandMessage);
    }

    QEventLoop waitLoop;
    QTimer::singleShot(qMax(300, testCase.timeoutMs), &waitLoop, &QEventLoop::quit);
    waitLoop.exec();

    const TestJudgeResult judgeResult = testCaseJudge.judge(testCase, readEvidenceText(testCase.id));
    TestCaseResult result = testCaseService.resultForCase(testCase.id);
    result.caseId = testCase.id;
    result.status = judgeResult.status;
    result.judgeReason = judgeResult.reason;
    result.failureCategory = judgeResult.failureCategory;
    result.keyFrames = judgeResult.keyFrames;
    QString actual = QStringLiteral("[自动执行] %1\n[自动判定] %2").arg(commandMessage, judgeResult.reason);
    if (!judgeResult.keyFrames.isEmpty()) {
        actual.append(QStringLiteral("\n[关键帧]\n%1").arg(judgeResult.keyFrames.join(QStringLiteral("\n"))));
    }
    result.actualResult = actual.trimmed();
    if (!testCaseService.saveResult(result, &error)) {
        QMessageBox::warning(this, QStringLiteral("自动执行"), error);
        return;
    }
    testCaseService.finishActiveCase();
    refreshTestCaseModel();
}

void MainWindow::runFilteredTestCases()
{
    if (testCaseModel == nullptr || testCaseModel->rowCount() == 0) {
        return;
    }
    QVector<TestCase> casesToRun;
    for (int row = 0; row < testCaseModel->rowCount(); ++row) {
        casesToRun.append(testCaseModel->caseAt(row));
    }
    QString reason;
    if (!precheckTestExecution(casesToRun.first(), &reason)) {
        QMessageBox::warning(this, QStringLiteral("批量执行"), reason);
        return;
    }

    const int count = casesToRun.size();
    if (QMessageBox::question(this,
            QStringLiteral("批量执行确认"),
            QStringLiteral("将按当前筛选条件执行 %1 条用例。\n自动用例会直接发送安全命令；半自动/手工用例只记录阻塞提示，不会执行持久化写入。")
                .arg(count),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    int passed = 0;
    int failedOrBlocked = 0;
    for (const TestCase &testCase : casesToRun) {
        int rowToSelect = -1;
        for (int row = 0; row < testCaseModel->rowCount(); ++row) {
            if (testCaseModel->caseAt(row).id == testCase.id) {
                rowToSelect = row;
                break;
            }
        }
        if (rowToSelect < 0) {
            testCaseService.finishActiveCase();
            refreshTestCaseModel();
            for (int row = 0; row < testCaseModel->rowCount(); ++row) {
                if (testCaseModel->caseAt(row).id == testCase.id) {
                    rowToSelect = row;
                    break;
                }
            }
        }
        if (rowToSelect < 0) {
            continue;
        }
        testCaseTableView->selectRow(rowToSelect);
        if (testCase.executionMode != QStringLiteral("auto")) {
            TestCaseResult result = testCaseService.resultForCase(testCase.id);
            result.caseId = testCase.id;
            result.status = TestResultStatus::Blocked;
            result.failureCategory = QStringLiteral("manual_required");
            result.judgeReason = testCase.manualPrompt.isEmpty()
                ? QStringLiteral("半自动/手工用例未执行，请按步骤操作后再自动判定。")
                : testCase.manualPrompt;
            result.actualResult = result.judgeReason;
            QString error;
            testCaseService.saveResult(result, &error);
            ++failedOrBlocked;
            if (testFailPauseCheck != nullptr && testFailPauseCheck->isChecked()) {
                break;
            }
            continue;
        }

        runSelectedTestCaseAuto();
        const TestCaseResult result = testCaseService.resultForCase(testCase.id);
        if (result.status == TestResultStatus::Passed) {
            ++passed;
        } else if (result.status == TestResultStatus::Failed || result.status == TestResultStatus::Blocked) {
            ++failedOrBlocked;
            if (testFailPauseCheck != nullptr && testFailPauseCheck->isChecked()) {
                break;
            }
        }
        QApplication::processEvents();
    }
    refreshTestCaseModel();
    QMessageBox::information(this, QStringLiteral("批量执行"), QStringLiteral("批量执行结束：通过 %1，失败/阻塞 %2。").arg(passed).arg(failedOrBlocked));
}

void MainWindow::retestFailedCases()
{
    if (testCaseModel == nullptr) {
        return;
    }
    const QMap<QString, TestCaseResult> results = testCaseService.results();
    QVector<TestCase> failedCases;
    for (const TestCase &testCase : testCaseService.cases()) {
        const TestCaseResult result = results.value(testCase.id);
        if (result.status == TestResultStatus::Failed || result.status == TestResultStatus::Blocked) {
            failedCases.append(testCase);
        }
    }
    if (failedCases.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("复测"), QStringLiteral("暂无失败/阻塞用例。"));
        return;
    }
    if (QMessageBox::question(this,
            QStringLiteral("复测确认"),
            QStringLiteral("将复测 %1 条失败/阻塞用例。自动用例会发送安全命令，其他用例保留人工提示。").arg(failedCases.size()),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    if (testResultFilterCombo != nullptr) {
        testResultFilterCombo->setCurrentText(QStringLiteral("全部"));
    }
    int handled = 0;
    for (const TestCase &testCase : failedCases) {
        refreshTestCaseModel();
        int rowToSelect = -1;
        for (int row = 0; row < testCaseModel->rowCount(); ++row) {
            if (testCaseModel->caseAt(row).id == testCase.id) {
                rowToSelect = row;
                break;
            }
        }
        if (rowToSelect < 0) {
            continue;
        }
        testCaseTableView->selectRow(rowToSelect);
        if (testCase.executionMode == QStringLiteral("auto")) {
            runSelectedTestCaseAuto();
        } else {
            TestCaseResult result = testCaseService.resultForCase(testCase.id);
            result.caseId = testCase.id;
            result.previousStatus = testResultStatusText(result.status);
            result.previousFailureReason = result.judgeReason.isEmpty() ? result.actualResult : result.judgeReason;
            result.retestCount += 1;
            result.lastRetestAt = QDateTime::currentDateTime();
            result.status = TestResultStatus::Blocked;
            result.failureCategory = QStringLiteral("manual_required");
            result.judgeReason = testCase.manualPrompt.isEmpty()
                ? QStringLiteral("该用例需要人工复测后保存结果。")
                : testCase.manualPrompt;
            result.actualResult = result.judgeReason;
            QString error;
            testCaseService.saveResult(result, &error);
        }
        ++handled;
        QApplication::processEvents();
    }
    refreshTestCaseModel();
    QMessageBox::information(this, QStringLiteral("复测"), QStringLiteral("复测处理完成：%1 条。").arg(handled));
}

void MainWindow::applyTestTemplatePreset(int index)
{
    if (testPriorityFilterCombo == nullptr || testResultFilterCombo == nullptr || testSearchEdit == nullptr) {
        return;
    }
    if (index == 1) {
        testPriorityFilterCombo->setCurrentText(QStringLiteral("P0"));
        testResultFilterCombo->setCurrentText(QStringLiteral("全部"));
        testSearchEdit->clear();
    } else if (index == 2) {
        testPriorityFilterCombo->setCurrentText(QStringLiteral("全部"));
        testResultFilterCombo->setCurrentText(QStringLiteral("失败"));
        testSearchEdit->clear();
    } else if (index == 3) {
        testPriorityFilterCombo->setCurrentText(QStringLiteral("全部"));
        testResultFilterCombo->setCurrentText(QStringLiteral("全部"));
        testSearchEdit->setText(QStringLiteral("广播"));
    }
    refreshTestCaseModel();
}

void MainWindow::judgeSelectedTestCase()
{
    if (testCaseTableView == nullptr || !testCaseTableView->currentIndex().isValid()) {
        return;
    }
    const TestCase testCase = testCaseModel->caseAt(testCaseTableView->currentIndex().row());
    const TestJudgeResult judgeResult = testCaseJudge.judge(testCase, readEvidenceText(testCase.id));
    const QString message = judgeResult.reason;
    const TestResultStatus status = judgeResult.status;

    if (testResultCombo != nullptr) {
        const int index = testResultCombo->findText(testResultStatusText(status));
        testResultCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    if (testActualResultEdit != nullptr) {
        QString current = testActualResultEdit->toPlainText().trimmed();
        if (!current.isEmpty()) {
            current.append(QStringLiteral("\n"));
        }
        current.append(QStringLiteral("[自动判定] %1").arg(message));
        if (!judgeResult.keyFrames.isEmpty()) {
            current.append(QStringLiteral("\n[关键帧]\n%1").arg(judgeResult.keyFrames.join(QStringLiteral("\n"))));
        }
        testActualResultEdit->setPlainText(current);
    }
    TestCaseResult result = testCaseService.resultForCase(testCase.id);
    result.caseId = testCase.id;
    result.status = status;
    result.judgeReason = judgeResult.reason;
    result.failureCategory = judgeResult.failureCategory;
    result.keyFrames = judgeResult.keyFrames;
    if (testActualResultEdit != nullptr) {
        result.actualResult = testActualResultEdit->toPlainText().trimmed();
    }
    QString error;
    testCaseService.saveResult(result, &error);
    refreshTestCaseModel();
}

void MainWindow::safeRunAndJudgeSelectedTestCase()
{
    const QString caseId = selectedCaseId();
    if (caseId.isEmpty()) {
        return;
    }
    if (!testCaseService.hasSession()) {
        QMessageBox::warning(this, QStringLiteral("测试执行"), QStringLiteral("请先新建测试会话。"));
        return;
    }

    if (!testCaseService.hasActiveCase() || testCaseService.activeCaseId() != caseId) {
        QString error;
        if (!testCaseService.startCase(caseId, &error)) {
            QMessageBox::warning(this, QStringLiteral("测试执行"), error);
            return;
        }
        if (testEvidenceLogText != nullptr) {
            testEvidenceLogText->append(QStringLiteral("安全执行：已启动当前用例证据记录，不自动发送任何指令。"));
        }
    }
    judgeSelectedTestCase();
    refreshTestCaseModel();
}

void MainWindow::bindStressStatsToSelectedTestCase()
{
    if (testCaseTableView == nullptr || !testCaseTableView->currentIndex().isValid()) {
        return;
    }
    if (!testCaseService.hasSession()) {
        QMessageBox::warning(this, QStringLiteral("测试执行"), QStringLiteral("请先新建测试会话。"));
        return;
    }

    const TestCase testCase = testCaseModel->caseAt(testCaseTableView->currentIndex().row());
    const StressTestStats stats = stressTestService.stats();
    QString summary = QStringLiteral(
        "[压力/长稳统计]\n"
        "运行状态：%1\n"
        "运行时长：%2 s\n"
        "总样本：%3\n"
        "成功：%4\n"
        "无TAG：%5\n"
        "模块故障：%6\n"
        "通信故障：%7\n"
        "成功率：%8%\n"
        "当前TAG：%9\n"
        "最后失败原因：%10")
        .arg(stats.running ? QStringLiteral("运行中") : QStringLiteral("已停止"))
        .arg(stats.elapsedSeconds)
        .arg(stats.totalSamples)
        .arg(stats.successCount)
        .arg(stats.noTagCount)
        .arg(stats.moduleFaultCount)
        .arg(stats.communicationFaultCount)
        .arg(stats.successRate, 0, 'f', 2)
        .arg(stats.currentTag)
        .arg(stats.lastFailureReason);

    TestCaseResult result = testCaseService.resultForCase(testCase.id);
    if (!result.actualResult.trimmed().isEmpty()) {
        result.actualResult.append(QStringLiteral("\n"));
    }
    result.actualResult.append(summary);
    if (stats.totalSamples > 0 && result.status == TestResultStatus::NotRun) {
        result.status = stats.successRate >= 95.0 ? TestResultStatus::Passed : TestResultStatus::Failed;
    }

    QString error;
    if (!testCaseService.saveResult(result, &error)) {
        QMessageBox::warning(this, QStringLiteral("测试执行"), error);
        return;
    }
    refreshTestCaseModel();
}

void MainWindow::appendTestEvidenceFrame(const CanFrame &frame, const QString &decodedText)
{
    if (!testCaseService.hasActiveCase()) {
        return;
    }
    QString error;
    const QString direction = frame.direction == CanFrameDirection::Tx ? QStringLiteral("发送") : QStringLiteral("接收");
    testCaseService.appendEvidence(
        direction,
        QString::number(frame.channel),
        frame.idText(),
        frame.remoteFrame ? QString() : frame.dataText(),
        decodedText,
        &error);

    if (testEvidenceLogText != nullptr) {
        testEvidenceLogText->append(QStringLiteral("%1  %2  %3  %4  %5")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz"),
                 direction,
                 frame.idText(),
                 frame.remoteFrame ? QStringLiteral("-") : frame.dataText(),
                 decodedText));
        if (testEvidenceLogText->document()->blockCount() > 1000) {
            testEvidenceLogText->clear();
            testEvidenceLogText->append(QStringLiteral("证据日志显示已超过 1000 行，完整内容请查看会话目录日志文件。"));
        }
    }
}

QWidget *MainWindow::createStressTestTab(QWidget *parent)
{
    QScrollArea *scrollArea = new QScrollArea(parent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *stressWidget = new QWidget(scrollArea);
    stressWidget->setMinimumWidth(900);
    QGridLayout *layout = new QGridLayout(stressWidget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(6);

    stressStartBtn = new QPushButton(QStringLiteral("开始"), stressWidget);
    stressStopBtn = new QPushButton(QStringLiteral("停止"), stressWidget);
    stressResetBtn = new QPushButton(QStringLiteral("清零"), stressWidget);
    stressAutoSaveCheckBox = new QCheckBox(QStringLiteral("自动保存CSV"), stressWidget);
    stressAutoExportSummaryCheckBox = new QCheckBox(QStringLiteral("完成后导出摘要"), stressWidget);
    stressDurationSecondsSpin = new QSpinBox(stressWidget);
    stressTargetSamplesSpin = new QSpinBox(stressWidget);
    stressExportBtn = new QPushButton(QStringLiteral("导出摘要"), stressWidget);
    stressDurationSecondsSpin->setRange(0, 24 * 60 * 60);
    stressDurationSecondsSpin->setSuffix(QStringLiteral(" 秒"));
    stressDurationSecondsSpin->setSpecialValueText(QStringLiteral("0秒=不限"));
    stressTargetSamplesSpin->setRange(0, 100000000);
    stressTargetSamplesSpin->setSuffix(QStringLiteral(" 次"));
    stressTargetSamplesSpin->setSpecialValueText(QStringLiteral("0次=不限"));
    stressDurationSecondsSpin->setMinimumWidth(105);
    stressDurationSecondsSpin->setMaximumWidth(130);
    stressTargetSamplesSpin->setMinimumWidth(105);
    stressTargetSamplesSpin->setMaximumWidth(130);

    stressStateValue = new QLabel("-", stressWidget);
    stressElapsedValue = new QLabel("-", stressWidget);
    stressTotalSamplesValue = new QLabel("-", stressWidget);
    stressSuccessCountValue = new QLabel("-", stressWidget);
    stressNoTagCountValue = new QLabel("-", stressWidget);
    stressTagLengthErrorValue = new QLabel("-", stressWidget);
    stressFaultCountValue = new QLabel("-", stressWidget);
    stressSuccessRateValue = new QLabel("-", stressWidget);
    stressTagValidRateValue = new QLabel("-", stressWidget);
    stressCurrentTagValue = new QLabel("-", stressWidget);
    stressLastSuccessTagValue = new QLabel("-", stressWidget);
    stressTagChangeCountValue = new QLabel("-", stressWidget);
    stressUniqueTagCountValue = new QLabel("-", stressWidget);
    stressMaxContinuousFailureValue = new QLabel("-", stressWidget);
    stressLastFailureReasonValue = new QLabel("-", stressWidget);

    stressCurrentTagValue->setWordWrap(true);
    stressLastSuccessTagValue->setWordWrap(true);
    stressLastFailureReasonValue->setWordWrap(true);

    qjStressStateValue = new QLabel("-", stressWidget);
    qjStressElapsedValue = new QLabel("-", stressWidget);
    qjStressTotalSamplesValue = new QLabel("-", stressWidget);
    qjStressSuccessCountValue = new QLabel("-", stressWidget);
    qjStressSuccessRateValue = new QLabel("-", stressWidget);
    qjStressCurrentUidValue = new QLabel("-", stressWidget);
    qjStressLastSuccessUidValue = new QLabel("-", stressWidget);
    qjStressUniqueUidCountValue = new QLabel("-", stressWidget);
    qjStressNoTagCountValue = new QLabel("-", stressWidget);
    qjStressUidReadErrorValue = new QLabel("-", stressWidget);
    qjStressModuleFaultValue = new QLabel("-", stressWidget);
    qjStressCommunicationFaultValue = new QLabel("-", stressWidget);
    qjStressContentErrorValue = new QLabel("-", stressWidget);
    qjStressMaxContinuousFailureValue = new QLabel("-", stressWidget);
    qjStressLastFailureReasonValue = new QLabel("-", stressWidget);
    qjStressCurrentUidValue->setWordWrap(true);
    qjStressLastSuccessUidValue->setWordWrap(true);
    qjStressLastFailureReasonValue->setWordWrap(true);

    QGroupBox *controlGroup = new QGroupBox(QStringLiteral("控制"), stressWidget);
    QGridLayout *controlLayout = new QGridLayout(controlGroup);
    controlLayout->setContentsMargins(8, 8, 8, 8);
    controlLayout->setHorizontalSpacing(6);
    controlLayout->addWidget(stressStartBtn, 0, 0);
    controlLayout->addWidget(stressStopBtn, 0, 1);
    controlLayout->addWidget(stressResetBtn, 0, 2);
    controlLayout->addWidget(new QLabel(QStringLiteral("时长目标"), controlGroup), 1, 0);
    controlLayout->addWidget(stressDurationSecondsSpin, 1, 1);
    controlLayout->addWidget(new QLabel(QStringLiteral("次数目标"), controlGroup), 1, 2);
    controlLayout->addWidget(stressTargetSamplesSpin, 1, 3);
    controlLayout->addWidget(stressAutoSaveCheckBox, 2, 0, 1, 2);
    controlLayout->addWidget(stressAutoExportSummaryCheckBox, 2, 2, 1, 2);
    controlLayout->addWidget(stressExportBtn, 0, 3);

    stressProgressBar = new QProgressBar(controlGroup);
    stressProgressBar->setRange(0, 100);
    stressProgressBar->setValue(0);
    stressProgressBar->setTextVisible(true);
    stressProgressBar->setMinimumHeight(18);
    stressProgressBar->setMaximumHeight(24);

    stressRemainingLabel = new QLabel(QStringLiteral("剩余目标：不限"), controlGroup);
    stressRemainingLabel->setStyleSheet("color: #555555;");

    controlLayout->addWidget(stressRemainingLabel, 3, 0, 1, 1);
    controlLayout->addWidget(stressProgressBar, 3, 1, 1, 3);
    controlLayout->setColumnStretch(0, 0);
    controlLayout->setColumnStretch(1, 1);
    controlLayout->setColumnStretch(2, 0);
    controlLayout->setColumnStretch(3, 1);

    QGroupBox *summaryGroup = new QGroupBox(QStringLiteral("核心统计"), stressWidget);
    QGridLayout *summaryLayout = new QGridLayout(summaryGroup);
    summaryLayout->setContentsMargins(8, 8, 8, 8);
    summaryLayout->addWidget(new QLabel(QStringLiteral("状态"), summaryGroup), 0, 0);
    summaryLayout->addWidget(stressStateValue, 0, 1);
    summaryLayout->addWidget(new QLabel(QStringLiteral("时长"), summaryGroup), 0, 2);
    summaryLayout->addWidget(stressElapsedValue, 0, 3);
    summaryLayout->addWidget(new QLabel(QStringLiteral("成功率"), summaryGroup), 1, 0);
    summaryLayout->addWidget(stressSuccessRateValue, 1, 1);
    summaryLayout->addWidget(new QLabel(QStringLiteral("TAG有效率"), summaryGroup), 1, 2);
    summaryLayout->addWidget(stressTagValidRateValue, 1, 3);
    summaryLayout->addWidget(new QLabel(QStringLiteral("总数"), summaryGroup), 2, 0);
    summaryLayout->addWidget(stressTotalSamplesValue, 2, 1);
    summaryLayout->addWidget(new QLabel(QStringLiteral("成功"), summaryGroup), 2, 2);
    summaryLayout->addWidget(stressSuccessCountValue, 2, 3);
    summaryLayout->addWidget(new QLabel(QStringLiteral("未识别"), summaryGroup), 3, 0);
    summaryLayout->addWidget(stressNoTagCountValue, 3, 1);
    summaryLayout->addWidget(new QLabel(QStringLiteral("长度异常"), summaryGroup), 3, 2);
    summaryLayout->addWidget(stressTagLengthErrorValue, 3, 3);

    QGroupBox *tagGroup = new QGroupBox(QStringLiteral("TAG信息"), stressWidget);
    QGridLayout *tagLayout = new QGridLayout(tagGroup);
    tagLayout->setContentsMargins(8, 8, 8, 8);
    tagLayout->addWidget(new QLabel(QStringLiteral("当前TAG"), tagGroup), 0, 0);
    tagLayout->addWidget(stressCurrentTagValue, 0, 1, 1, 3);
    tagLayout->addWidget(new QLabel(QStringLiteral("最近成功"), tagGroup), 1, 0);
    tagLayout->addWidget(stressLastSuccessTagValue, 1, 1, 1, 3);
    tagLayout->addWidget(new QLabel(QStringLiteral("变化"), tagGroup), 2, 0);
    tagLayout->addWidget(stressTagChangeCountValue, 2, 1);
    tagLayout->addWidget(new QLabel(QStringLiteral("不同TAG"), tagGroup), 2, 2);
    tagLayout->addWidget(stressUniqueTagCountValue, 2, 3);

    QGroupBox *faultGroup = new QGroupBox(QStringLiteral("异常信息"), stressWidget);
    QGridLayout *faultLayout = new QGridLayout(faultGroup);
    faultLayout->setContentsMargins(8, 8, 8, 8);
    faultLayout->addWidget(new QLabel(QStringLiteral("模块/通讯"), faultGroup), 0, 0);
    faultLayout->addWidget(stressFaultCountValue, 0, 1, 1, 3);
    faultLayout->addWidget(new QLabel(QStringLiteral("最大连续失败"), faultGroup), 1, 0);
    faultLayout->addWidget(stressMaxContinuousFailureValue, 1, 1, 1, 3);
    faultLayout->addWidget(new QLabel(QStringLiteral("失败原因"), faultGroup), 2, 0);
    faultLayout->addWidget(stressLastFailureReasonValue, 2, 1, 1, 3);

    mtStressPanel = new QWidget(stressWidget);
    QGridLayout *mtLayout = new QGridLayout(mtStressPanel);
    mtLayout->setContentsMargins(0, 0, 0, 0);
    mtLayout->setHorizontalSpacing(6);
    mtLayout->setVerticalSpacing(6);
    mtLayout->addWidget(summaryGroup, 0, 0);
    mtLayout->addWidget(tagGroup, 0, 1);
    mtLayout->addWidget(faultGroup, 1, 0, 1, 2);
    mtLayout->setColumnStretch(0, 1);
    mtLayout->setColumnStretch(1, 1);

    qjStressPanel = new QWidget(stressWidget);
    QGridLayout *qjLayout = new QGridLayout(qjStressPanel);
    qjLayout->setContentsMargins(0, 0, 0, 0);
    qjLayout->setHorizontalSpacing(6);
    qjLayout->setVerticalSpacing(6);

    QGroupBox *qjSummaryGroup = new QGroupBox(QStringLiteral("青桔读卡统计"), qjStressPanel);
    QGridLayout *qjSummaryLayout = new QGridLayout(qjSummaryGroup);
    qjSummaryLayout->setContentsMargins(8, 8, 8, 8);
    qjSummaryLayout->addWidget(new QLabel(QStringLiteral("状态"), qjSummaryGroup), 0, 0);
    qjSummaryLayout->addWidget(qjStressStateValue, 0, 1);
    qjSummaryLayout->addWidget(new QLabel(QStringLiteral("时长"), qjSummaryGroup), 0, 2);
    qjSummaryLayout->addWidget(qjStressElapsedValue, 0, 3);
    qjSummaryLayout->addWidget(new QLabel(QStringLiteral("成功率"), qjSummaryGroup), 1, 0);
    qjSummaryLayout->addWidget(qjStressSuccessRateValue, 1, 1);
    qjSummaryLayout->addWidget(new QLabel(QStringLiteral("总轮询次数"), qjSummaryGroup), 2, 0);
    qjSummaryLayout->addWidget(qjStressTotalSamplesValue, 2, 1);
    qjSummaryLayout->addWidget(new QLabel(QStringLiteral("读卡成功"), qjSummaryGroup), 2, 2);
    qjSummaryLayout->addWidget(qjStressSuccessCountValue, 2, 3);

    QGroupBox *qjUidGroup = new QGroupBox(QStringLiteral("当前 UID"), qjStressPanel);
    QGridLayout *qjUidLayout = new QGridLayout(qjUidGroup);
    qjUidLayout->setContentsMargins(8, 8, 8, 8);
    qjUidLayout->addWidget(new QLabel(QStringLiteral("当前 UID"), qjUidGroup), 0, 0);
    qjUidLayout->addWidget(qjStressCurrentUidValue, 0, 1, 1, 3);
    qjUidLayout->addWidget(new QLabel(QStringLiteral("最后成功 UID"), qjUidGroup), 1, 0);
    qjUidLayout->addWidget(qjStressLastSuccessUidValue, 1, 1, 1, 3);
    qjUidLayout->addWidget(new QLabel(QStringLiteral("唯一 UID 数"), qjUidGroup), 2, 0);
    qjUidLayout->addWidget(qjStressUniqueUidCountValue, 2, 1);

    QGroupBox *qjResultGroup = new QGroupBox(QStringLiteral("青桔结果分类"), qjStressPanel);
    QGridLayout *qjResultLayout = new QGridLayout(qjResultGroup);
    qjResultLayout->setContentsMargins(8, 8, 8, 8);
    qjResultLayout->addWidget(new QLabel(QStringLiteral("无标签"), qjResultGroup), 0, 0);
    qjResultLayout->addWidget(qjStressNoTagCountValue, 0, 1);
    qjResultLayout->addWidget(new QLabel(QStringLiteral("UID读取失败"), qjResultGroup), 0, 2);
    qjResultLayout->addWidget(qjStressUidReadErrorValue, 0, 3);
    qjResultLayout->addWidget(new QLabel(QStringLiteral("密钥/模块错误"), qjResultGroup), 1, 0);
    qjResultLayout->addWidget(qjStressModuleFaultValue, 1, 1);
    qjResultLayout->addWidget(new QLabel(QStringLiteral("通信故障"), qjResultGroup), 1, 2);
    qjResultLayout->addWidget(qjStressCommunicationFaultValue, 1, 3);
    qjResultLayout->addWidget(new QLabel(QStringLiteral("内容异常"), qjResultGroup), 2, 0);
    qjResultLayout->addWidget(qjStressContentErrorValue, 2, 1);
    qjResultLayout->addWidget(new QLabel(QStringLiteral("最大连续失败"), qjResultGroup), 2, 2);
    qjResultLayout->addWidget(qjStressMaxContinuousFailureValue, 2, 3);
    qjResultLayout->addWidget(new QLabel(QStringLiteral("失败原因"), qjResultGroup), 3, 0);
    qjResultLayout->addWidget(qjStressLastFailureReasonValue, 3, 1, 1, 3);

    qjLayout->addWidget(qjSummaryGroup, 0, 0);
    qjLayout->addWidget(qjUidGroup, 0, 1);
    qjLayout->addWidget(qjResultGroup, 1, 0, 1, 2);
    qjLayout->setColumnStretch(0, 1);
    qjLayout->setColumnStretch(1, 1);

    stressStatsStackedWidget = new QStackedWidget(stressWidget);
    rs485StressPanel = createRs485StressPanel(stressStatsStackedWidget);
    stressStatsStackedWidget->addWidget(mtStressPanel);
    stressStatsStackedWidget->addWidget(qjStressPanel);
    stressStatsStackedWidget->addWidget(rs485StressPanel);

    layout->addWidget(controlGroup, 0, 0);
    layout->addWidget(stressStatsStackedWidget, 1, 0);
    layout->setColumnStretch(0, 1);
    layout->setRowStretch(1, 1);

    connect(stressStartBtn, &QPushButton::clicked, this, [this]() {
        startStressTest();
    });
    connect(stressStopBtn, &QPushButton::clicked, this, [this]() {
        stopStressTest(false);
    });
    connect(stressResetBtn, &QPushButton::clicked, this, [this]() {
        if (stressTestService.stats().running) {
            stopStressTest(false);
        }
        stressTestService.reset();
        updateStressTestPanel(stressTestService.stats());
        if (stressProgressBar != nullptr) {
            stressProgressBar->setValue(0);
        }
        if (stressRemainingLabel != nullptr) {
            stressRemainingLabel->setText(QStringLiteral("已清零"));
        }
    });
    connect(stressAutoSaveCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            bool needPrompt = logDirectory.isEmpty() || !QDir(logDirectory).exists();
            if (!needPrompt) {
                QMessageBox::StandardButton reply = QMessageBox::question(
                    this,
                    QStringLiteral("提示"),
                    QStringLiteral("当前已设置压力测试自动保存目录为：\n%1\n\n是否需要修改保存目录？").arg(logDirectory),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No
                );
                if (reply == QMessageBox::Yes) {
                    needPrompt = true;
                }
            }

            if (needPrompt) {
                QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择压力测试自动保存目录"), logDirectory);
                if (dir.isEmpty()) {
                    stressAutoSaveCheckBox->blockSignals(true);
                    stressAutoSaveCheckBox->setChecked(false);
                    stressAutoSaveCheckBox->blockSignals(false);
                    stressTestService.setAutoSaveEnabled(false);
                    return;
                }
                logDirectory = dir;
                stressTestService.setOutputDirectory(dir);
                saveAppConfig();
            } else {
                stressTestService.setOutputDirectory(logDirectory);
            }
        }
        stressTestService.setAutoSaveEnabled(checked);
    });
    connect(stressExportBtn, &QPushButton::clicked, this, [this]() {
        if (!exportStressSummary()) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("压力测试摘要导出失败"));
        }
    });

    updateStressTestPanel(stressTestService.stats());
    scrollArea->setWidget(stressWidget);
    return scrollArea;
}

QWidget *MainWindow::createOtaTab(QWidget *parent)
{
    QScrollArea *scrollArea = new QScrollArea(parent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *otaWidget = new QWidget(scrollArea);
    otaWidget->setMinimumWidth(900);
    QGridLayout *layout = new QGridLayout(otaWidget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(6);

    otaQueryBtn = new QPushButton(QStringLiteral("查询APP/BOOT"), otaWidget);
    otaStartUpgradeBtn = new QPushButton(QStringLiteral("开始升级"), otaWidget);
    otaAbortUpgradeBtn = new QPushButton(QStringLiteral("中止升级"), otaWidget);
    otaSelectFileBtn = new QPushButton(QStringLiteral("选择固件"), otaWidget);

    otaVersionLabel = new QLabel(QStringLiteral("待升级版本号"), otaWidget);
    otaVersionEdit = new QLineEdit(otaWidget);
    otaVersionEdit->setPlaceholderText(QStringLiteral("如 1.1.3 (首字节须与产品型号一致)"));
    otaVersionLabel->hide();
    otaVersionEdit->hide();

    otaFirmwarePathValue = new QLabel("-", otaWidget);
    otaStateValue = new QLabel(otaService.stateText(), otaWidget);
    otaMessageValue = new QLabel(QStringLiteral("点击“开始升级”或“查询APP/BOOT”启动"), otaWidget);
    otaFirmwarePathValue->setWordWrap(true);
    otaMessageValue->setWordWrap(true);

    QGroupBox *fileGroup = new QGroupBox(QStringLiteral("固件文件"), otaWidget);
    QGridLayout *fileLayout = new QGridLayout(fileGroup);
    fileLayout->setContentsMargins(8, 8, 8, 8);
    fileLayout->addWidget(otaSelectFileBtn, 0, 0);
    fileLayout->addWidget(otaFirmwarePathValue, 0, 1);
    fileLayout->addWidget(otaVersionLabel, 1, 0);
    fileLayout->addWidget(otaVersionEdit, 1, 1);

    QGroupBox *controlGroup = new QGroupBox(QStringLiteral("控制"), otaWidget);
    QGridLayout *controlLayout = new QGridLayout(controlGroup);
    controlLayout->setContentsMargins(8, 8, 8, 8);
    controlLayout->addWidget(otaQueryBtn, 0, 0);
    controlLayout->addWidget(otaStartUpgradeBtn, 0, 1);
    controlLayout->addWidget(otaAbortUpgradeBtn, 0, 2);
    qjOtaTargetLabel = new QLabel(QStringLiteral("青桔升级目标"), controlGroup);
    qjOtaTargetCombo = new QComboBox(controlGroup);
    qjOtaTargetCombo->addItem(QStringLiteral("RFR (0x0B)"), 0x0B);
    qjOtaTargetCombo->addItem(QStringLiteral("NPK (0x0A)"), 0x0A);
    controlLayout->addWidget(qjOtaTargetLabel, 1, 0);
    controlLayout->addWidget(qjOtaTargetCombo, 1, 1, 1, 2);
    qjOtaTargetLabel->hide();
    qjOtaTargetCombo->hide();

    otaStressGroup = new QGroupBox(QStringLiteral("升级压力测试"), otaWidget);
    QGridLayout *stressLayout = new QGridLayout(otaStressGroup);
    stressLayout->setContentsMargins(8, 8, 8, 8);
    stressLayout->setHorizontalSpacing(6);
    stressLayout->setVerticalSpacing(6);

    otaStressTestEnabledCheck = new QCheckBox(QStringLiteral("启用升级压力测试"), otaStressGroup);
    otaStressSuspendLogCheck = new QCheckBox(QStringLiteral("压力测试时暂停显示日志"), otaStressGroup);
    otaStressSuspendLogCheck->setChecked(true);

    otaStressCyclesSpin = new QSpinBox(otaStressGroup);
    otaStressCyclesSpin->setRange(1, 9999);
    otaStressCyclesSpin->setValue(10);

    otaCooldownSpin = new QSpinBox(otaStressGroup);
    otaCooldownSpin->setRange(500, 10000);
    otaCooldownSpin->setValue(2000);
    otaCooldownSpin->setSingleStep(500);
    otaCooldownSpin->setSuffix(" ms");

    otaCurrentCycleLabel = new QLabel("0 / 10", otaStressGroup);
    otaSuccessCyclesLabel = new QLabel("0", otaStressGroup);
    otaFailureCyclesLabel = new QLabel("0", otaStressGroup);
    otaStressSuccessRateLabel = new QLabel("0.00%", otaStressGroup);
    otaLastFailureReasonLabel = new QLabel("-", otaStressGroup);
    otaLastFailureReasonLabel->setWordWrap(true);

    stressLayout->addWidget(otaStressTestEnabledCheck, 0, 0, 1, 2);
    stressLayout->addWidget(otaStressSuspendLogCheck, 1, 0, 1, 2);
    stressLayout->addWidget(new QLabel(QStringLiteral("目标循环次数"), otaStressGroup), 2, 0);
    stressLayout->addWidget(otaStressCyclesSpin, 2, 1);
    stressLayout->addWidget(new QLabel(QStringLiteral("循环冷却时间"), otaStressGroup), 3, 0);
    stressLayout->addWidget(otaCooldownSpin, 3, 1);
    stressLayout->addWidget(new QLabel(QStringLiteral("当前循环"), otaStressGroup), 4, 0);
    stressLayout->addWidget(otaCurrentCycleLabel, 4, 1);
    stressLayout->addWidget(new QLabel(QStringLiteral("成功次数"), otaStressGroup), 5, 0);
    stressLayout->addWidget(otaSuccessCyclesLabel, 5, 1);
    stressLayout->addWidget(new QLabel(QStringLiteral("失败次数"), otaStressGroup), 6, 0);
    stressLayout->addWidget(otaFailureCyclesLabel, 6, 1);
    stressLayout->addWidget(new QLabel(QStringLiteral("成功率"), otaStressGroup), 7, 0);
    stressLayout->addWidget(otaStressSuccessRateLabel, 7, 1);
    stressLayout->addWidget(new QLabel(QStringLiteral("上次失败原因"), otaStressGroup), 8, 0);
    stressLayout->addWidget(otaLastFailureReasonLabel, 8, 1);

    // 异常注入测试面板
    otaErrorInjectionGroup = new QGroupBox(QStringLiteral("异常注入测试"), otaWidget);
    QVBoxLayout *injectLayout = new QVBoxLayout(otaErrorInjectionGroup);
    injectLayout->setContentsMargins(8, 8, 8, 8);
    injectLayout->setSpacing(4);

    otaInjectMasterCheck = new QCheckBox(QStringLiteral("启用异常注入模拟"), otaErrorInjectionGroup);
    otaInjectCrcErrorCheck = new QCheckBox(QStringLiteral("注入 CRC 校验错误 (命令 A3 尾部)"), otaErrorInjectionGroup);
    otaInjectSeqErrorCheck = new QCheckBox(QStringLiteral("注入数据包号不连续 (包号失序)"), otaErrorInjectionGroup);
    otaInjectHwMismatchCheck = new QCheckBox(QStringLiteral("注入不匹配的硬件版本号 (启动 A1)"), otaErrorInjectionGroup);
    otaInjectSilentTimeoutCheck = new QCheckBox(QStringLiteral("注入传输中途静默 (50% 进度时)"), otaErrorInjectionGroup);
    otaInjectIgnoreFcCheck = new QCheckBox(QStringLiteral("忽略流控 STmin 限制 (极速发送)"), otaErrorInjectionGroup);
    otaInjectIsoTpSnCheck = new QCheckBox(QStringLiteral("注入 ISO-TP 连续帧序号 (SN) 错误"), otaErrorInjectionGroup);
    otaInjectOutOfOrderCheck = new QCheckBox(QStringLiteral("越权升级测试 (跳过握手直接发包)"), otaErrorInjectionGroup);

    injectLayout->addWidget(otaInjectMasterCheck);
    injectLayout->addWidget(otaInjectCrcErrorCheck);
    injectLayout->addWidget(otaInjectSeqErrorCheck);
    injectLayout->addWidget(otaInjectHwMismatchCheck);
    injectLayout->addWidget(otaInjectSilentTimeoutCheck);
    injectLayout->addWidget(otaInjectIgnoreFcCheck);
    injectLayout->addWidget(otaInjectIsoTpSnCheck);
    injectLayout->addWidget(otaInjectOutOfOrderCheck);

    QGroupBox *stateGroup = new QGroupBox(QStringLiteral("状态"), otaWidget);
    QGridLayout *stateLayout = new QGridLayout(stateGroup);
    stateLayout->setContentsMargins(8, 8, 8, 8);
    stateLayout->addWidget(new QLabel(QStringLiteral("阶段"), stateGroup), 0, 0);
    stateLayout->addWidget(otaStateValue, 0, 1);
    stateLayout->addWidget(new QLabel(QStringLiteral("说明"), stateGroup), 1, 0);
    stateLayout->addWidget(otaMessageValue, 1, 1);
    stateLayout->addWidget(new QLabel(QStringLiteral("进度"), stateGroup), 2, 0);
    otaProgressBar = new QProgressBar(stateGroup);
    otaProgressBar->setRange(0, 100);
    otaProgressBar->setValue(0);
    stateLayout->addWidget(otaProgressBar, 2, 1);

    // 青桔 OTA 异常注入测试面板
    qjOtaAnomalyGroup = new QGroupBox(QStringLiteral("青桔异常注入测试"), otaWidget);
    QVBoxLayout *qjInjectLayout = new QVBoxLayout(qjOtaAnomalyGroup);
    qjInjectLayout->setContentsMargins(8, 8, 8, 8);
    qjInjectLayout->setSpacing(4);
    qjOtaAnomalyEnableCheck = new QCheckBox(QStringLiteral("启用异常注入模拟"), qjOtaAnomalyGroup);
    qjOtaAnomalyCombo = new QComboBox(qjOtaAnomalyGroup);
    qjOtaAnomalyCombo->addItem(QStringLiteral("Case 1: 固件类型不匹配 (ECU 发送 0x99)"), 1);
    qjOtaAnomalyCombo->addItem(QStringLiteral("Case 2: 部分数据损坏 (ECU 发送错误文件 CRC)"), 2);
    qjOtaAnomalyCombo->addItem(QStringLiteral("Case 3: 升级过程中断 (ECU 在第 5 块中途静默)"), 3);
    qjOtaAnomalyCombo->addItem(QStringLiteral("Case 4: 传输静默超时 (握手后 ECU 静默 6s)"), 4);
    qjOtaAnomalyCombo->addItem(QStringLiteral("Case 5: 收到 ECU 重复包 (ECU 重发第 2 块)"), 5);
    qjInjectLayout->addWidget(qjOtaAnomalyEnableCheck);
    qjInjectLayout->addWidget(qjOtaAnomalyCombo);
    qjOtaAnomalyGroup->hide(); // 默认隐藏美团外的面板

    layout->addWidget(fileGroup, 0, 0);
    layout->addWidget(controlGroup, 1, 0);
    layout->addWidget(otaStressGroup, 2, 0);
    layout->addWidget(stateGroup, 0, 1, 2, 1); // 跨 2 行
    layout->addWidget(otaErrorInjectionGroup, 2, 1); // 美团异常注入
    layout->addWidget(qjOtaAnomalyGroup, 2, 1);      // 青桔异常注入
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);

    connect(otaSelectFileBtn, &QPushButton::clicked, this, [this]() {
        const QString filePath = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择OTA固件"),
            QString(),
            QStringLiteral("固件文件 (*.bin *.hex *.s19 *.mot);;所有文件 (*.*)"));
        if (!filePath.isEmpty()) {
            otaFirmwarePathValue->setText(filePath);
            logService.logRuntime(LogLevel::Info, QString("OTA firmware selected: %1").arg(filePath));

            // 尝试从文件名中提取版本号，如 control_v1.2.255_20201210000.bin
            QRegularExpression rx("v(\\d+)\\.(\\d+)\\.(\\d+)");
            QRegularExpressionMatch match = rx.match(filePath);
            if (match.hasMatch()) {
                QString verStr = QString("%1.%2.%3").arg(match.captured(1)).arg(match.captured(2)).arg(match.captured(3));
                otaVersionEdit->setText(verStr);
            }
        }
    });
    connect(otaQueryBtn, &QPushButton::clicked, this, [this]() {
        int protocolMode = appConfig.load().protocolMode;
        if (protocolMode == 1) { // 青桔协议
            if (canStarted) {
                qingjuOtaService->queryProgramStatus(selectedQingjuOtaTarget());
            } else {
                QMessageBox::warning(this, "警告", "请先启动 CAN 设备！");
            }
        } else if (protocolMode == 4) { // 哈啰协议
            if (!serialOpened) {
                QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先打开串口！"));
                return;
            }
            if (rs485Scanning) {
                emit requestRs485StopScan();
                updateControlsState();
            }
            emit requestHlOtaQueryProgramStatus();
        } else if (protocolMode == 2 || protocolMode == 3) { // BB/FF 协议
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("BB/FF 升级协议不支持单独查询程序状态，请选择固件后直接开始升级。"));
        } else { // 美团协议
            otaService.setChannel(static_cast<quint32>(ui->sendPathCombo->currentIndex()));
            otaService.queryProgramLocation();
        }
    });
    connect(otaStartUpgradeBtn, &QPushButton::clicked, this, [this]() {
        const QString firmwarePath = otaFirmwarePathValue == nullptr ? QString() : otaFirmwarePathValue->text();
        if (firmwarePath.isEmpty() || firmwarePath == "-") {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先选择固件文件！"));
            return;
        }

        int protocolMode = appConfig.load().protocolMode;
        if (protocolMode == 1) { // 青桔协议
            if (!canStarted) {
                QMessageBox::warning(this, "警告", "请先启动 CAN 设备！");
                return;
            }
            if (otaStressTestEnabledCheck != nullptr && otaStressTestEnabledCheck->isChecked()) {
                m_otaStressRunning = true;
                m_otaCurrentCycle = 1;
                m_otaSuccessCount = 0;
                m_otaFailureCount = 0;
                m_otaLastFailureReason = "-";
                m_otaTargetCycles = otaStressCyclesSpin->value();

                m_originalLogEnabled = ui->checkBox_4->isChecked();
                if (otaStressSuspendLogCheck->isChecked()) {
                    ui->checkBox_4->setChecked(false);
                }

                updateOtaStressUI();
                logService.logRuntime(LogLevel::Info, QString("Qingju OTA stress test started. Target cycles: %1").arg(m_otaTargetCycles));
            } else {
                m_otaStressRunning = false;
            }
            // 获取并下发青桔异常配置
            QingjuOtaErrorConfig cfg;
            if (qjOtaAnomalyEnableCheck != nullptr && qjOtaAnomalyEnableCheck->isChecked()) {
                cfg.enabled = true;
                cfg.caseMode = qjOtaAnomalyCombo->currentData().toInt();
            }
            qingjuOtaService->startUpgrade(firmwarePath, selectedQingjuOtaTarget(), cfg);
        } else if (protocolMode == 2 || protocolMode == 3) { // BB/FF 协议
            if (!serialOpened) {
                QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先打开串口！"));
                return;
            }
            QString versionStr = otaVersionEdit->text().trimmed();
            QRegularExpression rx("^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}$");
            if (!rx.match(versionStr).hasMatch()) {
                QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请输入合法的升级版本号，格式如 1.1.3"));
                return;
            }
            QStringList parts = versionStr.split('.');
            if (parts[0].toInt() > 255 || parts[1].toInt() > 255 || parts[2].toInt() > 255) {
                QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("版本号每个字段的值不能超过 255"));
                return;
            }
            if (rs485Scanning) {
                emit requestRs485StopScan();
                updateControlsState();
            }
            emit requestBbFfOtaStartUpgrade(firmwarePath, versionStr);
        } else if (protocolMode == 4) { // 哈啰协议
            if (!serialOpened) {
                QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请先打开串口！"));
                return;
            }
            if (rs485Scanning) {
                emit requestRs485StopScan();
                updateControlsState();
            }
            emit requestHlOtaStartUpgrade(firmwarePath);
        } else { // 美团协议
            otaService.setChannel(static_cast<quint32>(ui->sendPathCombo->currentIndex()));
            otaService.setDeviceVersions(rfidService.vendorCode(), rfidService.hardwareVersion(), rfidService.softwareVersion());

            if (otaStressTestEnabledCheck != nullptr && otaStressTestEnabledCheck->isChecked()) {
                m_otaStressRunning = true;
                m_otaCurrentCycle = 1;
                m_otaSuccessCount = 0;
                m_otaFailureCount = 0;
                m_otaLastFailureReason = "-";
                m_otaTargetCycles = otaStressCyclesSpin->value();

                // 暂停实时总线日志渲染
                m_originalLogEnabled = ui->checkBox_4->isChecked();
                if (otaStressSuspendLogCheck->isChecked()) {
                    ui->checkBox_4->setChecked(false);
                }

                updateOtaStressUI();
                logService.logRuntime(LogLevel::Info, QString("OTA stress test started. Target cycles: %1").arg(m_otaTargetCycles));
            } else {
                m_otaStressRunning = false;
            }

            otaService.startUpgrade(firmwarePath, getOtaErrorConfig());
        }
    });
    connect(otaAbortUpgradeBtn, &QPushButton::clicked, this, [this]() {
        int protocolMode = appConfig.load().protocolMode;
        if (protocolMode == 1) { // 青桔协议
            if (m_otaStressRunning) {
                m_otaStressRunning = false;
                m_otaCooldownTimer->stop();
                ui->checkBox_4->setChecked(m_originalLogEnabled);
                updateOtaStressUI();
                logService.logRuntime(LogLevel::Info, "Qingju OTA stress test manually aborted.");
            }
            qingjuOtaService->abortUpgrade();
        } else if (protocolMode == 4) { // 哈啰协议
            emit requestHlOtaAbortUpgrade();
        } else if (protocolMode == 2 || protocolMode == 3) { // BB/FF 协议
            emit requestBbFfOtaAbortUpgrade();
        } else { // 美团协议
            if (m_otaStressRunning) {
                m_otaStressRunning = false;
                m_otaCooldownTimer->stop();
                ui->checkBox_4->setChecked(m_originalLogEnabled);
                updateOtaStressUI();
                logService.logRuntime(LogLevel::Info, "OTA stress test manually aborted.");
            }
            otaService.abortUpgrade();
        }
    });

    connect(otaStressCyclesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        if (!m_otaStressRunning) {
            updateOtaStressUI();
        }
    });

    connect(otaInjectMasterCheck, &QCheckBox::toggled, this, &MainWindow::onOtaInjectMasterToggled);
    onOtaInjectMasterToggled(false); // 初始化置灰所有子 Checkbox

    scrollArea->setWidget(otaWidget);
    return scrollArea;
}

void MainWindow::setLabelValue(QLabel *label, const QString &value)
{
    if (label != nullptr) {
        label->setText(value.isEmpty() ? "-" : value);
    }
}

bool MainWindow::parseQingjuAddress(const QString &text, quint8 *address, QString *error) const
{
    const QString trimmed = text.trimmed();
    bool ok = false;
    const ushort value = trimmed.toUShort(&ok, 16);
    if (!ok || trimmed.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("目标设备地址必须为十六进制数。");
        }
        return false;
    }
    if (value > 0x3F) {
        if (error != nullptr) {
            *error = QStringLiteral("目标设备地址超出范围，青桔地址合法范围为 0x00~0x3F。");
        }
        return false;
    }
    if (address != nullptr) {
        *address = static_cast<quint8>(value);
    }
    return true;
}

void MainWindow::clearQingjuRfidPanel()
{
    setLabelValue(qjRfidResultValue, QString());
    if (qjRfidResultValue != nullptr) {
        qjRfidResultValue->setStyleSheet(QString());
    }
    setLabelValue(qjRfidAppStatusValue, QString());
    if (qjRfidAppStatusValue != nullptr) {
        qjRfidAppStatusValue->setStyleSheet(QString());
    }
    setLabelValue(qjRfidAlarmValue, QString());
    if (qjRfidAlarmValue != nullptr) {
        qjRfidAlarmValue->setStyleSheet(QString());
    }
    setLabelValue(qjRfidUidValue, QString());
    setLabelValue(qjRfidPwdValue, QString());
    setLabelValue(qjRfidModelValue, QString());
    setLabelValue(qjRfidSupplierValue, QString());
    setLabelValue(qjRfidSerialValue, QString());
    setLabelValue(qjRfidSnValue, QString());
    setLabelValue(qjRfidFirmwareVerValue, QString());
    setLabelValue(qjRfidHardwareVerValue, QString());
    setLabelValue(qjRfidVendorValue, QString());
    setLabelValue(qjRfidModelCodeValue, QString());
    setLabelValue(qjRfidFwStrValue, QString());
    setLabelValue(qjRfidHwStrValue, QString());
}

void MainWindow::updateMeituanTopStatus()
{
    if (topRfidStatusValue == nullptr) {
        return;
    }

    if (!canStarted) {
        topRfidStatusValue->setText(QStringLiteral("协议：美团  RFID：未启动  扫描：停"));
        topRfidStatusValue->setStyleSheet(QStringLiteral("color: gray; font-weight: bold;"));
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const bool online = lastRfidFrameTime.isValid() && lastRfidFrameTime.msecsTo(now) < 1500;
    const QString onlineText = online ? QStringLiteral("在线") : QStringLiteral("离线");
    const QString scanText = rfidScanning ? QStringLiteral("开") : QStringLiteral("停");

    const RfidState state = rfidService.state();
    const QString workModeText = state.workMode.isEmpty() ? QStringLiteral("-") : state.workMode;
    const QString cardText = state.cardStatus.isEmpty() ? QStringLiteral("-") : state.cardStatus;
    const QString faultText = state.faultStatus.isEmpty() ? QStringLiteral("-") : state.faultStatus;
    const QString tagText = state.tag.isEmpty() ? QStringLiteral("未识别") : state.tag;

    QStringList statusParts;
    statusParts << QStringLiteral("协议：美团")
                << QStringLiteral("RFID：%1").arg(onlineText)
                << QStringLiteral("扫描：%1").arg(scanText)
                << QStringLiteral("工作：%1").arg(workModeText)
                << QStringLiteral("卡：%1").arg(cardText)
                << QStringLiteral("故障：%1").arg(faultText)
                << QStringLiteral("TAG：%1").arg(tagText);
    topRfidStatusValue->setText(statusParts.join(QStringLiteral("  ")));

    if (!online) {
        topRfidStatusValue->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
    } else if (!state.faultStatus.isEmpty() && !state.faultStatus.contains(QStringLiteral("无故障"))) {
        topRfidStatusValue->setStyleSheet(QStringLiteral("color: #B26A00; font-weight: bold;"));
    } else {
        topRfidStatusValue->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
    }
}

void MainWindow::updateQingjuOnlineStatus(bool clearOfflineData)
{
    if (!canStarted) {
        if (topRfidStatusValue != nullptr) {
            topRfidStatusValue->setText(QStringLiteral("协议：青桔  NPK：未启动  RFR：未启动"));
            topRfidStatusValue->setStyleSheet(QStringLiteral("color: gray; font-weight: bold;"));
        }
        if (rfidOnlineStatusValue != nullptr) {
            rfidOnlineStatusValue->setText(QStringLiteral("未启动"));
            rfidOnlineStatusValue->setStyleSheet(QStringLiteral("color: gray; font-weight: bold;"));
        }
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const bool npkOnline = lastQingjuNpkFrameTime.isValid() && lastQingjuNpkFrameTime.msecsTo(now) < 1500;
    const bool rfrOnline = lastQingjuRfrFrameTime.isValid() && lastQingjuRfrFrameTime.msecsTo(now) < 1500;
    const QString npkText = npkOnline ? QStringLiteral("在线") : QStringLiteral("离线");
    const QString rfrText = rfrOnline ? QStringLiteral("在线") : QStringLiteral("离线");

    if (topRfidStatusValue != nullptr) {
        topRfidStatusValue->setText(QStringLiteral("协议：青桔  NPK：%1  RFR：%2").arg(npkText, rfrText));
        if (npkOnline && rfrOnline) {
            topRfidStatusValue->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        } else if (npkOnline || rfrOnline) {
            topRfidStatusValue->setStyleSheet(QStringLiteral("color: #B26A00; font-weight: bold;"));
        } else {
            topRfidStatusValue->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
        }
    }

    if (rfidOnlineStatusValue != nullptr) {
        rfidOnlineStatusValue->setText(QStringLiteral("NPK：%1 / RFR：%2").arg(npkText, rfrText));
        if (npkOnline && rfrOnline) {
            rfidOnlineStatusValue->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        } else if (npkOnline || rfrOnline) {
            rfidOnlineStatusValue->setStyleSheet(QStringLiteral("color: #B26A00; font-weight: bold;"));
        } else {
            rfidOnlineStatusValue->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
        }
    }

    bool isTargetOffline = false;
    if (qingjuRfidService != nullptr) {
        if (qingjuRfidService->targetAddress() == 0x0A) {
            isTargetOffline = !npkOnline;
        } else {
            isTargetOffline = !rfrOnline;
        }
    } else {
        isTargetOffline = !npkOnline;
    }
    if (clearOfflineData && isTargetOffline) {
        clearQingjuRfidPanel();
    }
}

void MainWindow::sendRfidFrame(UINT canId, const QByteArray &payload)
{
    const UINT channel = static_cast<UINT>(ui->sendPathCombo->currentIndex());
    if (!canthread->sendClassicData(canId, channel, payload)) {
        logService.logRuntime(LogLevel::Error,
                              QString("Failed to send RFID frame: id=0x%1").arg(canId, 0, 16));
        return;
    }

    CanFrame frame;
    frame.id = canId;
    frame.channel = channel;
    frame.data = payload;
    frame.direction = CanFrameDirection::Tx;
    frame.protocol = CanFrameProtocol::ClassicCan;
    frame.hostDateTime = QDateTime::currentDateTime();
    addCanFrameToList(frame);
    appendTestEvidenceFrame(frame, protocolDecodeText(frame));
}

void MainWindow::handleRfidFrame(const CanFrame &frame)
{
    if (frame.id == RfidProtocol::ResponseFrameId) {
        rfidDiagnosticTransfer.handleResponseFrame(frame);
    }

    if (rfidService.handleFrame(frame)) {
        updateRfidPanel(rfidService.state());
        if (frame.id == RfidProtocol::StatusFrameId) {
            productionTestService.handleRfidStatus(rfidService.state());
        } else if (frame.id == RfidProtocol::TagPart1FrameId ||
                   frame.id == RfidProtocol::TagPart2FrameId ||
                   frame.id == RfidProtocol::TagPart3FrameId) {
            productionTestService.handleRfidTagUpdate(rfidService.state().tag);
        }
    }
}

void MainWindow::updateRfidPanel(const RfidState &state)
{
    setLabelValue(rfidWorkModeValue, state.workMode);
    setLabelValue(rfidCardStatusValue, state.cardStatus);
    setLabelValue(rfidFaultStatusValue, state.faultStatus);
    setLabelValue(rfidScanPeriodValue, state.scanPeriod);
    setLabelValue(rfidTagValue, state.tag);
    setLabelValue(rfidTagPart1Value, state.tagPart1);
    setLabelValue(rfidTagPart2Value, state.tagPart2);
    setLabelValue(rfidTagPart3Value, state.tagPart3);
    setLabelValue(rfidDeviceIdValue, state.deviceId);
    setLabelValue(rfidVersionValue, state.version);
    setLabelValue(rfidResponseValue, state.response);
    updateMeituanTopStatus();
}

void MainWindow::updateStressTestPanel(const StressTestStats &stats)
{
    setLabelValue(stressStateValue, stats.running ? QStringLiteral("运行中") : QStringLiteral("已停止"));
    setLabelValue(stressElapsedValue, QString("%1 s").arg(stats.elapsedSeconds));
    setLabelValue(stressTotalSamplesValue, QString::number(stats.totalSamples));
    setLabelValue(stressSuccessCountValue, QString::number(stats.successCount));
    setLabelValue(stressNoTagCountValue, QString::number(stats.noTagCount));
    setLabelValue(stressTagLengthErrorValue, QString::number(stats.tagLengthErrorCount));
    setLabelValue(stressFaultCountValue,
                  QString("%1 / %2").arg(stats.moduleFaultCount).arg(stats.communicationFaultCount));
    setLabelValue(stressSuccessRateValue, QString("%1%").arg(stats.successRate, 0, 'f', 2));
    setLabelValue(stressTagValidRateValue, QString("%1%").arg(stats.tagValidRate, 0, 'f', 2));
    setLabelValue(stressCurrentTagValue, stats.currentTag);
    setLabelValue(stressLastSuccessTagValue, stats.lastSuccessTag);
    setLabelValue(stressTagChangeCountValue, QString::number(stats.tagChangeCount));
    setLabelValue(stressUniqueTagCountValue, QString::number(stats.uniqueTagCount));
    setLabelValue(stressMaxContinuousFailureValue, QString::number(stats.maxContinuousFailure));
    setLabelValue(stressLastFailureReasonValue, stats.lastFailureReason);

    setLabelValue(qjStressStateValue, stats.running ? QStringLiteral("运行中") : QStringLiteral("已停止"));
    setLabelValue(qjStressElapsedValue, QString("%1 s").arg(stats.elapsedSeconds));
    setLabelValue(qjStressTotalSamplesValue, QString::number(stats.totalSamples));
    setLabelValue(qjStressSuccessCountValue, QString::number(stats.successCount));
    setLabelValue(qjStressSuccessRateValue, QString("%1%").arg(stats.successRate, 0, 'f', 2));
    setLabelValue(qjStressCurrentUidValue, stats.currentTag);
    setLabelValue(qjStressLastSuccessUidValue, stats.lastSuccessTag);
    setLabelValue(qjStressUniqueUidCountValue, QString::number(stats.uniqueTagCount));
    setLabelValue(qjStressNoTagCountValue, QString::number(stats.noTagCount));
    setLabelValue(qjStressUidReadErrorValue, QString::number(stats.tagLengthErrorCount));
    setLabelValue(qjStressModuleFaultValue, QString::number(stats.moduleFaultCount));
    setLabelValue(qjStressCommunicationFaultValue, QString::number(stats.communicationFaultCount));
    setLabelValue(qjStressContentErrorValue, QString::number(stats.tagContentErrorCount));
    setLabelValue(qjStressMaxContinuousFailureValue, QString::number(stats.maxContinuousFailure));
    setLabelValue(qjStressLastFailureReasonValue, stats.lastFailureReason);

    setLabelValue(rs485StressStateValue, stats.running ? QStringLiteral("运行中") : QStringLiteral("已停止"));
    setLabelValue(rs485StressElapsedValue, QString("%1 s").arg(stats.elapsedSeconds));
    setLabelValue(rs485StressTotalSamplesValue, QString::number(stats.totalSamples));
    setLabelValue(rs485StressPollSkippedValue, QString::number(stats.pollSkippedCount));
    setLabelValue(rs485StressSuccessCountValue, QString::number(stats.successCount));
    setLabelValue(rs485StressSuccessRateValue, QString("%1%").arg(stats.successRate, 0, 'f', 2));
    setLabelValue(rs485StressCurrentTagValue, stats.currentTag);
    setLabelValue(rs485StressLastSuccessTagValue, stats.lastSuccessTag);
    setLabelValue(rs485StressUniqueTagCountValue, QString::number(stats.uniqueTagCount));
    setLabelValue(rs485StressNoTagCountValue, QString::number(stats.noTagCount));
    setLabelValue(rs485StressModuleFaultValue, QString::number(stats.moduleFaultCount));
    setLabelValue(rs485StressCommunicationFaultValue, QString::number(stats.communicationFaultCount));
    setLabelValue(rs485StressMaxContinuousFailureValue, QString::number(stats.maxContinuousFailure));
    setLabelValue(rs485StressLastFailureReasonValue, stats.lastFailureReason);

    if (topStressStatusValue != nullptr) {
        topStressStatusValue->setText(QStringLiteral("压测：%1 成功率 %2%")
                                      .arg(stats.running ? QStringLiteral("运行中") : QStringLiteral("已停止"))
                                      .arg(stats.successRate, 0, 'f', 2));
    }
}

void MainWindow::updateProductionTestPanel(const ProductionTestState &state)
{
    if (productionStateValue == nullptr) {
        return;
    }

    const bool running = state.running;
    setLabelValue(productionStateValue, state.phaseText);
    setLabelValue(productionSnValue, state.sn);
    setLabelValue(productionWriteValue,
                  productionTestService.isWritingSn() ? QStringLiteral("写入中") :
                  (state.sn.isEmpty() ? QStringLiteral("未写入") :
                   (state.resultText == QStringLiteral("FAIL") && state.completedSamples == 0 ? QStringLiteral("写入失败") : QStringLiteral("写入完成"))));
    setLabelValue(productionProgressValue, QStringLiteral("%1 / %2").arg(state.completedSamples).arg(state.totalSamples));
    setLabelValue(productionSuccessValue, QString::number(state.successCount));
    setLabelValue(productionFailureValue, QString::number(state.failureCount));
    setLabelValue(productionRateValue, QStringLiteral("%1%").arg(state.successRate, 0, 'f', 2));
    setLabelValue(productionTagValue, state.currentTag.isEmpty() ? QStringLiteral("-") : state.currentTag);
    setLabelValue(productionFailureReasonValue, state.lastFailureReason.isEmpty() ? QStringLiteral("-") : state.lastFailureReason);

    const int progress = state.totalSamples <= 0 ? 0 :
        qBound(0, static_cast<int>((state.completedSamples * 100) / state.totalSamples), 100);
    if (productionProgressBar != nullptr) {
        productionProgressBar->setValue(progress);
    }

    if (productionResultBanner != nullptr) {
        QString text = state.resultText;
        QString style = QStringLiteral("background:#E5E7EB;color:#374151;border-radius:6px;");
        if (running) {
            text = state.phaseText.isEmpty() ? QStringLiteral("RUNNING") : state.phaseText;
            style = QStringLiteral("background:#2563EB;color:white;border-radius:6px;");
        } else if (state.resultText == QStringLiteral("PASS")) {
            style = QStringLiteral("background:#16A34A;color:white;border-radius:6px;");
        } else if (state.resultText == QStringLiteral("FAIL")) {
            style = QStringLiteral("background:#DC2626;color:white;border-radius:6px;");
        } else if (state.resultText == QStringLiteral("STOPPED")) {
            style = QStringLiteral("background:#B26A00;color:white;border-radius:6px;");
        } else if (text.isEmpty() || text == QStringLiteral("-")) {
            text = QStringLiteral("待扫码");
        }
        productionResultBanner->setText(text);
        productionResultBanner->setStyleSheet(style);
    }

    if (productionSnEdit != nullptr) {
        productionSnEdit->setEnabled(!running);
    }
    const bool canStartProduction = canStarted &&
        !stressTestService.stats().running &&
        !isOtaRunning() &&
        protocolModeCombo != nullptr &&
        protocolModeCombo->currentIndex() == 0;
    if (productionPassThresholdSpin != nullptr) {
        productionPassThresholdSpin->setEnabled(!running && canStartProduction);
    }
    if (productionStartBtn != nullptr) {
        productionStartBtn->setEnabled(!running && canStartProduction);
    }
    if (productionStopBtn != nullptr) {
        productionStopBtn->setEnabled(running);
    }
    if (productionClearBtn != nullptr) {
        productionClearBtn->setEnabled(!running);
    }
}

void MainWindow::appendProductionTestLog(const QString &message)
{
    if (productionLogText == nullptr) {
        return;
    }
    const QString line = QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().time().toString(QStringLiteral("hh:mm:ss.zzz")))
        .arg(message);
    productionLogText->append(line);
}

void MainWindow::startProductionTestFromInput()
{
    if (productionSnEdit == nullptr) {
        return;
    }
    startProductionTestFromSn(productionSnEdit->text());
}

void MainWindow::startProductionTestFromSn(const QString &sn)
{
    const int protocolMode = protocolModeCombo == nullptr ? 0 : protocolModeCombo->currentIndex();
    if (protocolMode != 0) {
        QMessageBox::warning(this, QStringLiteral("产线检测"), QStringLiteral("产线检测仅支持美团协议。"));
        return;
    }
    if (!canStarted) {
        QMessageBox::warning(this, QStringLiteral("产线检测"), QStringLiteral("请先启动 CAN。"));
        prepareProductionSnInput();
        return;
    }
    if (stressTestService.stats().running) {
        QMessageBox::warning(this, QStringLiteral("产线检测"), QStringLiteral("请先停止压力测试。"));
        prepareProductionSnInput();
        return;
    }
    if (isOtaRunning()) {
        QMessageBox::warning(this, QStringLiteral("产线检测"), QStringLiteral("OTA 正在运行，不能开始产线检测。"));
        prepareProductionSnInput();
        return;
    }
    if (rfidDiagnosticTransfer.isBusy() && !productionWritePending) {
        QMessageBox::warning(this, QStringLiteral("产线检测"), QStringLiteral("当前有 0x2E 写入正在进行，请稍后再试。"));
        prepareProductionSnInput();
        return;
    }

    QString error;
    ProductionTestConfig config;
    config.totalSamples = 100;
    config.passRateThreshold = productionPassThresholdSpin == nullptr ? 95.0 : productionPassThresholdSpin->value();
    if (!productionTestService.start(sn, config, &error)) {
        QMessageBox::warning(this, QStringLiteral("SN格式错误"), error);
        if (productionSnEdit != nullptr) {
            productionSnEdit->setStyleSheet(QStringLiteral("border:1px solid #DC2626;"));
        }
        prepareProductionSnInput();
        return;
    }

    if (productionSnEdit != nullptr) {
        productionSnEdit->setStyleSheet(QString());
        productionSnEdit->setText(productionTestService.state().sn);
    }
    rfidService.reset();
    updateProductionTestPanel(productionTestService.state());
    updateControlsState();
}

void MainWindow::stopProductionTest()
{
    productionTestService.stop(QStringLiteral("用户停止"));
    abortRfidDiagnosticTransferSilently();
    prepareProductionSnInput();
    updateControlsState();
}

void MainWindow::abortRfidDiagnosticTransferSilently()
{
    if (!rfidDiagnosticTransfer.isBusy()) {
        return;
    }

    productionWritePending = true;
    rfidDiagnosticTransfer.abort();
    productionWritePending = false;
}

void MainWindow::clearProductionTestPanel()
{
    if (productionTestService.isRunning()) {
        return;
    }
    productionTestService.reset();
    if (productionLogText != nullptr) {
        productionLogText->clear();
    }
    if (productionSnEdit != nullptr) {
        productionSnEdit->setStyleSheet(QString());
    }
    prepareProductionSnInput();
}

void MainWindow::prepareProductionSnInput()
{
    if (productionSnEdit == nullptr || productionTestService.isRunning()) {
        return;
    }
    productionSnEdit->clear();
    productionSnEdit->setEnabled(true);
    productionSnEdit->setFocus();
    productionSnEdit->selectAll();
}

void MainWindow::processProductionScanText(const QString &text, bool showError)
{
    if (productionTestService.isRunning()) {
        appendProductionTestLog(QStringLiteral("检测中收到扫码输入，已忽略"));
        return;
    }
    const QString sn = text.trimmed().toUpper();
    if (sn.isEmpty()) {
        return;
    }
    QString error;
    if (!ProductionTestService::validateSn(sn, &error)) {
        if (showError || sn.size() >= 16) {
            appendProductionTestLog(QStringLiteral("扫码SN无效：%1，内容=%2").arg(error, sn));
            if (productionSnEdit != nullptr) {
                productionSnEdit->setText(sn);
                productionSnEdit->setStyleSheet(QStringLiteral("border:1px solid #DC2626;"));
            }
        }
        return;
    }
    if (productionSnEdit != nullptr) {
        productionSnEdit->setText(sn);
    }
    startProductionTestFromSn(sn);
}

void MainWindow::sendProductionScanControl(bool enabled)
{
    rfidScanning = enabled;
    if (canStarted) {
        sendRfidFrame(RfidProtocol::ControlFrameId, RfidProtocol::buildControlFrame(enabled));
    }
    updateMeituanTopStatus();
}

void MainWindow::addCanFrameToList(const CanFrame &frame)
{
    logService.logCanFrame(frame);
    if (!ui->checkBox_4->isChecked()) {
        return;
    }
    if (frame.id == RfidProtocol::ControlFrameId && show0x207LogCheck != nullptr && !show0x207LogCheck->isChecked()) {
        return;
    }

    QStringList messageList;
    messageList << frame.hostDateTime.time().toString("hh:mm:ss zzz");
    messageList << QString::number(frame.channel);
    messageList << frame.directionText();
    messageList << frame.idText();
    messageList << frame.frameTypeText();
    messageList << frame.payloadTypeText();
    messageList << QString::number(frame.dlc());
    messageList << frame.protocolText();
    messageList << (frame.remoteFrame ? QString() : frame.dataText());
    messageList << protocolDecodeText(frame);
    AddDataToList(messageList);
}

QString MainWindow::protocolDecodeText(const CanFrame &frame) const
{
    if (frame.remoteFrame || frame.data.isEmpty()) {
        return QStringLiteral("-");
    }

    const int protocolMode = protocolModeCombo == nullptr ? 0 : protocolModeCombo->currentIndex();
    if (protocolMode == 0 && !frame.extendedFrame) {
        const QByteArray &payload = frame.data;
        if (frame.id == RfidProtocol::ControlFrameId && payload.size() >= 1) {
            const quint8 enableScan = static_cast<quint8>(payload.at(0));
            return QStringLiteral("美团RFID控制: %1")
                .arg(enableScan == 0x01 ? QStringLiteral("开始检测") :
                     (enableScan == 0x00 ? QStringLiteral("停止检测") : QStringLiteral("未知控制值 0x%1").arg(enableScan, 2, 16, QChar('0')).toUpper()));
        }
        if (frame.id == RfidProtocol::RequestFrameId && payload.size() >= 2) {
            const quint8 pci = static_cast<quint8>(payload.at(0));
            const quint8 pciType = (pci >> 4) & 0x0F;
            if (pciType == 1 && payload.size() >= 2) {
                const int totalLength = ((pci & 0x0F) << 8) | static_cast<quint8>(payload.at(1));
                const QByteArray firstData = payload.mid(2, qMin(6, payload.size() - 2));
                if (firstData.size() >= 3 && static_cast<quint8>(firstData.at(0)) == 0x2E) {
                    const quint16 did = (static_cast<quint8>(firstData.at(1)) << 8) | static_cast<quint8>(firstData.at(2));
                    const QByteArray data = firstData.mid(3);
                    return QStringLiteral("美团诊断 0x2E 首帧: DID=0x%1 总长=%2 首帧DATA=%3")
                        .arg(did, 4, 16, QChar('0')).toUpper()
                        .arg(totalLength)
                        .arg(QString::fromLatin1(data.toHex(' ').toUpper()));
                }
                return QStringLiteral("美团ISO-TP首帧: 总长=%1 DATA=%2")
                    .arg(totalLength)
                    .arg(QString::fromLatin1(firstData.toHex(' ').toUpper()));
            }
            if (pciType == 2) {
                const quint8 sequenceNumber = pci & 0x0F;
                const QByteArray data = payload.mid(1);
                return QStringLiteral("美团ISO-TP连续帧: SN=%1 DATA=%2")
                    .arg(sequenceNumber)
                    .arg(QString::fromLatin1(data.toHex(' ').toUpper()));
            }
            if (pciType != 0) {
                return QStringLiteral("美团ISO-TP请求帧: PCI=0x%1")
                    .arg(pci, 2, 16, QChar('0')).toUpper();
            }

            const quint8 sfLength = pci & 0x0F;
            const quint8 sid = static_cast<quint8>(payload.at(1));
            switch (sid) {
            case 0x01:
                if (payload.size() >= 3) {
                    return QStringLiteral("美团RFID设置扫描周期: %1 ms")
                        .arg(static_cast<int>(static_cast<quint8>(payload.at(2))) * 10);
                }
                break;
            case 0x02:
                return QStringLiteral("美团RFID模块重启");
            case 0x10:
                if (payload.size() >= 3) {
                    const quint8 targetMode = static_cast<quint8>(payload.at(2));
                    return QStringLiteral("美团诊断 0x10 跳转: %1")
                        .arg(targetMode == 0x01 ? QStringLiteral("APP") :
                             (targetMode == 0x02 ? QStringLiteral("BOOT") : QStringLiteral("未知 0x%1").arg(targetMode, 2, 16, QChar('0')).toUpper()));
                }
                break;
            case 0x11:
                return QStringLiteral("美团诊断 0x11 软件复位");
            case 0x28:
                if (payload.size() >= 3) {
                    const quint8 enabled = static_cast<quint8>(payload.at(2));
                    return QStringLiteral("美团诊断 0x28 广播控制: %1")
                        .arg(enabled == 0x01 ? QStringLiteral("使能周期发送") :
                             (enabled == 0x00 ? QStringLiteral("禁止周期发送") : QStringLiteral("未知 0x%1").arg(enabled, 2, 16, QChar('0')).toUpper()));
                }
                break;
            case 0x29:
                if (payload.size() >= 6) {
                    const quint16 canId = (static_cast<quint8>(payload.at(2)) << 8) | static_cast<quint8>(payload.at(3));
                    const quint16 periodMs = (static_cast<quint8>(payload.at(4)) << 8) | static_cast<quint8>(payload.at(5));
                    return QStringLiteral("美团诊断 0x29 周期配置: ID=0x%1 周期=%2")
                        .arg(canId, 3, 16, QChar('0')).toUpper()
                        .arg(periodMs == 0xFFFF ? QStringLiteral("不发送") : QStringLiteral("%1 ms").arg(periodMs));
                }
                break;
            case 0x85:
                if (payload.size() >= 3) {
                    const quint8 enabled = static_cast<quint8>(payload.at(2));
                    return QStringLiteral("美团诊断 0x85 通信故障诊断: %1")
                        .arg(enabled == 0x01 ? QStringLiteral("启用") :
                             (enabled == 0x00 ? QStringLiteral("禁用") : QStringLiteral("未知 0x%1").arg(enabled, 2, 16, QChar('0')).toUpper()));
                }
                break;
            case 0x2E:
                if (payload.size() >= 4) {
                    const quint16 did = (static_cast<quint8>(payload.at(2)) << 8) | static_cast<quint8>(payload.at(3));
                    const int dataLength = qMax(0, static_cast<int>(sfLength) - 3);
                    const QByteArray data = payload.mid(4, qMin(dataLength, payload.size() - 4));
                    return QStringLiteral("美团诊断 0x2E 写非易失: DID=0x%1 DATA=%2")
                        .arg(did, 4, 16, QChar('0')).toUpper()
                        .arg(QString::fromLatin1(data.toHex(' ').toUpper()));
                }
                break;
            default:
                return QStringLiteral("美团事件请求 SID=0x%1 SF_DL=%2")
                    .arg(sid, 2, 16, QChar('0')).toUpper()
                    .arg(sfLength);
            }
        }
        if (frame.id == RfidProtocol::ResponseFrameId) {
            const quint8 pci = static_cast<quint8>(payload.at(0));
            const quint8 pciType = (pci >> 4) & 0x0F;
            if (pciType == 3 && payload.size() >= 3) {
                const quint8 flowStatus = pci & 0x0F;
                const quint8 blockSize = static_cast<quint8>(payload.at(1));
                const quint8 stMin = static_cast<quint8>(payload.at(2));
                QString flowStatusText;
                switch (flowStatus) {
                case 0: flowStatusText = QStringLiteral("CTS"); break;
                case 1: flowStatusText = QStringLiteral("WAIT"); break;
                case 2: flowStatusText = QStringLiteral("OVERFLOW"); break;
                default: flowStatusText = QStringLiteral("未知"); break;
                }
                return QStringLiteral("美团ISO-TP流控帧: FS=%1 BS=%2 STmin=0x%3")
                    .arg(flowStatusText)
                    .arg(blockSize)
                    .arg(stMin, 2, 16, QChar('0')).toUpper();
            }

            const RfidResponse response = RfidProtocol::parseResponseFrame(payload);
            if (response.positive) {
                const QString dataText = response.data.isEmpty()
                    ? QString()
                    : QStringLiteral(" DATA=%1").arg(QString::fromLatin1(response.data.toHex(' ').toUpper()));
                return QStringLiteral("美团诊断肯定响应 SID=0x%1%2")
                    .arg(response.sid, 2, 16, QChar('0')).toUpper()
                    .arg(dataText);
            }
            if (response.negative) {
                return QStringLiteral("美团诊断否定响应 SID=0x%1 NRC=0x%2")
                    .arg(response.originalSid, 2, 16, QChar('0'))
                    .arg(response.negativeCode, 2, 16, QChar('0')).toUpper();
            }
            return QStringLiteral("美团诊断响应: 未识别");
        }
        if (frame.id == RfidProtocol::StatusFrameId) {
            const RfidStatus status = RfidProtocol::parseStatusFrame(payload);
            if (status.valid) {
                return QStringLiteral("美团RFID状态: 工作=%1 卡=%2 故障=%3 周期=%4 ms")
                    .arg(RfidProtocol::workModeText(status.workMode))
                    .arg(RfidProtocol::cardStatusText(status.cardStatus))
                    .arg(RfidProtocol::faultStatusText(status.faultStatus))
                    .arg(static_cast<int>(status.scanPeriod10ms) * 10);
            }
        }
        if (frame.id == RfidProtocol::VersionFrameId) {
            const RfidVersion version = RfidProtocol::parseVersionFrame(payload);
            if (version.valid) {
                return QStringLiteral("美团RFID版本: Boot=%1 Vendor=%2 HW=0x%3 SW=0x%4 MAT=0x%5")
                    .arg(static_cast<int>(version.bootVersion))
                    .arg(RfidProtocol::vendorText(version.vendorCode))
                    .arg(version.hardwareVersion, 4, 16, QChar('0'))
                    .arg(version.softwareVersion, 4, 16, QChar('0'))
                    .arg(version.materialRecord, 4, 16, QChar('0')).toUpper();
            }
        }
        if (frame.id == RfidProtocol::TagPart1FrameId ||
            frame.id == RfidProtocol::TagPart2FrameId ||
            frame.id == RfidProtocol::TagPart3FrameId) {
            return QStringLiteral("美团RFID TAG分片: %1").arg(RfidProtocol::parseAsciiPayload(payload));
        }
        if (frame.id == RfidProtocol::DeviceIdPart1FrameId ||
            frame.id == RfidProtocol::DeviceIdPart2FrameId) {
            return QStringLiteral("美团RFID设备ID分片: %1").arg(RfidProtocol::parseAsciiPayload(payload));
        }
    }

    if (protocolMode != 1 || !frame.extendedFrame) {
        return QStringLiteral("-");
    }

    const QingjuCanId id = QingjuCanId::parse(frame.id);
    QString text = QString("QJ src=%1 dst=%2 pri=%3 q=%4 idx=%5")
        .arg(qingjuAddressName(id.srcAddr))
        .arg(qingjuAddressName(id.destAddr))
        .arg(id.priority)
        .arg(id.queue)
        .arg(id.index);

    if (!frame.remoteFrame && !frame.data.isEmpty() && id.index == 0) {
        const quint8 funcCode = static_cast<quint8>(frame.data.at(0));
        text += QString(" func=0x%1").arg(funcCode, 2, 16, QChar('0')).toUpper();
    }
    return text;
}

QString MainWindow::qingjuAddressName(quint8 address) const
{
    switch (address) {
    case 0x01: return QStringLiteral("ECU(0x01)");
    case 0x0A: return QStringLiteral("NPK(0x0A)");
    case 0x0B: return QStringLiteral("RFR(0x0B)");
    default: return QString("0x%1").arg(address, 2, 16, QChar('0')).toUpper();
    }
}

void MainWindow::setupCanLogSaveButton()
{
    saveCanLogButton = new QPushButton(QStringLiteral("保存CAN日志"), ui->cleanListBtn->parentWidget());
    connect(saveCanLogButton, &QPushButton::clicked, this, &MainWindow::exportCanLogSnapshot);
}

void MainWindow::exportCanLogSnapshot()
{
    flushPendingLogRows();

    const QString defaultFilePath = QDir(logDirectory).filePath(
        QString("can_snapshot_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存CAN日志"),
        defaultFilePath,
        QStringLiteral("CSV文件 (*.csv)"));
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logService.logRuntime(LogLevel::Error, QString("Failed to export CAN log snapshot: %1").arg(filePath));
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("CAN日志保存失败"));
        return;
    }

    auto csvEscape = [](QString value) {
        value.replace("\"", "\"\"");
        return QString("\"%1\"").arg(value);
    };

    QTextStream stream(&file);
    QStringList headers;
    for (int column = 0; column < ui->tableWidget->columnCount(); ++column) {
        QTableWidgetItem *headerItem = ui->tableWidget->horizontalHeaderItem(column);
        headers << csvEscape(headerItem == nullptr ? QString() : headerItem->text());
    }
    stream << headers.join(',') << '\n';

    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        QStringList rowValues;
        for (int column = 0; column < ui->tableWidget->columnCount(); ++column) {
            QTableWidgetItem *item = ui->tableWidget->item(row, column);
            rowValues << csvEscape(item == nullptr ? QString() : item->text());
        }
        stream << rowValues.join(',') << '\n';
    }

    logService.logRuntime(LogLevel::Info, QString("CAN log snapshot exported: %1").arg(filePath));
    QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("CAN日志已保存"));
}

void MainWindow::applyCanLogCompact(bool compact)
{
    canLogCompact = compact;

    if (compactCanLogButton != nullptr) {
        compactCanLogButton->setText(compact ? QStringLiteral("展开日志") : QStringLiteral("收起日志"));
    }
    if (ui->tableWidget != nullptr) {
        ui->tableWidget->setVisible(!compact);
    }
    if (canAutoSaveCheckBox != nullptr) {
        canAutoSaveCheckBox->setVisible(!compact);
    }
    if (ui->checkBox_4 != nullptr) {
        ui->checkBox_4->setVisible(!compact);
    }
    if (show0x207LogCheck != nullptr) {
        show0x207LogCheck->setVisible(!compact);
    }
    if (ui->cleanListBtn != nullptr) {
        ui->cleanListBtn->setVisible(!compact);
    }
    if (saveCanLogButton != nullptr) {
        saveCanLogButton->setVisible(!compact);
    }

    if (ui->groupBox_3 != nullptr) {
        ui->groupBox_3->setMinimumHeight(compact ? 118 : 220);
        ui->groupBox_3->setMaximumHeight(compact ? 140 : QWIDGETSIZE_MAX);
    }
    if (mainVerticalSplitter != nullptr) {
        if (compact) {
            const int totalHeight = qMax(600, mainVerticalSplitter->height());
            mainVerticalSplitter->setSizes(QList<int>() << qMax(480, totalHeight - 128) << 128);
        } else {
            const AppConfigData config = appConfig.load();
            mainVerticalSplitter->setSizes(QList<int>() << config.mainTopHeight << config.mainLogHeight);
        }
    }
}

void MainWindow::applyLayoutPreset(int preset)
{
    if (layoutPresetCombo != nullptr && layoutPresetCombo->currentIndex() != preset) {
        layoutPresetCombo->blockSignals(true);
        layoutPresetCombo->setCurrentIndex(qBound(0, preset, layoutPresetCombo->count() - 1));
        layoutPresetCombo->blockSignals(false);
    }

    if (mainVerticalSplitter != nullptr) {
        if (preset == 0) {
            applyCanLogCompact(true);
            mainVerticalSplitter->setSizes(QList<int>() << 720 << 128);
        } else if (preset == 2) {
            applyCanLogCompact(false);
            mainVerticalSplitter->setSizes(QList<int>() << 720 << 320);
        } else if (preset == 1) {
            applyCanLogCompact(false);
            mainVerticalSplitter->setSizes(QList<int>() << 620 << 240);
        }
    }

    if (testCaseSplitter != nullptr) {
        if (preset == 0) {
            testCaseSplitter->setSizes(QList<int>() << 440 << 760);
        } else if (preset == 2) {
            testCaseSplitter->setSizes(QList<int>() << 680 << 980);
        } else if (preset == 1) {
            testCaseSplitter->setSizes(QList<int>() << 520 << 720);
        }
    }
}

void MainWindow::openCanLogWindow()
{
    if (canLogWindow == nullptr) {
        canLogWindow = new CanLogWindow(nullptr);
        canLogWindow->setMaxRows(maxLogRows);
        canLogWindow->copyFromTable(ui->tableWidget);
        connect(canLogWindow, &QObject::destroyed, this, [this]() {
            canLogWindow = nullptr;
        });
    }
    canLogWindow->show();
    canLogWindow->raise();
    canLogWindow->activateWindow();
}

void MainWindow::syncCanLogWindowRows(const QVector<QStringList> &rows)
{
    if (canLogWindow != nullptr) {
        canLogWindow->setMaxRows(maxLogRows);
        canLogWindow->appendRows(rows);
    }
}

void MainWindow::restoreLayoutConfig(const AppConfigData &config)
{
    if (layoutPresetCombo != nullptr) {
        layoutPresetCombo->blockSignals(true);
        layoutPresetCombo->setCurrentIndex(qBound(0, config.layoutPreset, layoutPresetCombo->count() - 1));
        layoutPresetCombo->blockSignals(false);
    }
    if (mainVerticalSplitter != nullptr) {
        mainVerticalSplitter->setSizes(QList<int>() << config.mainTopHeight << config.mainLogHeight);
    }
    if (testCaseSplitter != nullptr) {
        testCaseSplitter->setSizes(QList<int>() << config.testCaseListWidth << config.testCaseDetailWidth);
    }
    applyCanLogCompact(config.canLogCompact);
}

void MainWindow::loadAppConfig()
{
    const AppConfigData config = appConfig.load();
    logDirectory = config.logDirectory;
    logService.setLogDirectory(config.logDirectory);
    logService.setCanAutoSaveEnabled(config.canAutoSaveCsv);
    stressTestService.setOutputDirectory(config.logDirectory);
    stressTestService.setAutoSaveEnabled(config.stressAutoSaveCsv);
    maxLogRows = config.maxLogRows;
    ui->deviceTypeCombo->setCurrentIndex(qBound(0, config.deviceTypeIndex, ui->deviceTypeCombo->count() - 1));
    ui->deviceIndexCombo->setCurrentIndex(qBound(0, config.deviceIndex, ui->deviceIndexCombo->count() - 1));
    ui->sendPathCombo->setCurrentIndex(qBound(0, config.defaultChannel, ui->sendPathCombo->count() - 1));
    ui->resistanceCheckBox->setChecked(config.resistanceEnabled);
    if (rfidControlEnabledCheck != nullptr) {
        rfidControlEnabledCheck->setChecked(config.rfidControlEnabled);
    }
    if (show0x207LogCheck != nullptr) {
        show0x207LogCheck->setChecked(config.show0x207Log);
    }
    if (rfidScanPeriodSpin != nullptr) {
        rfidScanPeriodSpin->setValue(qBound(1, config.scanPeriod10ms, 255) * 10);
    }
    if (stressAutoSaveCheckBox != nullptr) {
        stressAutoSaveCheckBox->blockSignals(true);
        stressAutoSaveCheckBox->setChecked(config.stressAutoSaveCsv);
        stressAutoSaveCheckBox->blockSignals(false);
    }
    if (stressAutoExportSummaryCheckBox != nullptr) {
        stressAutoExportSummaryCheckBox->setChecked(config.stressAutoExportSummary);
    }
    if (stressDurationSecondsSpin != nullptr) {
        stressDurationSecondsSpin->setValue(qBound(0, config.stressDurationSeconds, 24 * 60 * 60));
    }
    if (stressTargetSamplesSpin != nullptr) {
        stressTargetSamplesSpin->setValue(qBound(0, config.stressTargetSamples, 100000000));
    }
    if (canAutoSaveCheckBox != nullptr) {
        canAutoSaveCheckBox->blockSignals(true);
        canAutoSaveCheckBox->setChecked(config.canAutoSaveCsv);
        canAutoSaveCheckBox->blockSignals(false);
    }
    if (autoCompactLogOnTestExecutionCheck != nullptr) {
        autoCompactLogOnTestExecutionCheck->blockSignals(true);
        autoCompactLogOnTestExecutionCheck->setChecked(config.autoCompactLogOnTestExecution);
        autoCompactLogOnTestExecutionCheck->blockSignals(false);
    }
    if (qjAutoWritePwdCheckBox != nullptr) {
        qjAutoWritePwdCheckBox->blockSignals(true);
        qjAutoWritePwdCheckBox->setChecked(config.qjAutoWritePwd);
        qjAutoWritePwdCheckBox->blockSignals(false);
    }
    if (qjTargetDeviceCombo != nullptr) {
        qjTargetDeviceCombo->blockSignals(true);
        int idx = qjTargetDeviceCombo->findData(config.qjTargetDevice);
        if (idx >= 0) {
            qjTargetDeviceCombo->setCurrentIndex(idx);
        }
        qjTargetDeviceCombo->blockSignals(false);
    }
    if (qjQueryModeCombo != nullptr) {
        qjQueryModeCombo->blockSignals(true);
        qjQueryModeCombo->setCurrentIndex(qBound(0, config.qjQueryMode, qjQueryModeCombo->count() - 1));
        qjQueryModeCombo->blockSignals(false);
    }
    if (qjHostPollPeriodSpin != nullptr) {
        qjHostPollPeriodSpin->blockSignals(true);
        qjHostPollPeriodSpin->setValue(qBound(100, config.qjHostPollIntervalMs, 10000));
        qjHostPollPeriodSpin->blockSignals(false);
    }
    if (qingjuRfidService != nullptr) {
        qingjuRfidService->setAutoWritePassword(config.qjAutoWritePwd);
        qingjuRfidService->setTargetAddress(config.qjTargetDevice);
        
        bool isNpk = (config.qjTargetDevice == 0x0A);
        if (qjRfidAddrValue != nullptr) {
            qjRfidAddrValue->setText(QString("0x%1").arg(config.qjTargetDevice, 2, 16, QChar('0')).toUpper());
        }
        if (qjAutoWritePwdCheckBox != nullptr) {
            qjAutoWritePwdCheckBox->setEnabled(isNpk);
        }
        if (qjRfidAssetGroup != nullptr) {
            qjRfidAssetGroup->setEnabled(isNpk);
        }
        if (qjRfidResultValue != nullptr) qjRfidResultValue->setEnabled(isNpk);
        if (qjRfidAlarmValue != nullptr) qjRfidAlarmValue->setEnabled(isNpk);
        if (qjRfidUidValue != nullptr) qjRfidUidValue->setEnabled(isNpk);
        if (qjRfidPwdValue != nullptr) qjRfidPwdValue->setEnabled(isNpk);
    }

    // 恢复 485 配置
    if (serialPortCombo != nullptr) {
        updateRs485Ports(); // 刷新可用串口列表
        int idx = serialPortCombo->findText(config.serialPortName);
        if (idx >= 0) {
            serialPortCombo->setCurrentIndex(idx);
        }
    }
    if (serialBaudRateCombo != nullptr) {
        int idx = serialBaudRateCombo->findData(config.serialBaudRate);
        if (idx >= 0) {
            serialBaudRateCombo->setCurrentIndex(idx);
        }
    }
    if (bbQueryModeCombo != nullptr) {
        bbQueryModeCombo->setCurrentIndex(qBound(0, config.rs485QueryMode, bbQueryModeCombo->count() - 1));
    }
    if (bbHostPollPeriodSpin != nullptr) {
        bbHostPollPeriodSpin->setValue(config.rs485PollIntervalMs);
    }
    if (bbPowerSpin != nullptr) {
        bbPowerSpin->setValue(config.bbPower);
    }
    if (ffQueryModeCombo != nullptr) {
        ffQueryModeCombo->setCurrentIndex(qBound(0, config.rs485QueryMode, ffQueryModeCombo->count() - 1));
    }
    if (ffHostPollPeriodSpin != nullptr) {
        ffHostPollPeriodSpin->setValue(config.rs485PollIntervalMs);
    }
    if (ffPowerSpin != nullptr) {
        ffPowerSpin->setValue(config.ffPower);
    }

    if (protocolModeCombo != nullptr) {
        protocolModeCombo->setCurrentIndex(qBound(0, config.protocolMode, 4));
        onProtocolModeChanged(protocolModeCombo->currentIndex());
    }

    if (hlQueryModeCombo != nullptr) {
        hlQueryModeCombo->setCurrentIndex(qBound(0, config.rs485QueryMode, hlQueryModeCombo->count() - 1));
    }
    if (hlHostPollPeriodSpin != nullptr) {
        hlHostPollPeriodSpin->setValue(config.rs485PollIntervalMs);
    }
    if (hlScanTimeSpin != nullptr) {
        hlScanTimeSpin->setValue(config.hlScanTimeMs == -1 ? 2147483647 : config.hlScanTimeMs);
    }
    if (hlScanIntervalSpin != nullptr) {
        hlScanIntervalSpin->setValue(config.hlScanIntervalMs);
    }
    if (hlSavedCountSpin != nullptr) {
        hlSavedCountSpin->setValue(config.hlSavedTagCount);
    }
    if (hlClearAfterReadCheck != nullptr) {
        hlClearAfterReadCheck->setChecked(config.hlClearAfterRead);
    }
    if (hlDecryptEnableCheck != nullptr) {
        hlDecryptEnableCheck->setChecked(config.hlDecryptEnable);
    }
    syncHlConfigToService();
    restoreLayoutConfig(config);

    logService.logRuntime(LogLevel::Info, QStringLiteral("Application started"));
}

void MainWindow::saveAppConfig()
{
    AppConfigData config = appConfig.load();
    config.deviceTypeIndex = ui->deviceTypeCombo->currentIndex();
    config.deviceIndex = ui->deviceIndexCombo->currentIndex();
    config.defaultChannel = ui->sendPathCombo->currentIndex();
    config.resistanceEnabled = ui->resistanceCheckBox->isChecked();
    if (rfidControlEnabledCheck != nullptr) {
        config.rfidControlEnabled = rfidControlEnabledCheck->isChecked();
    }
    if (show0x207LogCheck != nullptr) {
        config.show0x207Log = show0x207LogCheck->isChecked();
    }
    if (canAutoSaveCheckBox != nullptr) {
        config.canAutoSaveCsv = canAutoSaveCheckBox->isChecked();
    }
    if (rfidScanPeriodSpin != nullptr) {
        config.scanPeriod10ms = rfidScanPeriodSpin->value() / 10;
    }
    if (stressAutoSaveCheckBox != nullptr) {
        config.stressAutoSaveCsv = stressAutoSaveCheckBox->isChecked();
    }
    if (stressAutoExportSummaryCheckBox != nullptr) {
        config.stressAutoExportSummary = stressAutoExportSummaryCheckBox->isChecked();
    }
    if (stressDurationSecondsSpin != nullptr) {
        config.stressDurationSeconds = stressDurationSecondsSpin->value();
    }
    if (stressTargetSamplesSpin != nullptr) {
        config.stressTargetSamples = stressTargetSamplesSpin->value();
    }
    if (protocolModeCombo != nullptr) {
        config.protocolMode = protocolModeCombo->currentIndex();
    }
    if (qjAutoWritePwdCheckBox != nullptr) {
        config.qjAutoWritePwd = qjAutoWritePwdCheckBox->isChecked();
    }
    if (qjTargetDeviceCombo != nullptr) {
        config.qjTargetDevice = qjTargetDeviceCombo->currentData().toInt();
    }
    if (qjQueryModeCombo != nullptr) {
        config.qjQueryMode = qjQueryModeCombo->currentIndex();
    }
    if (qjHostPollPeriodSpin != nullptr) {
        config.qjHostPollIntervalMs = qjHostPollPeriodSpin->value();
    }

    if (serialPortCombo != nullptr) {
        config.serialPortName = serialPortCombo->currentText();
    }
    if (serialBaudRateCombo != nullptr) {
        config.serialBaudRate = serialBaudRateCombo->currentData().toInt();
    }
    if (protocolModeCombo != nullptr) {
        int idx = protocolModeCombo->currentIndex();
        if (idx == 2) { // BB
            if (bbQueryModeCombo != nullptr) {
                config.rs485QueryMode = bbQueryModeCombo->currentIndex();
            }
            if (bbHostPollPeriodSpin != nullptr) {
                config.rs485PollIntervalMs = bbHostPollPeriodSpin->value();
            }
            if (bbPowerSpin != nullptr) {
                config.bbPower = bbPowerSpin->value();
            }
        } else if (idx == 3) { // FF
            if (ffQueryModeCombo != nullptr) {
                config.rs485QueryMode = ffQueryModeCombo->currentIndex();
            }
            if (ffHostPollPeriodSpin != nullptr) {
                config.rs485PollIntervalMs = ffHostPollPeriodSpin->value();
            }
            if (ffPowerSpin != nullptr) {
                config.ffPower = ffPowerSpin->value();
            }
        } else if (idx == 4) { // Hellobike
            if (hlQueryModeCombo != nullptr) {
                config.rs485QueryMode = hlQueryModeCombo->currentIndex();
            }
            if (hlHostPollPeriodSpin != nullptr) {
                config.rs485PollIntervalMs = hlHostPollPeriodSpin->value();
            }
            if (hlScanTimeSpin != nullptr) {
                config.hlScanTimeMs = hlScanTimeSpin->value() == 2147483647 ? -1 : hlScanTimeSpin->value();
            }
            if (hlScanIntervalSpin != nullptr) {
                config.hlScanIntervalMs = hlScanIntervalSpin->value();
            }
            if (hlSavedCountSpin != nullptr) {
                config.hlSavedTagCount = hlSavedCountSpin->value();
            }
            if (hlClearAfterReadCheck != nullptr) {
                config.hlClearAfterRead = hlClearAfterReadCheck->isChecked();
            }
            if (hlDecryptEnableCheck != nullptr) {
                config.hlDecryptEnable = hlDecryptEnableCheck->isChecked();
            }
        }
    }

    if (mainVerticalSplitter != nullptr && !canLogCompact) {
        const QList<int> sizes = mainVerticalSplitter->sizes();
        if (sizes.size() >= 2 && sizes.at(0) > 0 && sizes.at(1) > 0) {
            config.mainTopHeight = sizes.at(0);
            config.mainLogHeight = sizes.at(1);
        }
    }
    config.canLogCompact = canLogCompact;
    if (testCaseSplitter != nullptr) {
        const QList<int> sizes = testCaseSplitter->sizes();
        if (sizes.size() >= 2 && sizes.at(0) > 0 && sizes.at(1) > 0) {
            config.testCaseListWidth = sizes.at(0);
            config.testCaseDetailWidth = sizes.at(1);
        }
    }
    if (layoutPresetCombo != nullptr) {
        config.layoutPreset = layoutPresetCombo->currentIndex();
    }
    if (autoCompactLogOnTestExecutionCheck != nullptr) {
        config.autoCompactLogOnTestExecution = autoCompactLogOnTestExecutionCheck->isChecked();
    }

    config.logDirectory = logDirectory;
    appConfig.save(config);
    logService.logRuntime(LogLevel::Info, QStringLiteral("Application settings saved"));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    const bool stressRunning = stressTestService.stats().running;
    const bool otaRunning = isOtaRunning();
    const bool productionRunning = productionTestService.isRunning();
    const bool diagnosticWriteRunning = rfidDiagnosticTransfer.isBusy();

    if (stressRunning || otaRunning || productionRunning || diagnosticWriteRunning) {
        QString taskName = QStringLiteral("诊断写入");
        if (otaRunning) {
            taskName = QStringLiteral("OTA固件升级");
        } else if (stressRunning) {
            taskName = QStringLiteral("压力测试");
        } else if (productionRunning) {
            taskName = QStringLiteral("产线检测");
        }
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            QStringLiteral("警告"),
            QStringLiteral("当前正在进行%1，关闭程序将中止该操作，是否确认关闭？").arg(taskName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (reply != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }

    if (otaRunning) {
        int protocolMode = appConfig.load().protocolMode;
        if (protocolMode == 1) {
            qingjuOtaService->abortUpgrade();
        } else if (protocolMode == 4) {
            emit requestHlOtaAbortUpgrade();
        } else if (protocolMode == 2 || protocolMode == 3) {
            emit requestBbFfOtaAbortUpgrade();
        } else {
            otaService.abortUpgrade();
        }
    }
    if (stressRunning) {
        rfidScanning = false;
        stressTestService.stop();
    }
    if (productionRunning) {
        productionTestService.stop(QStringLiteral("程序关闭"));
    }
    if (diagnosticWriteRunning) {
        abortRfidDiagnosticTransferSilently();
    }

    // 关闭 485
    emit requestRs485StopScan();
    emit requestRs485ClosePort();

    if (canLogWindow != nullptr) {
        canLogWindow->close();
        canLogWindow = nullptr;
    }

    saveAppConfig();
    logService.logRuntime(LogLevel::Info, QStringLiteral("Application closing"));
    stressRefreshTimer->stop();
    canthread->stop();
    canthread->wait();
    canthread->closeDevice();
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow()
{
    if (rs485Thread != nullptr) {
        if (rs485Thread->isRunning() && rs485Worker != nullptr) {
            QMetaObject::invokeMethod(rs485Worker, "shutdown", Qt::BlockingQueuedConnection);
            rs485Worker->deleteLater();
            rs485Thread->quit();
            rs485Thread->wait();
            rs485Worker = nullptr;
        } else {
            delete rs485Worker;
            rs485Worker = nullptr;
        }
    }
    delete ui;
}

void MainWindow::handleRecvedFrames(const QVector<CanFrame> &frames)
{
    for(const CanFrame &frame : frames)
    {
        if (appConfig.load().protocolMode == 1) { // 青桔协议
            if (frame.extendedFrame) {
                QingjuCanId qjId = QingjuCanId::parse(frame.id);
                if (qjId.srcAddr == 0x0A) {
                    lastQingjuNpkFrameTime = QDateTime::currentDateTime();
                    lastRfidFrameTime = lastQingjuNpkFrameTime;
                } else if (qjId.srcAddr == 0x0B) {
                    lastQingjuRfrFrameTime = QDateTime::currentDateTime();
                    lastRfidFrameTime = QDateTime::currentDateTime();
                }
                qingjuCanManager->handleIncomingFrame(frame);
                updateQingjuOnlineStatus(false);
            }
        } else { // 美团协议
            if ((frame.id >= 0x2C0 && frame.id <= 0x2DF) || frame.id == 0x107) {
                lastRfidFrameTime = QDateTime::currentDateTime();
            }

            handleRfidFrame(frame);

            if (frame.id == 0x107) {
                otaService.handleIncomingFrame(frame);
            }

            if (stressTestService.handleFrame(frame)) {
                updateStressTestPanel(stressTestService.stats());
            }
        }

        addCanFrameToList(frame);
        appendTestEvidenceFrame(frame, protocolDecodeText(frame));
    }
}

void MainWindow::on_cleanListBtn_clicked()
{
    pendingLogRows.clear();
    logFlushTimer->stop();
    ui->tableWidget->setRowCount(0);
    if (canLogWindow != nullptr) {
        canLogWindow->clearRows();
    }
}

void MainWindow::on_openDeviceBtn_clicked()
{
    const int deviceTypeIndex = qBound(0, ui->deviceTypeCombo->currentIndex(), DeviceTypeNames.size() - 1);
    if(canthread->openDevice(DeviceTypeIndexes[deviceTypeIndex],ui->deviceIndexCombo->currentIndex(),0))
    {
        logService.logRuntime(LogLevel::Info, QStringLiteral("CAN device opened"));
        updateCanControlState(true, false, false);
    }
    else
    {
        logService.logRuntime(LogLevel::Error, QStringLiteral("Failed to open CAN device"));
        QMessageBox::warning(this,"警告","设备打开失败！");
    }
}

void MainWindow::on_initCANBtn_clicked()
{
    if(!canthread->setClassicBaudrate(500000))
    {
        logService.logRuntime(LogLevel::Error, QStringLiteral("Failed to set classic CAN baudrate: 500000"));
        QMessageBox::warning(this,"警告","设置普通 CAN 500k 波特率失败！");
        return;
    }
    logService.logRuntime(LogLevel::Info, QStringLiteral("Classic CAN baudrate set: 500000"));
    if (afterReSet()) {
        updateCanControlState(true, true, false);
    } else if (topCanStatusValue != nullptr) {
        topCanStatusValue->setText(QStringLiteral("CAN：初始化失败"));
    }
}

void MainWindow::on_closeDeviceBtn_clicked()
{
    canthread->stop();
    canthread->wait();
    canthread->closeDevice();
    logService.logRuntime(LogLevel::Info, QStringLiteral("CAN device closed"));
    ui->initCANBtn->setEnabled(false);
    ui->StartCANBtn->setEnabled(false);
    ui->reSetCANBtn->setEnabled(false);
    ui->closeDeviceBtn->setEnabled(false);
    ui->sendBtn->setEnabled(false);
    ui->openDeviceBtn->setEnabled(true);
    updateCanControlState(false, false, false);
}

void MainWindow::on_StartCANBtn_clicked()
{
    if(!canthread->startCAN())
    {
        logService.logRuntime(LogLevel::Error, QStringLiteral("Failed to start CAN"));
        QMessageBox::warning(this,"警告","CAN启动失败！");
        return;
    }
    logService.logRuntime(LogLevel::Info, QStringLiteral("CAN started"));
    updateCanControlState(true, true, true);
    canthread->start();
}

bool MainWindow::afterReSet()
{
    if(!canthread->initClassicCAN())
    {
        logService.logRuntime(LogLevel::Error, QStringLiteral("Failed to initialize classic CAN"));
        QMessageBox::warning(this,"警告","CAN初始化失败！");
        return false;
    }
    if(!canthread->setResistanceEnable(ui->resistanceCheckBox->checkState() ? 1: 0))
    {
        logService.logRuntime(LogLevel::Error, QStringLiteral("Failed to set terminal resistance"));
        QMessageBox::warning(this,"警告","使使能终端电阻失败！");
        return false;
    }
    if(ui->filterModeCombo->currentIndex() != 2)
    {
        if(!canthread->setFilter(ui->filterModeCombo->currentIndex(),"0x" + ui->StartIDEdit->text(),"0x" + ui->endIDEdit->text()))
        {
            QMessageBox::warning(this,"警告","设置过滤波失败！");
            logService.logRuntime(LogLevel::Error, QStringLiteral("Failed to set CAN filter"));
            return false;
        }
    }
    logService.logRuntime(LogLevel::Info, QStringLiteral("Classic CAN initialized"));
    ui->initCANBtn->setEnabled(false);
    ui->StartCANBtn->setEnabled(true);
    return true;
}

void MainWindow::on_reSetCANBtn_clicked()
{
    canthread->stop();
    canthread->wait();
    if(!canthread->reSetCAN())
    {
        logService.logRuntime(LogLevel::Error, QStringLiteral("Failed to reset CAN"));
        QMessageBox::warning(this,"警告","CAN复位失败！");
        return;
    }
    logService.logRuntime(LogLevel::Info, QStringLiteral("CAN reset"));
    ui->initCANBtn->setEnabled(true);
    ui->StartCANBtn->setEnabled(false);
    ui->reSetCANBtn->setEnabled(false);
    ui->sendBtn->setEnabled(false);
    updateCanControlState(true, false, false);
}

void MainWindow::on_sendBtn_clicked()
{
    const int protocolMode = protocolModeCombo != nullptr ? protocolModeCombo->currentIndex() : 0;
    if (protocolMode == 2 || protocolMode == 3 || protocolMode == 4) {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("发送失败，串口未打开！"));
            return;
        }

        QStringList byteTextList = ui->sendDataEdit->text().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (byteTextList.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("发送失败，串口数据不能为空！"));
            return;
        }

        QByteArray payload;
        payload.reserve(byteTextList.count());
        for (int index = 0; index < byteTextList.count(); ++index) {
            bool byteOk = false;
            const int byteValue = byteTextList.at(index).toInt(&byteOk, 16);
            if (!byteOk || byteValue < 0 || byteValue > 0xFF) {
                QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("发送失败，第%1个串口字节不是有效十六进制值！").arg(index + 1));
                return;
            }
            payload.append(static_cast<char>(byteValue));
        }

        emit requestRs485SendRawData(payload);
        return;
    }

    bool idOk = false;
    const UINT canId = ui->sendIDEdit->text().trimmed().toUInt(&idOk, 16);
    if (!idOk) {
        QMessageBox::warning(this, QStringLiteral("\u8b66\u544a"), QStringLiteral("\u53d1\u9001\u5931\u8d25\uff0cID\u683c\u5f0f\u65e0\u6548\uff01"));
        return;
    }
    if (ui->frameTypeCombo->currentIndex() == 0 && canId > 0x7FF) {
        QMessageBox::warning(this, QStringLiteral("\u8b66\u544a"), QStringLiteral("\u53d1\u9001\u5931\u8d25\uff0c\u6807\u51c6\u5e27ID\u8303\u56f4\u4e3a0~0x7FF\uff01"));
        return;
    }
    if (ui->frameTypeCombo->currentIndex() != 0 && canId > 0x1FFFFFFF) {
        QMessageBox::warning(this, QStringLiteral("\u8b66\u544a"), QStringLiteral("\u53d1\u9001\u5931\u8d25\uff0c\u6269\u5c55\u5e27ID\u8303\u56f4\u4e3a0~0x1FFFFFFF\uff01"));
        return;
    }

    QStringList byteTextList = ui->sendDataEdit->text().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (byteTextList.count() > MaxManualPayloadBytes) {
        QMessageBox::warning(this, QStringLiteral("\u8b66\u544a"), QStringLiteral("\u53d1\u9001\u5931\u8d25\uff0c\u6570\u636e\u957f\u5ea6\u4e0d\u80fd\u8d85\u8fc764\u5b57\u8282\uff01"));
        return;
    }

    QByteArray payload(MaxManualPayloadBytes, 0);
    for (int index = 0; index < byteTextList.count(); ++index) {
        bool byteOk = false;
        const int byteValue = byteTextList.at(index).toInt(&byteOk, 16);
        if (!byteOk || byteValue < 0 || byteValue > 0xFF) {
            QMessageBox::warning(this, QStringLiteral("\u8b66\u544a"), QStringLiteral("\u53d1\u9001\u5931\u8d25\uff0c\u7b2c%1\u4e2a\u6570\u636e\u5b57\u8282\u4e0d\u662f\u6709\u6548\u5341\u516d\u8fdb\u5236\u503c\uff01").arg(index + 1));
            return;
        }
        payload[index] = static_cast<char>(byteValue);
    }

    UINT dlc = 0;
    if (ui->protocolCombo->currentIndex() == 0) {
        dlc = static_cast<UINT>(qMin(byteTextList.count(), 8));
    } else {
        const int byteCount = byteTextList.count();
        if (byteCount <= 8) dlc = static_cast<UINT>(byteCount);
        else if (byteCount <= 12) dlc = 12;
        else if (byteCount <= 16) dlc = 16;
        else if (byteCount <= 20) dlc = 20;
        else if (byteCount <= 24) dlc = 24;
        else if (byteCount <= 32) dlc = 32;
        else if (byteCount <= 48) dlc = 48;
        else dlc = 64;
    }

    if (canthread->sendData(canId,
                            ui->frameTypeCombo->currentIndex(),
                            ui->protocolCombo->currentIndex(),
                            (ui->CANFDaccCheck->checkState() ? 1 : 0),
                            ui->sendPathCombo->currentIndex(),
                            payload.constData(),
                            dlc)) {
        CanFrame frame;
        frame.id = canId;
        frame.channel = static_cast<quint32>(ui->sendPathCombo->currentIndex());
        frame.data = payload.left(static_cast<int>(dlc));
        frame.direction = CanFrameDirection::Tx;
        frame.protocol = (ui->protocolCombo->currentIndex() == 0) ? CanFrameProtocol::ClassicCan : CanFrameProtocol::CanFd;
        frame.extendedFrame = ui->frameTypeCombo->currentIndex() != 0;
        frame.remoteFrame = false;
        frame.hostDateTime = QDateTime::currentDateTime();
        addCanFrameToList(frame);
    }
}

void MainWindow::AddDataToList(QStringList strList)
{
    if (strList.isEmpty()) {
        return;
    }
    pendingLogRows.append(strList);
    const int overflow = pendingLogRows.size() - maxLogRows;
    if (overflow > 0) {
        pendingLogRows.remove(0, overflow);
    }
    if (!logFlushTimer->isActive()) {
        logFlushTimer->start();
    }
}

void MainWindow::flushPendingLogRows()
{
    if (pendingLogRows.isEmpty()) {
        return;
    }

    QScrollBar *vBar = ui->tableWidget->verticalScrollBar();
    bool wasAtBottom = (vBar == nullptr || vBar->value() == vBar->maximum());

    const int overflow = ui->tableWidget->rowCount() + pendingLogRows.size() - maxLogRows;
    ui->tableWidget->setUpdatesEnabled(false);
    const int rowsToRemove = overflow > 0
        ? qMin(ui->tableWidget->rowCount(), qMax(overflow, LogPruneBatchRows))
        : 0;
    for (int i = 0; i < rowsToRemove; ++i) {
        ui->tableWidget->removeRow(0);
    }

    const int firstRow = ui->tableWidget->rowCount();
    ui->tableWidget->setRowCount(firstRow + pendingLogRows.size());
    for (int rowOffset = 0; rowOffset < pendingLogRows.size(); ++rowOffset) {
        const QStringList &strList = pendingLogRows.at(rowOffset);
        const int row = firstRow + rowOffset;
        for (int column = 0; column < strList.count(); ++column) {
            QTableWidgetItem *item = new QTableWidgetItem(strList.at(column), 0);
            ui->tableWidget->setItem(row, column, item);
            if (column != strList.count() - 1) {
                item->setTextAlignment(Qt::AlignCenter | Qt::AlignHCenter);
            }
        }
    }
    syncCanLogWindowRows(pendingLogRows);
    pendingLogRows.clear();
    ui->tableWidget->setUpdatesEnabled(true);
    if (wasAtBottom) {
        ui->tableWidget->scrollToBottom();
    }
}

void MainWindow::updateOtaStressUI()
{
    if (otaCurrentCycleLabel == nullptr) return;

    if (m_otaStressRunning) {
        otaCurrentCycleLabel->setText(QString("%1 / %2").arg(m_otaCurrentCycle).arg(m_otaTargetCycles));
    } else {
        otaCurrentCycleLabel->setText(QString("0 / %1").arg(otaStressCyclesSpin->value()));
    }

    otaSuccessCyclesLabel->setText(QString::number(m_otaSuccessCount));
    otaFailureCyclesLabel->setText(QString::number(m_otaFailureCount));

    int totalCompleted = m_otaSuccessCount + m_otaFailureCount;
    double successRate = 0.0;
    if (totalCompleted > 0) {
        successRate = (static_cast<double>(m_otaSuccessCount) / totalCompleted) * 100.0;
    }
    otaStressSuccessRateLabel->setText(QString("%1%").arg(successRate, 0, 'f', 2));
    otaLastFailureReasonLabel->setText(m_otaLastFailureReason.isEmpty() ? "-" : m_otaLastFailureReason);

    // 在运行测试期间，禁用相关压力测试参数配置
    bool configEnabled = !m_otaStressRunning;
    if (otaStressTestEnabledCheck != nullptr) otaStressTestEnabledCheck->setEnabled(configEnabled);
    if (otaStressCyclesSpin != nullptr) otaStressCyclesSpin->setEnabled(configEnabled);
    if (otaCooldownSpin != nullptr) otaCooldownSpin->setEnabled(configEnabled);
    if (otaStressSuspendLogCheck != nullptr) otaStressSuspendLogCheck->setEnabled(configEnabled);
}

void MainWindow::handleOtaStateChangeForStressTest(OtaService::State state, const QString &message)
{
    if (!m_otaStressRunning) return;

    if (state == OtaService::State::Completed) {
        m_otaSuccessCount++;
        updateOtaStressUI();

        if (m_otaCurrentCycle >= m_otaTargetCycles) {
            // 测试结束（成功）
            m_otaStressRunning = false;
            m_otaCooldownTimer->stop();
            ui->checkBox_4->setChecked(m_originalLogEnabled);
            updateOtaStressUI();
            logService.logRuntime(LogLevel::Info, QString("OTA stress test completed successfully. Total cycles: %1").arg(m_otaTargetCycles));
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("OTA 升级压力测试全部成功完成！"));
        } else {
            // 开始倒计时冷却，进入下一轮
            int cooldownMs = otaCooldownSpin != nullptr ? otaCooldownSpin->value() : 2000;
            otaMessageValue->setText(QStringLiteral("第 %1 轮升级成功。冷却中，等待 %2 ms 发起下一轮...").arg(m_otaCurrentCycle).arg(cooldownMs));
            m_otaCooldownTimer->start(cooldownMs);
        }
    }
    else if (state == OtaService::State::Failed) {
        m_otaFailureCount++;
        m_otaLastFailureReason = message;
        updateOtaStressUI();

        if (m_otaCurrentCycle >= m_otaTargetCycles) {
            // 测试结束（包含失败记录）
            m_otaStressRunning = false;
            m_otaCooldownTimer->stop();
            ui->checkBox_4->setChecked(m_originalLogEnabled);
            updateOtaStressUI();
            logService.logRuntime(LogLevel::Warning, QString("OTA stress test finished with failures. Success: %1, Failure: %2, Last error: %3")
                                  .arg(m_otaSuccessCount).arg(m_otaFailureCount).arg(message));
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("OTA 升级压力测试已完成，但存在失败记录。"));
        } else {
            // 开始倒计时冷却，进入下一轮
            int cooldownMs = otaCooldownSpin != nullptr ? otaCooldownSpin->value() : 2000;
            otaMessageValue->setText(QStringLiteral("第 %1 轮升级失败。冷却中，等待 %2 ms 发起下一轮...").arg(m_otaCurrentCycle).arg(cooldownMs));
            m_otaCooldownTimer->start(cooldownMs);
        }
    }
    else if (state == OtaService::State::Abort) {
        m_otaStressRunning = false;
        m_otaCooldownTimer->stop();
        ui->checkBox_4->setChecked(m_originalLogEnabled);
        updateOtaStressUI();
    }
}

void MainWindow::handleHlOtaStateChanged(HlOtaService::State state, const QString &message)
{
    hlOtaState = state;
    hlOtaMessage = message;
    if (appConfig.load().protocolMode == 4) {
        if (otaStateValue != nullptr) {
            otaStateValue->setText(hlOtaStateText());
        }
        if (otaMessageValue != nullptr) {
            otaMessageValue->setText(message);
        }
        if (state == HlOtaService::State::Completed) {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("OTA 升级成功完成！"));
        } else if (state == HlOtaService::State::Failed) {
            QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("OTA 升级失败: %1").arg(message));
        }
        updateControlsState();
    }
}

void MainWindow::handleBbFfOtaStateChanged(BbFfOtaService::State state, const QString &message)
{
    bbFfOtaState = state;
    bbFfOtaMessage = message;
    int mode = appConfig.load().protocolMode;
    if (mode == 2 || mode == 3) {
        if (otaStateValue != nullptr) {
            otaStateValue->setText(bbFfOtaStateText());
        }
        if (otaMessageValue != nullptr) {
            otaMessageValue->setText(message);
        }
        if (state == BbFfOtaService::State::Completed) {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("OTA 升级成功完成！"));
        } else if (state == BbFfOtaService::State::Failed) {
            QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("OTA 升级失败: %1").arg(message));
        }
        updateControlsState();
    }
}

QString MainWindow::hlOtaStateText() const
{
    switch (hlOtaState) {
    case HlOtaService::State::Idle:
        return QStringLiteral("空闲");
    case HlOtaService::State::QueryProgram:
        return QStringLiteral("查询程序");
    case HlOtaService::State::StartUpgrade:
        return QStringLiteral("开始升级");
    case HlOtaService::State::SendData:
        return QStringLiteral("发送数据");
    case HlOtaService::State::FinishUpgrade:
        return QStringLiteral("完成升级");
    case HlOtaService::State::Abort:
        return QStringLiteral("已中止");
    case HlOtaService::State::Failed:
        return QStringLiteral("失败");
    case HlOtaService::State::Completed:
        return QStringLiteral("完成");
    }
    return QStringLiteral("未知");
}

QString MainWindow::bbFfOtaStateText() const
{
    switch (bbFfOtaState) {
    case BbFfOtaService::State::Idle:
        return QStringLiteral("空闲");
    case BbFfOtaService::State::StartUpgrade:
        return QStringLiteral("开始升级");
    case BbFfOtaService::State::SendData:
        return QStringLiteral("发送数据");
    case BbFfOtaService::State::FinishUpgrade:
        return QStringLiteral("完成升级");
    case BbFfOtaService::State::Abort:
        return QStringLiteral("已中止");
    case BbFfOtaService::State::Failed:
        return QStringLiteral("失败");
    case BbFfOtaService::State::Completed:
        return QStringLiteral("完成");
    }
    return QStringLiteral("未知");
}

void MainWindow::handleQingjuOtaStateChangeForStressTest(QingjuOtaService::State state, const QString &message)
{
    if (!m_otaStressRunning) return;

    if (state == QingjuOtaService::State::Completed) {
        m_otaSuccessCount++;
    } else if (state == QingjuOtaService::State::Failed) {
        m_otaFailureCount++;
        m_otaLastFailureReason = message;
    } else if (state == QingjuOtaService::State::Abort) {
        m_otaStressRunning = false;
        m_otaCooldownTimer->stop();
        ui->checkBox_4->setChecked(m_originalLogEnabled);
        updateOtaStressUI();
        return;
    } else {
        return;
    }

    updateOtaStressUI();

    if (m_otaCurrentCycle >= m_otaTargetCycles) {
        m_otaStressRunning = false;
        m_otaCooldownTimer->stop();
        ui->checkBox_4->setChecked(m_originalLogEnabled);
        updateOtaStressUI();

        if (m_otaFailureCount == 0) {
            logService.logRuntime(LogLevel::Info, QString("Qingju OTA stress test completed successfully. Total cycles: %1").arg(m_otaTargetCycles));
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("青桔 OTA 升级压力测试全部成功完成。"));
        } else {
            logService.logRuntime(LogLevel::Warning, QString("Qingju OTA stress test finished with failures. Success: %1, Failure: %2, Last error: %3")
                                  .arg(m_otaSuccessCount).arg(m_otaFailureCount).arg(message));
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("青桔 OTA 升级压力测试已完成，但存在失败记录。"));
        }
        return;
    }

    const int cooldownMs = otaCooldownSpin != nullptr ? otaCooldownSpin->value() : 2000;
    otaMessageValue->setText(QStringLiteral("第 %1 轮青桔 OTA 已结束，冷却 %2 ms 后发起下一轮。").arg(m_otaCurrentCycle).arg(cooldownMs));
    m_otaCooldownTimer->start(cooldownMs);
}

void MainWindow::onOtaInjectMasterToggled(bool checked)
{
    if (otaInjectCrcErrorCheck != nullptr) otaInjectCrcErrorCheck->setEnabled(checked);
    if (otaInjectSeqErrorCheck != nullptr) otaInjectSeqErrorCheck->setEnabled(checked);
    if (otaInjectHwMismatchCheck != nullptr) otaInjectHwMismatchCheck->setEnabled(checked);
    if (otaInjectSilentTimeoutCheck != nullptr) otaInjectSilentTimeoutCheck->setEnabled(checked);
    if (otaInjectIgnoreFcCheck != nullptr) otaInjectIgnoreFcCheck->setEnabled(checked);
    if (otaInjectIsoTpSnCheck != nullptr) otaInjectIsoTpSnCheck->setEnabled(checked);
    if (otaInjectOutOfOrderCheck != nullptr) otaInjectOutOfOrderCheck->setEnabled(checked);
}

OtaErrorConfig MainWindow::getOtaErrorConfig() const
{
    OtaErrorConfig config;
    if (otaInjectMasterCheck != nullptr && otaInjectMasterCheck->isChecked()) {
        config.enabled = true;
        config.crcError = otaInjectCrcErrorCheck != nullptr && otaInjectCrcErrorCheck->isChecked();
        config.seqError = otaInjectSeqErrorCheck != nullptr && otaInjectSeqErrorCheck->isChecked();
        config.hwMismatch = otaInjectHwMismatchCheck != nullptr && otaInjectHwMismatchCheck->isChecked();
        config.silentTimeout = otaInjectSilentTimeoutCheck != nullptr && otaInjectSilentTimeoutCheck->isChecked();
        config.ignoreFcInterval = otaInjectIgnoreFcCheck != nullptr && otaInjectIgnoreFcCheck->isChecked();
        config.isoTpSnError = otaInjectIsoTpSnCheck != nullptr && otaInjectIsoTpSnCheck->isChecked();
        config.outOfOrderState = otaInjectOutOfOrderCheck != nullptr && otaInjectOutOfOrderCheck->isChecked();
    }
    return config;
}

QingjuOtaErrorConfig MainWindow::getQingjuOtaErrorConfig() const
{
    QingjuOtaErrorConfig config;
    if (qjOtaAnomalyEnableCheck != nullptr && qjOtaAnomalyEnableCheck->isChecked()) {
        config.enabled = true;
        config.caseMode = qjOtaAnomalyCombo != nullptr ? qjOtaAnomalyCombo->currentData().toInt() : 0;
    }
    return config;
}

quint8 MainWindow::selectedQingjuOtaTarget() const
{
    if (qjOtaTargetCombo == nullptr) {
        return 0x0B;
    }
    return static_cast<quint8>(qjOtaTargetCombo->currentData().toUInt());
}

void MainWindow::onProtocolModeChanged(int index)
{
    if (productionTestService.isRunning() && index != 0) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            QStringLiteral("产线检测运行中"),
            QStringLiteral("切换协议会停止当前产线检测，确认继续吗？"));
        if (answer != QMessageBox::Yes) {
            if (protocolModeCombo != nullptr) {
                protocolModeCombo->blockSignals(true);
                protocolModeCombo->setCurrentIndex(0);
                protocolModeCombo->blockSignals(false);
            }
            return;
        }
        productionTestService.stop(QStringLiteral("协议切换"));
        productionWritePending = false;
    }

    AppConfigData config = appConfig.load();
    config.protocolMode = index;
    appConfig.save(config);

    // 强制关闭所有通道，清理资源，达到物理通道的隔离！
    if (index == 0 || index == 1) { // CAN 模式下，强制停止 485 并关闭串口
        if (rs485Scanning) {
            emit requestRs485StopScan();
            rs485Scanning = false;
        }
        if (serialOpened) {
            emit requestRs485ClosePort();
            serialOpened = false;
            if (serialStatusLabel != nullptr) {
                serialStatusLabel->setText(QStringLiteral("串口已关闭"));
                serialStatusLabel->setStyleSheet("color: red; font-weight: bold;");
            }
            if (serialOpenCloseBtn != nullptr) {
                serialOpenCloseBtn->setText(QStringLiteral("打开串口"));
            }
        }
    } else { // 485 模式下，强制停止 CAN
        if (canStarted) {
            on_reSetCANBtn_clicked(); // 重置或关闭 CAN
        }
        if (deviceOpened) {
            on_closeDeviceBtn_clicked();
        }
    }

    if (index == 0 || index == 1) {
        updateCanControlState(deviceOpened, canInitialized, canStarted);
    }

    if (index == 0) { // 美团协议
        qingjuRfidService->stopScan();
        updateMeituanTopStatus();
        if (stressStatsStackedWidget != nullptr && mtStressPanel != nullptr) {
            stressStatsStackedWidget->setCurrentWidget(mtStressPanel);
        }
        if (rfidStackedWidget != nullptr && mtRfidPanel != nullptr) {
            rfidStackedWidget->setCurrentWidget(mtRfidPanel);
        }
        if (serialDevicePanel != nullptr) serialDevicePanel->hide();
        if (devicePanel != nullptr) devicePanel->show();
        if (rfidTabs != nullptr) rfidTabs->setTabEnabled(2, true);

        if (qjOtaAnomalyGroup != nullptr) qjOtaAnomalyGroup->hide();
        if (otaErrorInjectionGroup != nullptr) otaErrorInjectionGroup->show();
        if (otaStressGroup != nullptr) otaStressGroup->show();
        if (qjOtaTargetLabel != nullptr) qjOtaTargetLabel->hide();
        if (qjOtaTargetCombo != nullptr) qjOtaTargetCombo->hide();
        if (otaVersionLabel != nullptr) otaVersionLabel->hide();
        if (otaVersionEdit != nullptr) otaVersionEdit->hide();
        if (otaQueryBtn != nullptr) otaQueryBtn->setText(QStringLiteral("查询APP/BOOT"));
        if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setText(QStringLiteral("开始升级"));
        
        otaStateValue->setText(otaService.stateText());
        otaMessageValue->setText(otaService.lastMessage().isEmpty() ? QStringLiteral("点击“开始升级”或“查询APP/BOOT”启动") : otaService.lastMessage());
    } else if (index == 1) { // 青桔协议
        if (rfidStackedWidget != nullptr && qjRfidPanel != nullptr) {
            rfidStackedWidget->setCurrentWidget(qjRfidPanel);
        }
        if (stressStatsStackedWidget != nullptr && qjStressPanel != nullptr) {
            stressStatsStackedWidget->setCurrentWidget(qjStressPanel);
        }
        if (serialDevicePanel != nullptr) serialDevicePanel->hide();
        if (devicePanel != nullptr) devicePanel->show();
        if (rfidTabs != nullptr) rfidTabs->setTabEnabled(2, true);

        if (qjOtaAnomalyGroup != nullptr) qjOtaAnomalyGroup->show();
        if (otaErrorInjectionGroup != nullptr) otaErrorInjectionGroup->hide();
        if (otaStressGroup != nullptr) otaStressGroup->show();
        if (qjOtaTargetLabel != nullptr) qjOtaTargetLabel->show();
        if (qjOtaTargetCombo != nullptr) qjOtaTargetCombo->show();
        if (otaVersionLabel != nullptr) otaVersionLabel->hide();
        if (otaVersionEdit != nullptr) otaVersionEdit->hide();
        if (otaQueryBtn != nullptr) otaQueryBtn->setText(QStringLiteral("查询目标 APP/BOOT"));
        if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setText(QStringLiteral("开始目标升级"));
        
        otaStateValue->setText(qingjuOtaService->stateText());
        otaMessageValue->setText(qingjuOtaService->lastMessage().isEmpty() ? QStringLiteral("选择 NPK/RFR 后，点击“开始目标升级”或“查询目标 APP/BOOT”启动") : qingjuOtaService->lastMessage());
    } else { // RS485 协议（2: BB, 3: FF, 4: Hellobike）
        int mode = index;
        emit requestRs485SetProtocolMode(mode);

        if (devicePanel != nullptr) devicePanel->hide();
        if (serialDevicePanel != nullptr) serialDevicePanel->show();
        if (rfidTabs != nullptr) {
            if (mode == 4) {
                rfidTabs->setTabEnabled(2, true); // 允许哈啰协议使用 OTA 升级

                // 隐藏美团/青桔专属的异常注入与配置项，重置按钮文本
                if (qjOtaAnomalyGroup != nullptr) qjOtaAnomalyGroup->hide();
                if (otaErrorInjectionGroup != nullptr) otaErrorInjectionGroup->hide();
                if (otaStressGroup != nullptr) otaStressGroup->hide();
                if (qjOtaTargetLabel != nullptr) qjOtaTargetLabel->hide();
                if (qjOtaTargetCombo != nullptr) qjOtaTargetCombo->hide();
                if (otaVersionLabel != nullptr) otaVersionLabel->hide();
                if (otaVersionEdit != nullptr) otaVersionEdit->hide();

                if (otaQueryBtn != nullptr) otaQueryBtn->setText(QStringLiteral("查询APP/BOOT"));
                if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setText(QStringLiteral("开始升级"));

                if (otaStateValue != nullptr) otaStateValue->setText(hlOtaStateText());
                if (otaMessageValue != nullptr) otaMessageValue->setText(hlOtaMessage.isEmpty() ? QStringLiteral("点击“开始升级”或“查询APP/BOOT”启动") : hlOtaMessage);
            } else if (mode == 2 || mode == 3) {
                rfidTabs->setTabEnabled(2, true); // 允许 BB/FF 协议使用 OTA 升级

                // 显示版本号输入框，隐藏美团/青桔专属控件
                if (qjOtaAnomalyGroup != nullptr) qjOtaAnomalyGroup->hide();
                if (otaErrorInjectionGroup != nullptr) otaErrorInjectionGroup->hide();
                if (otaStressGroup != nullptr) otaStressGroup->hide();
                if (qjOtaTargetLabel != nullptr) qjOtaTargetLabel->hide();
                if (qjOtaTargetCombo != nullptr) qjOtaTargetCombo->hide();
                if (otaVersionLabel != nullptr) otaVersionLabel->show();
                if (otaVersionEdit != nullptr) otaVersionEdit->show();

                if (otaQueryBtn != nullptr) otaQueryBtn->setText(QStringLiteral("查询APP/BOOT"));
                if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setText(QStringLiteral("开始升级"));

                if (otaStateValue != nullptr) otaStateValue->setText(bbFfOtaStateText());
                if (otaMessageValue != nullptr) otaMessageValue->setText(bbFfOtaMessage.isEmpty() ? QStringLiteral("点击“开始升级”或“查询APP/BOOT”启动") : bbFfOtaMessage);
            } else {
                rfidTabs->setTabEnabled(2, false); // 禁用其它协议的 OTA 升级
                if (rfidTabs->currentIndex() == 2) {
                    rfidTabs->setCurrentIndex(0); // 跳回监控页
                }
            }
        }

        if (mode == 2) { // BB
            if (rfidStackedWidget != nullptr && bbRfidPanel != nullptr) {
                rfidStackedWidget->setCurrentWidget(bbRfidPanel);
            }
        } else if (mode == 3) { // FF
            if (rfidStackedWidget != nullptr && ffRfidPanel != nullptr) {
                rfidStackedWidget->setCurrentWidget(ffRfidPanel);
            }
        } else if (mode == 4) { // Hellobike
            if (rfidStackedWidget != nullptr && hlRfidPanel != nullptr) {
                rfidStackedWidget->setCurrentWidget(hlRfidPanel);
            }
            if (serialBaudRateCombo != nullptr) {
                int baudIdx = serialBaudRateCombo->findData(115200);
                if (baudIdx != -1) {
                    serialBaudRateCombo->setCurrentIndex(baudIdx);
                }
            }
            syncHlConfigToService();
        }

        if (stressStatsStackedWidget != nullptr && rs485StressPanel != nullptr) {
            stressStatsStackedWidget->setCurrentWidget(rs485StressPanel);
        }
        updateRs485TopStatus();
    }

    if (index == 0 || index == 1) {
        rfidOnlineStatusValue->setText("-");
        rfidOnlineStatusValue->setStyleSheet("color: gray; font-weight: bold;");
        lastRfidFrameTime = QDateTime();
        lastQingjuNpkFrameTime = QDateTime();
        lastQingjuRfrFrameTime = QDateTime();
        if (index == 1) {
            updateQingjuOnlineStatus(false);
        }
    } else {
        if (serialOpened) {
            rfidOnlineStatusValue->setText(QStringLiteral("串口已开"));
            rfidOnlineStatusValue->setStyleSheet("color: green; font-weight: bold;");
        } else {
            rfidOnlineStatusValue->setText(QStringLiteral("串口已关"));
            rfidOnlineStatusValue->setStyleSheet("color: red; font-weight: bold;");
        }
    }

    updateManualSendPanelMode();
    updateControlsState();
    updateStressTestPanel(stressTestService.stats());
}

void MainWindow::updateQingjuRfidPanel(const QingjuNpkState &state)
{
    if (qjRfidResultValue != nullptr) {
        qjRfidResultValue->setText(state.statusText.isEmpty() ? "-" : state.statusText);
        if (state.result == 1) {
            qjRfidResultValue->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        } else if (state.result == 2) {
            qjRfidResultValue->setStyleSheet(QStringLiteral("color: gray; font-weight: bold;"));
        } else if (state.result == 3) {
            qjRfidResultValue->setStyleSheet(QStringLiteral("color: #B26A00; font-weight: bold;"));
        } else if (state.result >= 4 && state.result <= 7) {
            qjRfidResultValue->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
        } else {
            qjRfidResultValue->setStyleSheet(QString());
        }
    }
    if (qjRfidAppStatusValue != nullptr) {
        qjRfidAppStatusValue->setText(state.appStatus.isEmpty() ? "-" : state.appStatus);
        if (state.appStatus.compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0) {
            qjRfidAppStatusValue->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        } else if (state.appStatus.compare(QStringLiteral("boot"), Qt::CaseInsensitive) == 0) {
            qjRfidAppStatusValue->setStyleSheet(QStringLiteral("color: #B26A00; font-weight: bold;"));
        } else {
            qjRfidAppStatusValue->setStyleSheet(QString());
        }
    }
    if (qjRfidAlarmValue != nullptr) {
        qjRfidAlarmValue->setText(state.alarmText.isEmpty() ? "-" : state.alarmText);
        if (state.alarmText.isEmpty()) {
            qjRfidAlarmValue->setStyleSheet(QString());
        } else if (state.alarm == 0) {
            qjRfidAlarmValue->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        } else {
            qjRfidAlarmValue->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
        }
    }
    if (qjRfidUidValue != nullptr) {
        qjRfidUidValue->setText(state.uidText.isEmpty() ? "-" : state.uidText);
    }
    if (qjRfidPwdValue != nullptr) {
        if (state.password == 0) {
            qjRfidPwdValue->setText("-");
        } else {
            qjRfidPwdValue->setText(QString("0x%1").arg(state.password, 8, 16, QChar('0')).toUpper());
            if (qjRegisterPresetCombo != nullptr &&
                qjRegisterPresetCombo->currentData().toInt() == 5 &&
                qjRegValueEdit != nullptr) {
                qjRegValueEdit->setText(QString("%1 %2")
                    .arg((state.password >> 16) & 0xFFFF, 4, 16, QChar('0'))
                    .arg(state.password & 0xFFFF, 4, 16, QChar('0'))
                    .toUpper());
            }
        }
    }
    if (qjRfidModelValue != nullptr) {
        qjRfidModelValue->setText(state.assetModel.isEmpty() ? "-" : state.assetModel);
    }
    if (qjRfidSupplierValue != nullptr) {
        qjRfidSupplierValue->setText(state.assetSupplier.isEmpty() ? "-" : state.assetSupplier);
    }
    if (qjRfidSerialValue != nullptr) {
        qjRfidSerialValue->setText(state.assetSerial.isEmpty() ? "-" : state.assetSerial);
    }
    if (qjRfidSnValue != nullptr) {
        qjRfidSnValue->setText(state.devSn.isEmpty() ? "-" : state.devSn);
    }
    if (qjRfidFirmwareVerValue != nullptr) {
        qjRfidFirmwareVerValue->setText(state.firmwareVer.isEmpty() ? "-" : state.firmwareVer);
    }
    if (qjRfidHardwareVerValue != nullptr) {
        qjRfidHardwareVerValue->setText(state.hardwareVer.isEmpty() ? "-" : state.hardwareVer);
    }
    if (qjRfidVendorValue != nullptr) {
        qjRfidVendorValue->setText(state.vendorInfo.isEmpty() ? "-" : state.vendorInfo);
    }
    if (qjRfidModelCodeValue != nullptr) {
        qjRfidModelCodeValue->setText(state.modelCodeText.isEmpty() ? "-" : state.modelCodeText);
    }
    if (qjRfidFwStrValue != nullptr) {
        qjRfidFwStrValue->setText(state.fwVersionStr.isEmpty() ? "-" : state.fwVersionStr);
    }
    if (qjRfidHwStrValue != nullptr) {
        qjRfidHwStrValue->setText(state.hwVersionStr.isEmpty() ? "-" : state.hwVersionStr);
    }
    if (stressTestService.handleQingjuState(state)) {
        updateStressTestPanel(stressTestService.stats());
    }
}

void MainWindow::onQjCustomWriteClicked()
{
    if (!canStarted) {
        QMessageBox::warning(this, "警告", "请先启动 CAN 设备！");
        return;
    }

    quint8 destAddr = 0;
    if (qjDestAddrCombo->currentIndex() == 2) {
        bool ok;
        QString text = QInputDialog::getText(this, "自定义目标设备", "请输入目标设备地址(Hex):", QLineEdit::Normal, "0A", &ok);
        if (!ok) {
            return;
        }
        QString error;
        if (!parseQingjuAddress(text, &destAddr, &error)) {
            QMessageBox::warning(this, "错误", error);
            return;
        }
    } else {
        destAddr = static_cast<quint8>(qjDestAddrCombo->currentData().toUInt());
    }

    bool ok;
    quint16 startReg = qjRegAddrEdit->text().toUShort(&ok, 16);
    if (!ok) {
        QMessageBox::warning(this, "错误", "寄存器地址格式错误！");
        return;
    }

    QString valStr = qjRegValueEdit->text().trimmed();
    QStringList valList = valStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QVector<quint16> values;
    for (const QString &s : valList) {
        quint16 val = s.toUShort(&ok, 16);
        if (!ok) {
            QMessageBox::warning(this, "错误", QString("写入数值格式错误: %1").arg(s));
            return;
        }
        values.append(val);
    }

    if (values.isEmpty()) {
        QMessageBox::warning(this, "错误", "请输入待写入的寄存器数值！");
        return;
    }

    bool noAck = (qjFuncCodeCombo->currentIndex() == 1);
    bool res = qingjuCanManager->writeRegisters(destAddr, startReg, values, noAck);

    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    if (res) {
        qjCustomRequestPending = !noAck;
        qjCustomExpectedSrc = destAddr;
        qjCustomExpectedFunc = 0x10;
        handleQjCustomLog(QString("[%1] Write Reg: 0x%2 to Dest: 0x%3 Sent")
                          .arg(timeStr)
                          .arg(QString::number(startReg, 16).toUpper())
                          .arg(QString::number(destAddr, 16).toUpper()));
        if (noAck) {
            handleQjCustomLog(QString("[%1] No-ACK write: device response is not expected").arg(timeStr));
        }
    } else {
        qjCustomRequestPending = false;
        handleQjCustomLog(QString("[%1] Write Reg: 0x%2 to Dest: 0x%3 Failed")
                          .arg(timeStr)
                          .arg(QString::number(startReg, 16).toUpper())
                          .arg(QString::number(destAddr, 16).toUpper()));
    }
}

void MainWindow::onQjCustomReadClicked()
{
    if (!canStarted) {
        QMessageBox::warning(this, "警告", "请先启动 CAN 设备！");
        return;
    }

    quint8 destAddr = 0;
    if (qjDestAddrCombo->currentIndex() == 2) {
        bool ok;
        QString text = QInputDialog::getText(this, "自定义目标设备", "请输入目标设备地址(Hex):", QLineEdit::Normal, "0A", &ok);
        if (!ok) {
            return;
        }
        QString error;
        if (!parseQingjuAddress(text, &destAddr, &error)) {
            QMessageBox::warning(this, "错误", error);
            return;
        }
    } else {
        destAddr = static_cast<quint8>(qjDestAddrCombo->currentData().toUInt());
    }

    bool ok;
    quint16 startReg = qjRegAddrEdit->text().toUShort(&ok, 16);
    if (!ok) {
        QMessageBox::warning(this, "错误", "寄存器地址格式错误！");
        return;
    }

    int regCount = 1;
    QString valStr = qjRegValueEdit->text().trimmed();
    if (!valStr.isEmpty()) {
        regCount = valStr.toInt(&ok);
        if (!ok || regCount <= 0 || regCount > 125) {
            regCount = 1;
        }
    }

    bool res = qingjuCanManager->readRegisters(destAddr, startReg, regCount);

    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    if (res) {
        qjCustomRequestPending = true;
        qjCustomExpectedSrc = destAddr;
        qjCustomExpectedFunc = 0x03;
        handleQjCustomLog(QString("[%1] Read Reg: 0x%2 (Count: %3) from Dest: 0x%4 Sent")
                          .arg(timeStr)
                          .arg(QString::number(startReg, 16).toUpper())
                          .arg(regCount)
                          .arg(QString::number(destAddr, 16).toUpper()));
    } else {
        qjCustomRequestPending = false;
        handleQjCustomLog(QString("[%1] Read Reg: 0x%2 (Count: %3) from Dest: 0x%4 Failed")
                          .arg(timeStr)
                          .arg(QString::number(startReg, 16).toUpper())
                          .arg(regCount)
                          .arg(QString::number(destAddr, 16).toUpper()));
    }
}

void MainWindow::handleQjCustomResponse(quint8 srcAddr, quint8 destAddr, quint8 funcCode, const QByteArray &payload)
{
    if (!qjCustomRequestPending ||
        srcAddr != qjCustomExpectedSrc ||
        destAddr != 0x01 ||
        funcCode != qjCustomExpectedFunc) {
        return;
    }

    qjCustomRequestPending = false;
    const QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");

    if (funcCode == 0x03) {
        if (payload.isEmpty()) {
            handleQjCustomLog(QString("[%1] Read Response from 0x%2: invalid empty payload")
                              .arg(timeStr)
                              .arg(QString::number(srcAddr, 16).toUpper()));
            return;
        }

        const int byteCount = static_cast<quint8>(payload.at(0));
        const QByteArray data = payload.mid(1, byteCount);
        if (data.size() != byteCount) {
            handleQjCustomLog(QString("[%1] Read Response from 0x%2: length mismatch, byteCount=%3 actual=%4")
                              .arg(timeStr)
                              .arg(QString::number(srcAddr, 16).toUpper())
                              .arg(byteCount)
                              .arg(data.size()));
            return;
        }

        QStringList words;
        for (int i = 0; i + 1 < data.size(); i += 2) {
            const quint16 value = (static_cast<quint8>(data.at(i)) << 8) |
                                  static_cast<quint8>(data.at(i + 1));
            words << QString("0x%1").arg(value, 4, 16, QChar('0')).toUpper();
        }
        handleQjCustomLog(QString("[%1] Read Response from 0x%2: %3")
                          .arg(timeStr)
                          .arg(QString::number(srcAddr, 16).toUpper())
                          .arg(words.isEmpty() ? QString(data.toHex(' ').toUpper()) : words.join(' ')));
        return;
    }

    if (funcCode == 0x10) {
        if (payload.size() < 3) {
            handleQjCustomLog(QString("[%1] Write Response from 0x%2: %3")
                              .arg(timeStr)
                              .arg(QString::number(srcAddr, 16).toUpper())
                              .arg(QString(payload.toHex(' ').toUpper())));
            return;
        }

        const quint16 startReg = (static_cast<quint8>(payload.at(0)) << 8) |
                                 static_cast<quint8>(payload.at(1));
        const quint8 count = static_cast<quint8>(payload.at(2));
        const QString regText = QString("%1").arg(startReg, 4, 16, QChar('0')).toUpper();
        handleQjCustomLog(QString("[%1] Write Response from 0x%2: startReg=0x%3 count=%4")
                          .arg(timeStr)
                          .arg(QString::number(srcAddr, 16).toUpper())
                          .arg(regText)
                          .arg(count));
    }
}

void MainWindow::handleQjCustomLog(const QString &text)
{
    if (qjCustomLog != nullptr) {
        qjCustomLog->append(text);
    }
}

QWidget *MainWindow::createQjRfidMonitorPanel(QWidget *parent)
{
    QWidget *panel = new QWidget(parent);
    QGridLayout *mainLayout = new QGridLayout(panel);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setHorizontalSpacing(10);
    mainLayout->setVerticalSpacing(6);

    QGroupBox *ctrlGroup = new QGroupBox(QStringLiteral("控制"), panel);
    QGridLayout *ctrlLayout = new QGridLayout(ctrlGroup);
    ctrlLayout->setContentsMargins(6, 6, 6, 6);
    ctrlLayout->setHorizontalSpacing(6);
    ctrlLayout->setVerticalSpacing(4);
    
    qjTargetDeviceCombo = new QComboBox(ctrlGroup);
    qjTargetDeviceCombo->addItem(QStringLiteral("NPK (0x0A)"), 0x0A);
    qjTargetDeviceCombo->addItem(QStringLiteral("RFR (0x0B)"), 0x0B);

    qjStartBtn = new QPushButton(QStringLiteral("开始检测"), ctrlGroup);
    qjStartBtn->setToolTip(QStringLiteral("开始检测：发送启动指令，写入 0xA900（读取模式：1-自动轮询，2-单次查询）和 0xA901（使能及读卡间隔时间，高字节Bit7=1为使能，低字节为读卡间隔）"));

    qjStopBtn = new QPushButton(QStringLiteral("停止检测"), ctrlGroup);
    qjStopBtn->setToolTip(QStringLiteral("停止检测：发送停止指令，向 0xA901 写入 0x0000 关闭读卡"));

    qjQueryModeCombo = new QComboBox(ctrlGroup);
    qjQueryModeCombo->addItem(QStringLiteral("自动轮询"), 0);
    qjQueryModeCombo->addItem(QStringLiteral("单次查询"), 1);
    qjQueryModeCombo->setToolTip(QStringLiteral("设置从机 0xA900 寄存器：1 代表普通循环读取模式，2 代表单次读取模式"));

    qjRfidPeriodSpin = new QSpinBox(ctrlGroup);
    qjRfidPeriodSpin->setRange(100, 25500);
    qjRfidPeriodSpin->setValue(100);
    qjRfidPeriodSpin->setSingleStep(100);
    qjRfidPeriodSpin->setSuffix(" ms");
    qjRfidPeriodSpin->setMinimumWidth(120);
    qjRfidPeriodSpin->setToolTip(QStringLiteral("读卡器的读卡时间间隔（单位：100ms），配置从机 0xA901 寄存器的低字节（写入值=间隔ms/100）"));

    qjHostPollPeriodSpin = new QSpinBox(ctrlGroup);
    qjHostPollPeriodSpin->setRange(100, 10000);
    qjHostPollPeriodSpin->setValue(500);
    qjHostPollPeriodSpin->setSingleStep(100);
    qjHostPollPeriodSpin->setSuffix(" ms");
    qjHostPollPeriodSpin->setMinimumWidth(120);
    qjHostPollPeriodSpin->setToolTip(QStringLiteral("设置上位机周期性发送读命令（NPK: 0xA904; RFR: 0xA02A）的时间间隔(ms)；这属于上位机软件设置，不修改从机寄存器"));

    qjDistanceQueryOnceBtn = new QPushButton(QStringLiteral("单次查询"), ctrlGroup);
    qjDistanceQueryOnceBtn->setToolTip(QStringLiteral("当查询方式为单次查询时，手动发送读指令获取从机当前数据（NPK 读取 0xA904，RFR 读取 0xA02A）"));

    qjAutoWritePwdCheckBox = new QCheckBox(QStringLiteral("自动写入密码(0xA902/A903)"), ctrlGroup);
    qjAutoWritePwdCheckBox->setToolTip(QStringLiteral("开启后，在检测到有效 UID 时自动计算并写入 0xA902/0xA903 密码寄存器进行密钥解锁"));

    ctrlLayout->addWidget(new QLabel(QStringLiteral("目标设备"), ctrlGroup), 0, 0);
    ctrlLayout->addWidget(qjTargetDeviceCombo, 0, 1);
    ctrlLayout->addWidget(qjStartBtn, 1, 0);
    ctrlLayout->addWidget(qjStopBtn, 1, 1);
    ctrlLayout->addWidget(new QLabel(QStringLiteral("查询方式 (0xA900)"), ctrlGroup), 2, 0);
    ctrlLayout->addWidget(qjQueryModeCombo, 2, 1);
    ctrlLayout->addWidget(new QLabel(QStringLiteral("读取间隔 (0xA901)"), ctrlGroup), 3, 0);
    ctrlLayout->addWidget(qjRfidPeriodSpin, 3, 1);
    ctrlLayout->addWidget(new QLabel(QStringLiteral("轮询间隔"), ctrlGroup), 4, 0);
    ctrlLayout->addWidget(qjHostPollPeriodSpin, 4, 1);
    ctrlLayout->addWidget(qjDistanceQueryOnceBtn, 5, 0, 1, 2);
    ctrlLayout->addWidget(qjAutoWritePwdCheckBox, 6, 0, 1, 2);

    connect(qjTargetDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (qingjuRfidService == nullptr) return;
        quint8 selectedAddr = qjTargetDeviceCombo->itemData(index).toUInt();
        qingjuRfidService->setTargetAddress(selectedAddr);
        
        if (qjRfidAddrValue != nullptr) {
            qjRfidAddrValue->setText(QString("0x%1").arg(selectedAddr, 2, 16, QChar('0')).toUpper());
        }
        
        bool isNpk = (selectedAddr == 0x0A);
        if (qjAutoWritePwdCheckBox != nullptr) {
            qjAutoWritePwdCheckBox->setEnabled(isNpk);
        }
        if (qjRfidAssetGroup != nullptr) {
            qjRfidAssetGroup->setEnabled(isNpk);
        }
        if (qjRfidResultValue != nullptr) qjRfidResultValue->setEnabled(isNpk);
        if (qjRfidAlarmValue != nullptr) qjRfidAlarmValue->setEnabled(isNpk);
        if (qjRfidUidValue != nullptr) qjRfidUidValue->setEnabled(isNpk);
        if (qjRfidPwdValue != nullptr) qjRfidPwdValue->setEnabled(isNpk);

        clearQingjuRfidPanel();

        if (qingjuRfidService->isScanning()) {
            qingjuRfidService->queryDeviceInfo();
        }
        saveAppConfig();
    });

    connect(qjAutoWritePwdCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (qingjuRfidService != nullptr) {
            qingjuRfidService->setAutoWritePassword(checked);
        }
        saveAppConfig();
    });

    connect(qjQueryModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        updateControlsState();
        saveAppConfig();
    });

    connect(qjHostPollPeriodSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        saveAppConfig();
    });

    connect(qjDistanceQueryOnceBtn, &QPushButton::clicked, this, [this]() {
        if (qingjuRfidService != nullptr) {
            qingjuRfidService->triggerSingleQuery();
        }
    });

    connect(qjStartBtn, &QPushButton::clicked, this, [this]() {
        if (canStarted) {
            int intervalMs = qjRfidPeriodSpin->value();
            int hostPollIntervalMs = qjHostPollPeriodSpin->value();
            int queryModeIndex = qjQueryModeCombo->currentIndex();
            int readMode = (queryModeIndex == 0) ? 1 : 2;
            qingjuRfidService->startScan(intervalMs, hostPollIntervalMs, readMode);
            updateControlsState();
        } else {
            QMessageBox::warning(this, "警告", "请先启动 CAN 设备！");
        }
    });
    connect(qjStopBtn, &QPushButton::clicked, this, [this]() {
        qingjuRfidService->stopScan();
        updateControlsState();
    });

    QGroupBox *statusGroup = new QGroupBox(QStringLiteral("NPK状态信息"), panel);
    qjRfidStatusGroup = statusGroup;
    QGridLayout *statusLayout = new QGridLayout(statusGroup);
    statusLayout->setContentsMargins(6, 6, 6, 6);
    statusLayout->setHorizontalSpacing(8);
    statusLayout->setVerticalSpacing(3);

    qjRfidAddrValue = new QLabel("0x0A", statusGroup);
    qjRfidResultValue = new QLabel("-", statusGroup);
    qjRfidAppStatusValue = new QLabel("-", statusGroup);
    qjRfidAlarmValue = new QLabel("-", statusGroup);
    qjRfidUidValue = new QLabel("-", statusGroup);
    qjRfidPwdValue = new QLabel("-", statusGroup);

    statusLayout->addWidget(new QLabel(QStringLiteral("读卡器地址"), statusGroup), 0, 0);
    statusLayout->addWidget(qjRfidAddrValue, 0, 1);
    statusLayout->addWidget(new QLabel(QStringLiteral("读取结果 0xA904"), statusGroup), 1, 0);
    statusLayout->addWidget(qjRfidResultValue, 1, 1);
    statusLayout->addWidget(new QLabel(QStringLiteral("当前程序状态 (0xA02A)"), statusGroup), 2, 0);
    statusLayout->addWidget(qjRfidAppStatusValue, 2, 1);
    statusLayout->addWidget(new QLabel(QStringLiteral("芯片异常告警 0xA919"), statusGroup), 3, 0);
    statusLayout->addWidget(qjRfidAlarmValue, 3, 1);
    statusLayout->addWidget(new QLabel(QStringLiteral("标签64位UID"), statusGroup), 4, 0);
    statusLayout->addWidget(qjRfidUidValue, 4, 1);
    statusLayout->addWidget(new QLabel(QStringLiteral("计算的密码"), statusGroup), 5, 0);
    statusLayout->addWidget(qjRfidPwdValue, 5, 1);

    QGroupBox *assetGroup = new QGroupBox(QStringLiteral("标签资产信息"), panel);
    qjRfidAssetGroup = assetGroup;
    QGridLayout *assetLayout = new QGridLayout(assetGroup);
    assetLayout->setContentsMargins(6, 6, 6, 6);
    assetLayout->setHorizontalSpacing(8);
    assetLayout->setVerticalSpacing(3);

    qjRfidModelValue = new QLabel("-", assetGroup);
    qjRfidSupplierValue = new QLabel("-", assetGroup);
    qjRfidSerialValue = new QLabel("-", assetGroup);

    assetLayout->addWidget(new QLabel(QStringLiteral("产品型号"), assetGroup), 0, 0);
    assetLayout->addWidget(qjRfidModelValue, 0, 1);
    assetLayout->addWidget(new QLabel(QStringLiteral("供应商"), assetGroup), 1, 0);
    assetLayout->addWidget(qjRfidSupplierValue, 1, 1);
    assetLayout->addWidget(new QLabel(QStringLiteral("流水号"), assetGroup), 2, 0);
    assetLayout->addWidget(qjRfidSerialValue, 2, 1);

    QGroupBox *devGroup = new QGroupBox(QStringLiteral("设备基本信息"), panel);
    QGridLayout *devLayout = new QGridLayout(devGroup);
    devLayout->setContentsMargins(6, 6, 6, 6);
    devLayout->setHorizontalSpacing(8);
    devLayout->setVerticalSpacing(3);

    qjRfidSnValue = new QLabel("-", devGroup);
    qjRfidFirmwareVerValue = new QLabel("-", devGroup);
    qjRfidHardwareVerValue = new QLabel("-", devGroup);
    qjRfidVendorValue = new QLabel("-", devGroup);
    qjRfidModelCodeValue = new QLabel("-", devGroup);
    qjRfidFwStrValue = new QLabel("-", devGroup);
    qjRfidHwStrValue = new QLabel("-", devGroup);

    devLayout->addWidget(new QLabel(QStringLiteral("设备SN"), devGroup), 0, 0);
    devLayout->addWidget(qjRfidSnValue, 0, 1);
    devLayout->addWidget(new QLabel(QStringLiteral("软件版本"), devGroup), 1, 0);
    devLayout->addWidget(qjRfidFirmwareVerValue, 1, 1);
    devLayout->addWidget(new QLabel(QStringLiteral("硬件版本"), devGroup), 2, 0);
    devLayout->addWidget(qjRfidHardwareVerValue, 2, 1);
    devLayout->addWidget(new QLabel(QStringLiteral("制造厂商"), devGroup), 3, 0);
    devLayout->addWidget(qjRfidVendorValue, 3, 1);
    devLayout->addWidget(new QLabel(QStringLiteral("型号编码"), devGroup), 4, 0);
    devLayout->addWidget(qjRfidModelCodeValue, 4, 1);
    devLayout->addWidget(new QLabel(QStringLiteral("固件标识串"), devGroup), 5, 0);
    devLayout->addWidget(qjRfidFwStrValue, 5, 1);
    devLayout->addWidget(new QLabel(QStringLiteral("硬件标识串"), devGroup), 6, 0);
    devLayout->addWidget(qjRfidHwStrValue, 6, 1);

    QGroupBox *customGroup = new QGroupBox(QStringLiteral("自定义寄存器读写调试"), panel);
    QGridLayout *customLayout = new QGridLayout(customGroup);
    customLayout->setContentsMargins(6, 6, 6, 6);
    customLayout->setHorizontalSpacing(6);
    customLayout->setVerticalSpacing(4);

    qjRegisterPresetCombo = new QComboBox(customGroup);
    qjRegisterPresetCombo->addItem(QStringLiteral("手动输入"), 0);
    qjRegisterPresetCombo->addItem(QStringLiteral("读 NPK 状态 0xA904~0xA919"), 1);
    qjRegisterPresetCombo->addItem(QStringLiteral("读 RFR APP/BOOT 0xA02A"), 2);
    qjRegisterPresetCombo->addItem(QStringLiteral("启动 NPK 检测"), 3);
    qjRegisterPresetCombo->addItem(QStringLiteral("停止 NPK 检测"), 4);
    qjRegisterPresetCombo->addItem(QStringLiteral("写一机一密 0xA902/0xA903"), 5);

    qjDestAddrCombo = new QComboBox(customGroup);
    qjDestAddrCombo->addItem(QStringLiteral("NPK (0x0A)"), 0x0A);
    qjDestAddrCombo->addItem(QStringLiteral("RFR (0x0B)"), 0x0B);
    qjDestAddrCombo->addItem(QStringLiteral("自定义"), 0x00);
    
    qjRegAddrEdit = new QLineEdit(customGroup);
    qjRegAddrEdit->setPlaceholderText("Hex: e.g. A900");
    
    qjRegValueEdit = new QLineEdit(customGroup);
    qjRegValueEdit->setPlaceholderText("Hex: e.g. 0001 0002");
    
    qjFuncCodeCombo = new QComboBox(customGroup);
    qjFuncCodeCombo->addItem(QStringLiteral("0x10 写(带ACK)"), 0x10);
    qjFuncCodeCombo->addItem(QStringLiteral("0x90 写(无ACK)"), 0x90);
    qjFuncCodeCombo->addItem(QStringLiteral("0x03 读寄存器"), 0x03);

    qjCustomWriteBtn = new QPushButton(QStringLiteral("寄存器写入"), customGroup);
    qjCustomReadBtn = new QPushButton(QStringLiteral("寄存器读取"), customGroup);
    qjCustomLog = new QTextEdit(customGroup);
    qjCustomLog->setReadOnly(true);
    qjCustomLog->setMaximumHeight(80);

    customLayout->addWidget(new QLabel(QStringLiteral("快捷模板"), customGroup), 0, 0);
    customLayout->addWidget(qjRegisterPresetCombo, 0, 1, 1, 3);

    customLayout->addWidget(new QLabel(QStringLiteral("目标设备"), customGroup), 1, 0);
    customLayout->addWidget(qjDestAddrCombo, 1, 1);
    customLayout->addWidget(new QLabel(QStringLiteral("功能码"), customGroup), 1, 2);
    customLayout->addWidget(qjFuncCodeCombo, 1, 3);

    customLayout->addWidget(new QLabel(QStringLiteral("寄存器地址"), customGroup), 2, 0);
    customLayout->addWidget(qjRegAddrEdit, 2, 1);
    customLayout->addWidget(new QLabel(QStringLiteral("值/数量"), customGroup), 2, 2);
    customLayout->addWidget(qjRegValueEdit, 2, 3);

    customLayout->addWidget(qjCustomWriteBtn, 3, 0, 1, 2);
    customLayout->addWidget(qjCustomReadBtn, 3, 2, 1, 2);
    customLayout->addWidget(qjCustomLog, 4, 0, 1, 4);

    connect(qjRegisterPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index <= 0) {
            qjRegValueEdit->setPlaceholderText(QStringLiteral("Hex: e.g. 0001 0002；读取时填写数量"));
            return;
        }

        const int preset = qjRegisterPresetCombo->itemData(index).toInt();
        qjRegValueEdit->setPlaceholderText(QStringLiteral("Hex: e.g. 0001 0002；读取时填写数量"));

        switch (preset) {
        case 1:
            qjDestAddrCombo->setCurrentIndex(0);
            qjFuncCodeCombo->setCurrentIndex(2);
            qjRegAddrEdit->setText(QStringLiteral("A904"));
            qjRegValueEdit->setText(QStringLiteral("22"));
            break;
        case 2:
            qjDestAddrCombo->setCurrentIndex(1);
            qjFuncCodeCombo->setCurrentIndex(2);
            qjRegAddrEdit->setText(QStringLiteral("A02A"));
            qjRegValueEdit->setText(QStringLiteral("1"));
            break;
        case 3: {
            const int intervalMs = qjRfidPeriodSpin == nullptr ? 100 : qjRfidPeriodSpin->value();
            const quint16 periodValue = static_cast<quint16>(0x8000 | (qBound(100, intervalMs, 25500) / 100));
            qjDestAddrCombo->setCurrentIndex(0);
            qjFuncCodeCombo->setCurrentIndex(0);
            qjRegAddrEdit->setText(QStringLiteral("A900"));
            qjRegValueEdit->setText(QString("0001 %1").arg(periodValue, 4, 16, QChar('0')).toUpper());
            break;
        }
        case 4:
            qjDestAddrCombo->setCurrentIndex(0);
            qjFuncCodeCombo->setCurrentIndex(0);
            qjRegAddrEdit->setText(QStringLiteral("A900"));
            qjRegValueEdit->setText(QStringLiteral("0001 0000"));
            break;
        case 5: {
            qjDestAddrCombo->setCurrentIndex(0);
            qjFuncCodeCombo->setCurrentIndex(0);
            qjRegAddrEdit->setText(QStringLiteral("A902"));
            QString passwordText = qjRfidPwdValue == nullptr ? QString() : qjRfidPwdValue->text().trimmed();
            if (passwordText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
                passwordText = passwordText.mid(2);
            }
            if (passwordText.length() == 8) {
                qjRegValueEdit->setText(QString("%1 %2").arg(passwordText.left(4), passwordText.mid(4, 4)).toUpper());
            } else {
                qjRegValueEdit->clear();
                qjRegValueEdit->setPlaceholderText(QStringLiteral("等待 UID 后自动填入密码"));
            }
            break;
        }
        default:
            break;
        }
    });
    connect(qjCustomWriteBtn, &QPushButton::clicked, this, &MainWindow::onQjCustomWriteClicked);
    connect(qjCustomReadBtn, &QPushButton::clicked, this, &MainWindow::onQjCustomReadClicked);

    mainLayout->addWidget(ctrlGroup, 0, 0);
    mainLayout->addWidget(statusGroup, 0, 1);
    mainLayout->addWidget(assetGroup, 1, 0);
    mainLayout->addWidget(devGroup, 1, 1);
    mainLayout->addWidget(customGroup, 2, 0, 1, 2);

    mainLayout->setColumnStretch(0, 1);
    mainLayout->setColumnStretch(1, 1);

    return panel;
}

// ==========================================
// RS485 (BB/FF) Integration Implementations
// ==========================================

void MainWindow::updateRs485TopStatus()
{
    const int protocolMode = protocolModeCombo != nullptr ? protocolModeCombo->currentIndex() : 2;
    if (protocolMode != 2 && protocolMode != 3 && protocolMode != 4) {
        return;
    }

    const QString protocolName = protocolMode == 2 ? QStringLiteral("BB") : (protocolMode == 3 ? QStringLiteral("FF") : QStringLiteral("哈啰"));
    if (topCanStatusValue != nullptr) {
        if (serialOpened) {
            topCanStatusValue->setText(QStringLiteral("RS485：已连接"));
            topCanStatusValue->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        } else {
            topCanStatusValue->setText(QStringLiteral("RS485：未连接"));
            topCanStatusValue->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
        }
    }

    if (topRfidStatusValue != nullptr) {
        QString rfidStateText;
        QString styleSheet;
        if (!serialOpened) {
            rfidStateText = QStringLiteral("离线");
            styleSheet = QStringLiteral("color: red; font-weight: bold;");
        } else if (rs485Scanning) {
            rfidStateText = QStringLiteral("轮询中");
            styleSheet = QStringLiteral("color: green; font-weight: bold;");
        } else {
            rfidStateText = QStringLiteral("待查询");
            styleSheet = QStringLiteral("color: gray; font-weight: bold;");
        }

        topRfidStatusValue->setText(QStringLiteral("协议：%1  RFID：%2").arg(protocolName, rfidStateText));
        topRfidStatusValue->setStyleSheet(styleSheet);
    }
}

void MainWindow::onSerialOpenCloseClicked()
{
    if (serialOpened) {
        emit requestRs485ClosePort();
    } else {
        QString portName = serialPortCombo->currentText();
        int baudRate = serialBaudRateCombo->currentData().toInt();
        if (portName.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("未选择串口！"));
            return;
        }
        rs485StartAfterOpen = false;
        emit requestRs485OpenPort(portName, baudRate);
    }
    updateControlsState();
}

void MainWindow::onRs485PortOpened(bool success, const QString &message)
{
    Q_UNUSED(message)
    if (!success) {
        rs485StartAfterOpen = false;
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("串口打开失败，请检查端口是否被占用"));
        updateControlsState();
        return;
    }

    const QString portName = serialPortCombo != nullptr ? serialPortCombo->currentText() : QString();
    const int baudRate = serialBaudRateCombo != nullptr ? serialBaudRateCombo->currentData().toInt() : 0;
    serialOpened = true;
    if (serialStatusLabel != nullptr) {
        serialStatusLabel->setText(QStringLiteral("已连接 (%1, %2)").arg(portName).arg(baudRate));
        serialStatusLabel->setStyleSheet("color: green; font-weight: bold;");
    }
    if (serialOpenCloseBtn != nullptr) {
        serialOpenCloseBtn->setText(QStringLiteral("关闭串口"));
    }
    updateRs485TopStatus();

    if (rs485StartAfterOpen) {
        rs485StartAfterOpen = false;
        emit requestRs485StartScan(rs485PendingIntervalMs, rs485PendingReadMode);
    } else {
        emit requestRs485QueryDeviceInfo();
    }
    updateControlsState();
}

void MainWindow::onRs485PortClosed()
{
    serialOpened = false;
    rs485Scanning = false;
    rs485StartAfterOpen = false;
    if (serialStatusLabel != nullptr) {
        serialStatusLabel->setText(QStringLiteral("串口已关闭"));
        serialStatusLabel->setStyleSheet("color: red; font-weight: bold;");
    }
    if (serialOpenCloseBtn != nullptr) {
        serialOpenCloseBtn->setText(QStringLiteral("打开串口"));
    }
    updateRs485TopStatus();
    updateControlsState();
}

void MainWindow::onRs485ScanStateChanged(bool scanning)
{
    rs485Scanning = scanning;
    updateRs485TopStatus();
    updateControlsState();
}

void MainWindow::onSerialRefreshClicked()
{
    updateRs485Ports();
}

void MainWindow::updateRs485Ports()
{
    if (serialPortCombo == nullptr) return;
    serialPortCombo->clear();
    QStringList ports = Rs485Manager::scanPorts();
    serialPortCombo->addItems(ports);
}

void MainWindow::onSerialOneClickStartClicked()
{
    if (serialOpened) {
        int protocolIdx = protocolModeCombo->currentIndex();
        if (protocolIdx == 2) {
            emit requestRs485StartScan(bbHostPollPeriodSpin->value(), bbQueryModeCombo->currentData().toInt());
        } else if (protocolIdx == 3) {
            emit requestRs485StartScan(ffHostPollPeriodSpin->value(), ffQueryModeCombo->currentData().toInt());
        } else if (protocolIdx == 4) {
            syncHlConfigToService();
            emit requestRs485StartScan(hlHostPollPeriodSpin->value(), hlQueryModeCombo->currentData().toInt());
        }
        updateControlsState();
    } else {
        QString portName = serialPortCombo->currentText();
        int baudRate = serialBaudRateCombo->currentData().toInt();
        if (portName.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("未选择串口！"));
            return;
        }
        rs485StartAfterOpen = true;
        int protocolIdx = protocolModeCombo->currentIndex();
        if (protocolIdx == 2) {
            rs485PendingIntervalMs = bbHostPollPeriodSpin->value();
            rs485PendingReadMode = bbQueryModeCombo->currentData().toInt();
        } else if (protocolIdx == 3) {
            rs485PendingIntervalMs = ffHostPollPeriodSpin->value();
            rs485PendingReadMode = ffQueryModeCombo->currentData().toInt();
        } else {
            syncHlConfigToService();
            rs485PendingIntervalMs = hlHostPollPeriodSpin->value();
            rs485PendingReadMode = hlQueryModeCombo->currentData().toInt();
        }
        emit requestRs485OpenPort(portName, baudRate);
        updateControlsState();
    }
}

void MainWindow::handleRs485Disconnect()
{
    if (serialOpened) {
        serialOpened = false;
        rs485Scanning = false;
        serialStatusLabel->setText(QStringLiteral("串口异常断开"));
        serialStatusLabel->setStyleSheet("color: red; font-weight: bold;");
        serialOpenCloseBtn->setText(QStringLiteral("打开串口"));
        updateRs485TopStatus();

        // 自动终止压测进程，防止界面状态挂死
        if (stressTestService.stats().running) {
            stopStressTest(false);
        }

        // 自动终止 RS485 OTA 升级进程，防止界面状态挂死
        if (isOtaRunning()) {
            int protocolMode = appConfig.load().protocolMode;
            if (protocolMode == 4) {
                emit requestHlOtaAbortUpgrade();
            } else if (protocolMode == 2 || protocolMode == 3) {
                emit requestBbFfOtaAbortUpgrade();
            }
        }

        updateControlsState();
        QMessageBox::critical(this, QStringLiteral("串口断开"), QStringLiteral("检测到串口已异常断开，轮询停止"));
    }
}

void MainWindow::syncHlConfigToService()
{
    quint32 timeMs = 0xFFFFFFFF;
    if (hlScanTimeSpin != nullptr) {
        timeMs = static_cast<quint32>(hlScanTimeSpin->value());
        if (hlScanTimeSpin->value() == 2147483647) {
            timeMs = 0xFFFFFFFF;
        }
    }
    int intervalMs = hlScanIntervalSpin != nullptr ? hlScanIntervalSpin->value() : 1000;
    int savedCount = hlSavedCountSpin != nullptr ? hlSavedCountSpin->value() : 1;
    int clearAfter = (hlClearAfterReadCheck != nullptr && hlClearAfterReadCheck->isChecked()) ? 1 : 0;
    int decrypt = (hlDecryptEnableCheck != nullptr && hlDecryptEnableCheck->isChecked()) ? 1 : 0;

    emit requestRs485SetHlConfig(timeMs, intervalMs, savedCount, clearAfter, decrypt);
}

void MainWindow::updateRs485RfidPanel(const Rs485State &state)
{
    auto powerDisplayText = [](int powerRaw01Dbm) {
        if (powerRaw01Dbm < 0) {
            return QStringLiteral("-");
        }
        return QStringLiteral("%1 (%2 dBm)")
            .arg(powerRaw01Dbm)
            .arg(QString::number(powerRaw01Dbm / 100.0, 'f', 2));
    };

    if (state.protocolMode == 2) { // BB
        setLabelValue(bbTagIdVal, state.tagId.isEmpty() ? QStringLiteral("-") : state.tagId);
        setLabelValue(bbRssiVal, state.bbRssi.isEmpty() ? QStringLiteral("-") : state.bbRssi);
        setLabelValue(bbPcVal, state.bbPc.isEmpty() ? QStringLiteral("-") : state.bbPc);
        setLabelValue(bbCrcVal, state.bbCrc.isEmpty() ? QStringLiteral("-") : state.bbCrc);
        setLabelValue(bbPowerVal, powerDisplayText(state.transmitPower));
        
        setLabelValue(bbRfidMfgVal, state.manufacturer.isEmpty() ? QString("-") : state.manufacturer);
        setLabelValue(bbRfidDevIdVal, state.deviceId.isEmpty() ? QString("-") : state.deviceId);
        setLabelValue(bbRfidHwVerVal, state.hwVersion.isEmpty() ? QString("-") : state.hwVersion);
        setLabelValue(bbRfidSwVerVal, state.swVersion.isEmpty() ? QString("-") : state.swVersion);
    } else if (state.protocolMode == 3) { // FF
        setLabelValue(ffTagIdVal, state.tagId.isEmpty() ? QStringLiteral("-") : state.tagId);
        setLabelValue(ffPowerVal, powerDisplayText(state.transmitPower));
        setLabelValue(ffMixerVal, state.ffMixer >= 0 ? QString::number(state.ffMixer) : QString("-"));
        setLabelValue(ffIfAmpVal, state.ffIfAmp >= 0 ? QString::number(state.ffIfAmp) : QString("-"));
        setLabelValue(ffThrdVal, state.ffThrd >= 0 ? QStringLiteral("%1 (0x%2)").arg(state.ffThrd).arg(state.ffThrd, 4, 16, QChar('0')).toUpper() : QString("-"));
        setLabelValue(ffCardSwitchVal, state.ffCardSwitch ? QStringLiteral("启用") : QStringLiteral("关闭"));

        setLabelValue(ffRfidMfgVal, state.manufacturer.isEmpty() ? QString("-") : state.manufacturer);
        setLabelValue(ffRfidDevIdVal, state.deviceId.isEmpty() ? QString("-") : state.deviceId);
        setLabelValue(ffRfidHwVerVal, state.hwVersion.isEmpty() ? QString("-") : state.hwVersion);
        setLabelValue(ffRfidSwVerVal, state.swVersion.isEmpty() ? QString("-") : state.swVersion);
    } else if (state.protocolMode == 4) { // Hellobike
        updateHlRfidPanel(state);
    }

    if (protocolModeCombo != nullptr && (protocolModeCombo->currentIndex() == 2 || protocolModeCombo->currentIndex() == 3 || protocolModeCombo->currentIndex() == 4)) {
        updateRs485TopStatus();
        if (topRfidStatusValue != nullptr && serialOpened) {
            const QString protocolName = state.protocolMode == 2 ? QStringLiteral("BB") : (state.protocolMode == 3 ? QStringLiteral("FF") : QStringLiteral("哈啰"));
            if (!state.tagId.isEmpty()) {
                topRfidStatusValue->setText(QStringLiteral("协议：%1  RFID：已读到标签").arg(protocolName));
                topRfidStatusValue->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
            } else if (state.errCode != 0 || state.isCommunicationTimeout) {
                const QString statusText = state.errorMsg.isEmpty() ? QStringLiteral("未读到标签") : state.errorMsg;
                topRfidStatusValue->setText(QStringLiteral("协议：%1  RFID：%2").arg(protocolName, statusText));
                topRfidStatusValue->setStyleSheet(QStringLiteral("color: #B26A00; font-weight: bold;"));
            }
        }
    }

    // 更新压测数据
    if (stressTestService.stats().running) {
        const int protocolMode = protocolModeCombo != nullptr ? protocolModeCombo->currentIndex() : 0;
        if (protocolMode == 2 || protocolMode == 3 || protocolMode == 4) {
            stressTestService.handleRs485State(
                state.protocolMode,
                state.tagId,
                state.errCode,
                state.isCommunicationTimeout,
                state.errorMsg
            );
            updateStressTestPanel(stressTestService.stats());
            const int targetSamples = stressTargetSamplesSpin == nullptr ? 0 : stressTargetSamplesSpin->value();
            if (targetSamples > 0 && stressTestService.stats().totalSamples >= static_cast<quint64>(targetSamples)) {
                stopStressTest(true);
            }
        }
    }
}

void MainWindow::addSerialFrameToList(bool isTx, const QByteArray &data, const QString &decodeText)
{
    const int protocolMode = protocolModeCombo != nullptr ? protocolModeCombo->currentIndex() : 2;
    const QString protocolId = protocolMode == 2 ? QStringLiteral("0xBB") :
        (protocolMode == 3 ? QStringLiteral("0xFF") : QStringLiteral("0x0D"));
    logService.logSerialFrame(isTx, data, protocolId, decodeText);

    if (!ui->checkBox_4->isChecked()) {
        return;
    }
    static quint64 serialLogSequence = 0;
    const QString sequenceText = QString("#%1 ")
        .arg(++serialLogSequence, 6, 10, QChar('0'));
    QStringList messageList;
    messageList << QDateTime::currentDateTime().time().toString("hh:mm:ss zzz");
    messageList << QStringLiteral("RS485");
    messageList << (isTx ? QStringLiteral("发送") : QStringLiteral("接收"));
    messageList << protocolId;
    messageList << QStringLiteral("数据帧");
    messageList << QStringLiteral("-");
    messageList << QString::number(data.size());
    messageList << QStringLiteral("-");
    messageList << data.toHex(' ').toUpper();
    messageList << sequenceText + decodeText;
    AddDataToList(messageList);
}

void MainWindow::onRs485CommandFinished(bool success, const QString &message)
{
    if (success) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("操作成功：%1").arg(message));
    } else {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("操作失败：%1").arg(message));
    }
    updateControlsState();
}

QWidget *MainWindow::createBbRfidMonitorPanel(QWidget *parent)
{
    QWidget *panel = new QWidget(parent);
    QVBoxLayout *mainLayout = new QVBoxLayout(panel);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    QGroupBox *controlGroup = new QGroupBox(QStringLiteral("扫卡控制"), panel);
    QGridLayout *controlLayout = new QGridLayout(controlGroup);
    controlLayout->setContentsMargins(8, 8, 8, 8);
    controlLayout->setHorizontalSpacing(6);
    controlLayout->setVerticalSpacing(6);

    bbStartBtn = new QPushButton(QStringLiteral("开始轮询"), controlGroup);
    bbStopBtn = new QPushButton(QStringLiteral("停止轮询"), controlGroup);
    bbQueryOnceBtn = new QPushButton(QStringLiteral("单次查询"), controlGroup);
    bbQueryModeCombo = new QComboBox(controlGroup);
    bbQueryModeCombo->addItem(QStringLiteral("自动轮询"), 1);
    bbQueryModeCombo->addItem(QStringLiteral("单次查询"), 2);
    bbHostPollPeriodSpin = new QSpinBox(controlGroup);
    bbHostPollPeriodSpin->setRange(100, 10000);
    bbHostPollPeriodSpin->setSuffix(" ms");
    bbHostPollPeriodSpin->setValue(500);
    bbHostPollPeriodSpin->setToolTip(QStringLiteral("上位机自动轮询尝试间隔。若上一条请求尚未收到响应或超时，本周期会跳过发送，避免请求堆积。"));

    controlLayout->addWidget(new QLabel(QStringLiteral("工作模式"), controlGroup), 0, 0);
    controlLayout->addWidget(bbQueryModeCombo, 0, 1);
    controlLayout->addWidget(new QLabel(QStringLiteral("轮询周期"), controlGroup), 0, 2);
    controlLayout->addWidget(bbHostPollPeriodSpin, 0, 3);
    controlLayout->addWidget(bbStartBtn, 1, 0, 1, 2);
    controlLayout->addWidget(bbStopBtn, 1, 2);
    controlLayout->addWidget(bbQueryOnceBtn, 1, 3);

    QGroupBox *powerGroup = new QGroupBox(QStringLiteral("发射功率"), panel);
    QHBoxLayout *powerLayout = new QHBoxLayout(powerGroup);
    powerLayout->setContentsMargins(8, 8, 8, 8);
    powerLayout->setSpacing(8);

    bbPowerSpin = new QSpinBox(powerGroup);
    bbPowerSpin->setRange(0, 3300);
    bbPowerSpin->setSuffix(" (0.01dBm)");
    bbPowerSpin->setValue(2000);
    bbPowerSpin->setToolTip(QStringLiteral("协议单位：0.01dBm，20dBm = 2000。"));
    bbSetPowerBtn = new QPushButton(QStringLiteral("设置功率"), powerGroup);
    bbQueryPowerBtn = new QPushButton(QStringLiteral("读取功率"), powerGroup);
    bbPowerVal = new QLabel("-", powerGroup);

    powerLayout->addWidget(new QLabel(QStringLiteral("设置值(0.01dBm):"), powerGroup));
    powerLayout->addWidget(bbPowerSpin);
    powerLayout->addWidget(bbSetPowerBtn);
    powerLayout->addWidget(bbQueryPowerBtn);
    powerLayout->addWidget(new QLabel(QStringLiteral("当前值:"), powerGroup));
    powerLayout->addWidget(bbPowerVal);
    powerLayout->addStretch();

    QHBoxLayout *infoAndDataLayout = new QHBoxLayout();
    infoAndDataLayout->setSpacing(8);

    QGroupBox *infoGroup = new QGroupBox(QStringLiteral("设备信息"), panel);
    QGridLayout *infoLayout = new QGridLayout(infoGroup);
    infoLayout->setContentsMargins(8, 8, 8, 8);
    infoLayout->setHorizontalSpacing(6);
    infoLayout->setVerticalSpacing(6);

    bbQueryInfoBtn = new QPushButton(QStringLiteral("读取信息"), infoGroup);
    bbRfidMfgVal = new QLabel("-", infoGroup);
    bbRfidDevIdVal = new QLabel("-", infoGroup);
    bbRfidHwVerVal = new QLabel("-", infoGroup);
    bbRfidSwVerVal = new QLabel("-", infoGroup);

    infoLayout->addWidget(new QLabel(QStringLiteral("厂商"), infoGroup), 0, 0);
    infoLayout->addWidget(bbRfidMfgVal, 0, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("设备ID"), infoGroup), 1, 0);
    infoLayout->addWidget(bbRfidDevIdVal, 1, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("硬件版本"), infoGroup), 2, 0);
    infoLayout->addWidget(bbRfidHwVerVal, 2, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("软件版本"), infoGroup), 3, 0);
    infoLayout->addWidget(bbRfidSwVerVal, 3, 1);
    infoLayout->addWidget(bbQueryInfoBtn, 4, 0, 1, 2);

    QGroupBox *dataGroup = new QGroupBox(QStringLiteral("标签数据"), panel);
    QGridLayout *dataLayout = new QGridLayout(dataGroup);
    dataLayout->setContentsMargins(8, 8, 8, 8);
    dataLayout->setHorizontalSpacing(6);
    dataLayout->setVerticalSpacing(6);

    bbTagIdVal = new QLabel("-", dataGroup);
    bbTagIdVal->setWordWrap(true);
    bbPcVal = new QLabel("-", dataGroup);
    bbCrcVal = new QLabel("-", dataGroup);
    bbRssiVal = new QLabel("-", dataGroup);

    dataLayout->addWidget(new QLabel(QStringLiteral("卡号(EPC/UID)"), dataGroup), 0, 0);
    dataLayout->addWidget(bbTagIdVal, 0, 1, 1, 3);
    dataLayout->addWidget(new QLabel(QStringLiteral("PC"), dataGroup), 1, 0);
    dataLayout->addWidget(bbPcVal, 1, 1);
    dataLayout->addWidget(new QLabel(QStringLiteral("CRC"), dataGroup), 1, 2);
    dataLayout->addWidget(bbCrcVal, 1, 3);
    dataLayout->addWidget(new QLabel(QStringLiteral("RSSI"), dataGroup), 2, 0);
    dataLayout->addWidget(bbRssiVal, 2, 1, 1, 3);

    infoGroup->setMinimumWidth(220);
    infoGroup->setMaximumWidth(320);
    infoAndDataLayout->addWidget(infoGroup);
    infoAndDataLayout->addWidget(dataGroup);

    mainLayout->addWidget(controlGroup);
    mainLayout->addWidget(powerGroup);
    mainLayout->addLayout(infoAndDataLayout);
    mainLayout->addStretch();

    connect(bbStartBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开，请先打开串口"));
            return;
        }
        emit requestRs485StartScan(bbHostPollPeriodSpin->value(), bbQueryModeCombo->currentData().toInt());
        updateControlsState();
    });
    connect(bbStopBtn, &QPushButton::clicked, this, [this]() {
        emit requestRs485StopScan();
        updateControlsState();
    });
    connect(bbQueryOnceBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485TriggerSingleQuery();
    });
    connect(bbQueryInfoBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485QueryDeviceInfo();
    });
    connect(bbSetPowerBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485SetPower(bbPowerSpin->value());
    });
    connect(bbQueryPowerBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485QueryPower();
    });

    return panel;
}

QWidget *MainWindow::createFfRfidMonitorPanel(QWidget *parent)
{
    QWidget *panel = new QWidget(parent);
    QVBoxLayout *mainLayout = new QVBoxLayout(panel);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    QGroupBox *controlGroup = new QGroupBox(QStringLiteral("扫卡与控制"), panel);
    QGridLayout *controlLayout = new QGridLayout(controlGroup);
    controlLayout->setContentsMargins(8, 8, 8, 8);
    controlLayout->setHorizontalSpacing(6);
    controlLayout->setVerticalSpacing(6);

    ffStartBtn = new QPushButton(QStringLiteral("开始轮询"), controlGroup);
    ffStopBtn = new QPushButton(QStringLiteral("停止轮询"), controlGroup);
    ffQueryOnceBtn = new QPushButton(QStringLiteral("单次查询"), controlGroup);
    ffQueryModeCombo = new QComboBox(controlGroup);
    ffQueryModeCombo->addItem(QStringLiteral("自动轮询"), 1);
    ffQueryModeCombo->addItem(QStringLiteral("单次查询"), 2);
    ffHostPollPeriodSpin = new QSpinBox(controlGroup);
    ffHostPollPeriodSpin->setRange(100, 10000);
    ffHostPollPeriodSpin->setSuffix(" ms");
    ffHostPollPeriodSpin->setValue(500);
    ffHostPollPeriodSpin->setToolTip(QStringLiteral("上位机自动轮询尝试间隔。若上一条请求尚未收到响应或超时，本周期会跳过发送，避免请求堆积。"));

    ffRebootBtn = new QPushButton(QStringLiteral("设备重启"), controlGroup);
    ffQuerySwitchBtn = new QPushButton(QStringLiteral("查询读卡开关"), controlGroup);
    ffCardSwitchVal = new QLabel("-", controlGroup);

    controlLayout->addWidget(new QLabel(QStringLiteral("工作模式"), controlGroup), 0, 0);
    controlLayout->addWidget(ffQueryModeCombo, 0, 1);
    controlLayout->addWidget(new QLabel(QStringLiteral("轮询周期"), controlGroup), 0, 2);
    controlLayout->addWidget(ffHostPollPeriodSpin, 0, 3);
    controlLayout->addWidget(ffStartBtn, 1, 0, 1, 2);
    controlLayout->addWidget(ffStopBtn, 1, 2);
    controlLayout->addWidget(ffQueryOnceBtn, 1, 3);
    
    controlLayout->addWidget(ffRebootBtn, 2, 0, 1, 2);
    controlLayout->addWidget(ffQuerySwitchBtn, 2, 2);
    controlLayout->addWidget(ffCardSwitchVal, 2, 3);

    QHBoxLayout *configParamsLayout = new QHBoxLayout();
    configParamsLayout->setSpacing(8);

    QGroupBox *powerGroup = new QGroupBox(QStringLiteral("发射功率"), panel);
    QGridLayout *powerLayout = new QGridLayout(powerGroup);
    powerLayout->setContentsMargins(8, 8, 8, 8);
    powerLayout->setHorizontalSpacing(6);
    powerLayout->setVerticalSpacing(6);

    ffPowerSpin = new QSpinBox(powerGroup);
    ffPowerSpin->setRange(0, 3300);
    ffPowerSpin->setSuffix(" (0.01dBm)");
    ffPowerSpin->setValue(2000);
    ffPowerSpin->setToolTip(QStringLiteral("协议单位：0.01dBm，20dBm = 2000。"));
    ffSetPowerBtn = new QPushButton(QStringLiteral("设置功率"), powerGroup);
    ffQueryPowerBtn = new QPushButton(QStringLiteral("读取功率"), powerGroup);
    ffPowerVal = new QLabel("-", powerGroup);

    powerLayout->addWidget(new QLabel(QStringLiteral("设置值:"), powerGroup), 0, 0);
    powerLayout->addWidget(ffPowerSpin, 0, 1);
    powerLayout->addWidget(ffSetPowerBtn, 1, 0);
    powerLayout->addWidget(ffQueryPowerBtn, 1, 1);
    powerLayout->addWidget(new QLabel(QStringLiteral("当前值:"), powerGroup), 2, 0);
    powerLayout->addWidget(ffPowerVal, 2, 1);

    QGroupBox *demodGroup = new QGroupBox(QStringLiteral("解调参数设置"), panel);
    QGridLayout *demodLayout = new QGridLayout(demodGroup);
    demodLayout->setContentsMargins(8, 8, 8, 8);
    demodLayout->setHorizontalSpacing(6);
    demodLayout->setVerticalSpacing(6);

    ffMixerSpin = new QSpinBox(demodGroup);
    ffMixerSpin->setRange(0, 7);
    ffMixerSpin->setPrefix(QStringLiteral("混频增益: "));
    ffMixerSpin->setToolTip(QStringLiteral("协议字段 Mixer_G，示例 0x03 对应 9dB。"));
    ffIfAmpSpin = new QSpinBox(demodGroup);
    ffIfAmpSpin->setRange(0, 7);
    ffIfAmpSpin->setPrefix(QStringLiteral("中频增益: "));
    ffIfAmpSpin->setToolTip(QStringLiteral("协议字段 IF_G，示例 0x06 对应 36dB。"));
    ffThrdSpin = new QSpinBox(demodGroup);
    ffThrdSpin->setRange(0, 65535);
    ffThrdSpin->setPrefix(QStringLiteral("解调阈值: "));
    ffThrdSpin->setValue(432);
    ffThrdSpin->setToolTip(QStringLiteral("协议字段 Thrd，2字节阈值；推荐最小值 0x01B0 = 432。"));
    
    ffMixerVal = new QLabel("-", demodGroup);
    ffIfAmpVal = new QLabel("-", demodGroup);
    ffThrdVal = new QLabel("-", demodGroup);

    ffSetDemodBtn = new QPushButton(QStringLiteral("下发解调"), demodGroup);
    ffQueryDemodBtn = new QPushButton(QStringLiteral("读取解调"), demodGroup);

    demodLayout->addWidget(ffMixerSpin, 0, 0);
    demodLayout->addWidget(ffMixerVal, 0, 1);
    demodLayout->addWidget(ffIfAmpSpin, 1, 0);
    demodLayout->addWidget(ffIfAmpVal, 1, 1);
    demodLayout->addWidget(ffThrdSpin, 2, 0);
    demodLayout->addWidget(ffThrdVal, 2, 1);
    demodLayout->addWidget(ffSetDemodBtn, 3, 0);
    demodLayout->addWidget(ffQueryDemodBtn, 3, 1);

    configParamsLayout->addWidget(powerGroup);
    configParamsLayout->addWidget(demodGroup);

    QHBoxLayout *infoAndDataLayout = new QHBoxLayout();
    infoAndDataLayout->setSpacing(8);

    QGroupBox *infoGroup = new QGroupBox(QStringLiteral("设备信息"), panel);
    QGridLayout *infoLayout = new QGridLayout(infoGroup);
    infoLayout->setContentsMargins(8, 8, 8, 8);
    infoLayout->setHorizontalSpacing(6);
    infoLayout->setVerticalSpacing(6);

    ffQueryInfoBtn = new QPushButton(QStringLiteral("读取信息"), infoGroup);
    ffRfidMfgVal = new QLabel("-", infoGroup);
    ffRfidDevIdVal = new QLabel("-", infoGroup);
    ffRfidHwVerVal = new QLabel("-", infoGroup);
    ffRfidSwVerVal = new QLabel("-", infoGroup);

    infoLayout->addWidget(new QLabel(QStringLiteral("厂商"), infoGroup), 0, 0);
    infoLayout->addWidget(ffRfidMfgVal, 0, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("设备ID"), infoGroup), 1, 0);
    infoLayout->addWidget(ffRfidDevIdVal, 1, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("硬件版本"), infoGroup), 2, 0);
    infoLayout->addWidget(ffRfidHwVerVal, 2, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("软件版本"), infoGroup), 3, 0);
    infoLayout->addWidget(ffRfidSwVerVal, 3, 1);
    infoLayout->addWidget(ffQueryInfoBtn, 4, 0, 1, 2);

    QGroupBox *dataGroup = new QGroupBox(QStringLiteral("标签数据"), panel);
    QVBoxLayout *dataLayout = new QVBoxLayout(dataGroup);
    dataLayout->setContentsMargins(8, 8, 8, 8);
    
    ffTagIdVal = new QLabel("-", dataGroup);
    ffTagIdVal->setWordWrap(true);
    
    dataLayout->addWidget(new QLabel(QStringLiteral("当前读取到的标签ID:"), dataGroup));
    dataLayout->addWidget(ffTagIdVal);
    dataLayout->addStretch();

    infoGroup->setMinimumWidth(220);
    infoGroup->setMaximumWidth(320);
    infoAndDataLayout->addWidget(infoGroup);
    infoAndDataLayout->addWidget(dataGroup);

    mainLayout->addWidget(controlGroup);
    mainLayout->addLayout(configParamsLayout);
    mainLayout->addLayout(infoAndDataLayout);
    mainLayout->addStretch();

    connect(ffStartBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485StartScan(ffHostPollPeriodSpin->value(), ffQueryModeCombo->currentData().toInt());
        updateControlsState();
    });
    connect(ffStopBtn, &QPushButton::clicked, this, [this]() {
        emit requestRs485StopScan();
        updateControlsState();
    });
    connect(ffQueryOnceBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485TriggerSingleQuery();
    });
    connect(ffRebootBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485FfReboot();
    });
    connect(ffQuerySwitchBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485FfQueryCardSwitch();
    });
    connect(ffQueryInfoBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485QueryDeviceInfo();
    });
    connect(ffSetPowerBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485SetPower(ffPowerSpin->value());
    });
    connect(ffQueryPowerBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485QueryPower();
    });
    connect(ffSetDemodBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485FfSetDemodulatorParams(ffMixerSpin->value(), ffIfAmpSpin->value(), ffThrdSpin->value());
    });
    connect(ffQueryDemodBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        emit requestRs485FfQueryDemodulatorParams();
    });

    return panel;
}

QWidget *MainWindow::createRs485StressPanel(QWidget *parent)
{
    QWidget *panel = new QWidget(parent);
    QGridLayout *layout = new QGridLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(6);

    rs485StressStateValue = new QLabel("-", panel);
    rs485StressElapsedValue = new QLabel("-", panel);
    rs485StressTotalSamplesValue = new QLabel("-", panel);
    rs485StressPollSkippedValue = new QLabel("-", panel);
    rs485StressSuccessCountValue = new QLabel("-", panel);
    rs485StressSuccessRateValue = new QLabel("-", panel);
    rs485StressNoTagCountValue = new QLabel("-", panel);
    rs485StressModuleFaultValue = new QLabel("-", panel);
    rs485StressCommunicationFaultValue = new QLabel("-", panel);
    rs485StressCurrentTagValue = new QLabel("-", panel);
    rs485StressLastSuccessTagValue = new QLabel("-", panel);
    rs485StressUniqueTagCountValue = new QLabel("-", panel);
    rs485StressMaxContinuousFailureValue = new QLabel("-", panel);
    rs485StressLastFailureReasonValue = new QLabel("-", panel);

    rs485StressCurrentTagValue->setWordWrap(true);
    rs485StressLastSuccessTagValue->setWordWrap(true);
    rs485StressLastFailureReasonValue->setWordWrap(true);

    QGroupBox *summaryGroup = new QGroupBox(QStringLiteral("RS485读卡统计"), panel);
    QGridLayout *summaryLayout = new QGridLayout(summaryGroup);
    summaryLayout->setContentsMargins(8, 8, 8, 8);
    summaryLayout->addWidget(new QLabel(QStringLiteral("状态"), summaryGroup), 0, 0);
    summaryLayout->addWidget(rs485StressStateValue, 0, 1);
    summaryLayout->addWidget(new QLabel(QStringLiteral("时长"), summaryGroup), 0, 2);
    summaryLayout->addWidget(rs485StressElapsedValue, 0, 3);
    summaryLayout->addWidget(new QLabel(QStringLiteral("成功率"), summaryGroup), 1, 0);
    summaryLayout->addWidget(rs485StressSuccessRateValue, 1, 1);
    summaryLayout->addWidget(new QLabel(QStringLiteral("总轮询次数"), summaryGroup), 2, 0);
    summaryLayout->addWidget(rs485StressTotalSamplesValue, 2, 1);
    summaryLayout->addWidget(new QLabel(QStringLiteral("读卡成功"), summaryGroup), 2, 2);
    summaryLayout->addWidget(rs485StressSuccessCountValue, 2, 3);
    summaryLayout->addWidget(new QLabel(QStringLiteral("轮询跳过"), summaryGroup), 3, 0);
    summaryLayout->addWidget(rs485StressPollSkippedValue, 3, 1);

    QGroupBox *tagGroup = new QGroupBox(QStringLiteral("标签信息"), panel);
    QGridLayout *tagLayout = new QGridLayout(tagGroup);
    tagLayout->setContentsMargins(8, 8, 8, 8);
    tagLayout->addWidget(new QLabel(QStringLiteral("当前标签"), tagGroup), 0, 0);
    tagLayout->addWidget(rs485StressCurrentTagValue, 0, 1, 1, 3);
    tagLayout->addWidget(new QLabel(QStringLiteral("最后成功标签"), tagGroup), 1, 0);
    tagLayout->addWidget(rs485StressLastSuccessTagValue, 1, 1, 1, 3);
    tagLayout->addWidget(new QLabel(QStringLiteral("唯一标签数"), tagGroup), 2, 0);
    tagLayout->addWidget(rs485StressUniqueTagCountValue, 2, 1);

    QGroupBox *resultGroup = new QGroupBox(QStringLiteral("结果分类与异常"), panel);
    QGridLayout *resultLayout = new QGridLayout(resultGroup);
    resultLayout->setContentsMargins(8, 8, 8, 8);
    resultLayout->addWidget(new QLabel(QStringLiteral("无标签"), resultGroup), 0, 0);
    resultLayout->addWidget(rs485StressNoTagCountValue, 0, 1);
    resultLayout->addWidget(new QLabel(QStringLiteral("模块故障"), resultGroup), 0, 2);
    resultLayout->addWidget(rs485StressModuleFaultValue, 0, 3);
    resultLayout->addWidget(new QLabel(QStringLiteral("通信故障"), resultGroup), 1, 0);
    resultLayout->addWidget(rs485StressCommunicationFaultValue, 1, 1);
    resultLayout->addWidget(new QLabel(QStringLiteral("最大连续失败"), resultGroup), 1, 2);
    resultLayout->addWidget(rs485StressMaxContinuousFailureValue, 1, 3);
    resultLayout->addWidget(new QLabel(QStringLiteral("失败原因"), resultGroup), 2, 0);
    resultLayout->addWidget(rs485StressLastFailureReasonValue, 2, 1, 1, 3);

    layout->addWidget(summaryGroup, 0, 0);
    layout->addWidget(tagGroup, 0, 1);
    layout->addWidget(resultGroup, 1, 0, 1, 2);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);

    return panel;
}

QWidget *MainWindow::createHlRfidMonitorPanel(QWidget *parent)
{
    QWidget *panel = new QWidget(parent);
    QVBoxLayout *mainLayout = new QVBoxLayout(panel);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    QGroupBox *controlGroup = new QGroupBox(QStringLiteral("扫卡控制"), panel);
    QGridLayout *controlLayout = new QGridLayout(controlGroup);
    controlLayout->setContentsMargins(8, 8, 8, 8);
    controlLayout->setHorizontalSpacing(6);
    controlLayout->setVerticalSpacing(6);

    hlStartBtn = new QPushButton(QStringLiteral("开始轮询"), controlGroup);
    hlStopBtn = new QPushButton(QStringLiteral("停止轮询"), controlGroup);
    hlQueryOnceBtn = new QPushButton(QStringLiteral("单次查询"), controlGroup);
    hlQueryModeCombo = new QComboBox(controlGroup);
    hlQueryModeCombo->addItem(QStringLiteral("自动轮询"), 1);
    hlQueryModeCombo->addItem(QStringLiteral("单次查询"), 2);
    hlHostPollPeriodSpin = new QSpinBox(controlGroup);
    hlHostPollPeriodSpin->setRange(100, 10000);
    hlHostPollPeriodSpin->setSuffix(" ms");
    hlHostPollPeriodSpin->setValue(1000);
    hlHostPollPeriodSpin->setToolTip(QStringLiteral("上位机自动轮询尝试间隔。若上一条请求尚未收到响应或超时，本周期会跳过发送，避免请求堆积。"));

    controlLayout->addWidget(new QLabel(QStringLiteral("工作模式"), controlGroup), 0, 0);
    controlLayout->addWidget(hlQueryModeCombo, 0, 1);
    controlLayout->addWidget(new QLabel(QStringLiteral("轮询周期"), controlGroup), 0, 2);
    controlLayout->addWidget(hlHostPollPeriodSpin, 0, 3);
    controlLayout->addWidget(hlStartBtn, 1, 0, 1, 2);
    controlLayout->addWidget(hlStopBtn, 1, 2);
    controlLayout->addWidget(hlQueryOnceBtn, 1, 3);

    QGroupBox *scanControlGroup = new QGroupBox(QStringLiteral("扫描控制配置 (私有寄存器 4101)"), panel);
    QGridLayout *scanControlLayout = new QGridLayout(scanControlGroup);
    scanControlLayout->setContentsMargins(8, 8, 8, 8);
    scanControlLayout->setHorizontalSpacing(6);
    scanControlLayout->setVerticalSpacing(6);

    hlScanTimeSpin = new QSpinBox(scanControlGroup);
    hlScanTimeSpin->setRange(0, 2147483647);
    hlScanTimeSpin->setSuffix(" ms");
    hlScanTimeSpin->setValue(2147483647); // representing 0xFFFFFFFF (infinite)
    hlScanTimeSpin->setToolTip(QStringLiteral("扫描持续时间，2147483647 表示无限。"));

    hlScanIntervalSpin = new QSpinBox(scanControlGroup);
    hlScanIntervalSpin->setRange(0, 65535);
    hlScanIntervalSpin->setSuffix(" ms");
    hlScanIntervalSpin->setValue(1000);

    hlSavedCountSpin = new QSpinBox(scanControlGroup);
    hlSavedCountSpin->setRange(1, 10);
    hlSavedCountSpin->setValue(1);

    hlClearAfterReadCheck = new QCheckBox(QStringLiteral("读取后自动清空缓存"), scanControlGroup);
    hlClearAfterReadCheck->setChecked(false);

    hlDecryptEnableCheck = new QCheckBox(QStringLiteral("开启数据解密"), scanControlGroup);
    hlDecryptEnableCheck->setChecked(true);

    hlSetControlBtn = new QPushButton(QStringLiteral("下发扫描配置"), scanControlGroup);
    hlRebootBtn = new QPushButton(QStringLiteral("设备重启"), scanControlGroup);

    scanControlLayout->addWidget(new QLabel(QStringLiteral("扫描时间"), scanControlGroup), 0, 0);
    scanControlLayout->addWidget(hlScanTimeSpin, 0, 1);
    scanControlLayout->addWidget(new QLabel(QStringLiteral("扫描间隔"), scanControlGroup), 0, 2);
    scanControlLayout->addWidget(hlScanIntervalSpin, 0, 3);
    
    scanControlLayout->addWidget(new QLabel(QStringLiteral("缓存数量"), scanControlGroup), 1, 0);
    scanControlLayout->addWidget(hlSavedCountSpin, 1, 1);
    scanControlLayout->addWidget(hlClearAfterReadCheck, 1, 2);
    scanControlLayout->addWidget(hlDecryptEnableCheck, 1, 3);
    
    scanControlLayout->addWidget(hlSetControlBtn, 2, 0, 1, 2);
    scanControlLayout->addWidget(hlRebootBtn, 2, 2, 1, 2);

    QHBoxLayout *infoAndDataLayout = new QHBoxLayout();
    infoAndDataLayout->setSpacing(8);

    QGroupBox *infoGroup = new QGroupBox(QStringLiteral("设备信息"), panel);
    QGridLayout *infoLayout = new QGridLayout(infoGroup);
    infoLayout->setContentsMargins(8, 8, 8, 8);
    infoLayout->setHorizontalSpacing(6);
    infoLayout->setVerticalSpacing(6);

    hlQueryInfoBtn = new QPushButton(QStringLiteral("读取版本与厂商"), infoGroup);
    hlRfidMfgVal = new QLabel("-", infoGroup);
    hlRfidDevIdVal = new QLabel("-", infoGroup);
    hlRfidHwVerVal = new QLabel("-", infoGroup);
    hlRfidSwVerVal = new QLabel("-", infoGroup);
    hlRfidProtoVerVal = new QLabel("-", infoGroup);
    hlRfidProjectNoVal = new QLabel("-", infoGroup);

    infoLayout->addWidget(new QLabel(QStringLiteral("厂商标识"), infoGroup), 0, 0);
    infoLayout->addWidget(hlRfidMfgVal, 0, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("协议版本"), infoGroup), 1, 0);
    infoLayout->addWidget(hlRfidProtoVerVal, 1, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("项目编号"), infoGroup), 2, 0);
    infoLayout->addWidget(hlRfidProjectNoVal, 2, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("硬件版本"), infoGroup), 3, 0);
    infoLayout->addWidget(hlRfidHwVerVal, 3, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("软件版本"), infoGroup), 4, 0);
    infoLayout->addWidget(hlRfidSwVerVal, 4, 1);
    infoLayout->addWidget(new QLabel(QStringLiteral("设备备注"), infoGroup), 5, 0);
    infoLayout->addWidget(hlRfidDevIdVal, 5, 1);
    infoLayout->addWidget(hlQueryInfoBtn, 6, 0, 1, 2);

    QGroupBox *dataGroup = new QGroupBox(QStringLiteral("卡号数据"), panel);
    QGridLayout *dataLayout = new QGridLayout(dataGroup);
    dataLayout->setContentsMargins(8, 8, 8, 8);
    dataLayout->setHorizontalSpacing(6);
    dataLayout->setVerticalSpacing(6);

    hlTagIdVal = new QLabel("-", dataGroup);
    hlTagIdVal->setStyleSheet("font-size: 16px; font-weight: bold; color: blue;");
    hlTagIdVal->setWordWrap(true);
    hlScanStateVal = new QLabel("-", dataGroup);
    hlErrorCodeVal = new QLabel("-", dataGroup);

    dataLayout->addWidget(new QLabel(QStringLiteral("EPC/UID:"), dataGroup), 0, 0);
    dataLayout->addWidget(hlTagIdVal, 0, 1);
    dataLayout->addWidget(new QLabel(QStringLiteral("运行状态:"), dataGroup), 1, 0);
    dataLayout->addWidget(hlScanStateVal, 1, 1);
    dataLayout->addWidget(new QLabel(QStringLiteral("错误码:"), dataGroup), 2, 0);
    dataLayout->addWidget(hlErrorCodeVal, 2, 1);
    dataLayout->setRowStretch(3, 1);

    infoAndDataLayout->addWidget(infoGroup, 1);
    infoAndDataLayout->addWidget(dataGroup, 2);

    mainLayout->addWidget(controlGroup);
    mainLayout->addWidget(scanControlGroup);
    mainLayout->addLayout(infoAndDataLayout);
    mainLayout->addStretch();

    // Bind event connections
    connect(hlStartBtn, &QPushButton::clicked, this, [this]() {
        syncHlConfigToService();
        emit requestRs485StartScan(hlHostPollPeriodSpin->value(), hlQueryModeCombo->currentData().toInt());
        updateControlsState();
    });
    connect(hlStopBtn, &QPushButton::clicked, this, [this]() {
        emit requestRs485StopScan();
        updateControlsState();
    });
    connect(hlQueryOnceBtn, &QPushButton::clicked, this, [this]() {
        emit requestRs485TriggerSingleQuery();
    });
    connect(hlQueryInfoBtn, &QPushButton::clicked, this, [this]() {
        emit requestRs485QueryDeviceInfo();
    });
    connect(hlRebootBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("重启从机"), QStringLiteral("确认重启哈啰RFID读卡器吗？")) == QMessageBox::Yes) {
            emit requestRs485HlRebootDevice();
        }
    });
    connect(hlSetControlBtn, &QPushButton::clicked, this, [this]() {
        int startStop = rs485Scanning ? 1 : 0;
        quint32 timeMs = static_cast<quint32>(hlScanTimeSpin->value());
        if (hlScanTimeSpin->value() == 2147483647) {
            timeMs = 0xFFFFFFFF; // translate INT_MAX to infinite value
        }
        int intervalMs = hlScanIntervalSpin->value();
        int savedCount = hlSavedCountSpin->value();
        int clearAfter = hlClearAfterReadCheck->isChecked() ? 1 : 0;
        int decrypt = hlDecryptEnableCheck->isChecked() ? 1 : 0;
        
        emit requestRs485HlWriteScanControl(startStop, timeMs, intervalMs, savedCount, clearAfter, decrypt);
    });

    return panel;
}

void MainWindow::updateHlRfidPanel(const Rs485State &state)
{
    setLabelValue(hlTagIdVal, state.tagId.isEmpty() ? QStringLiteral("-") : state.tagId);
    
    QString scanStateText;
    if (state.hlScanState == 0) scanStateText = QStringLiteral("未扫描 (0)");
    else if (state.hlScanState == 1) scanStateText = QStringLiteral("扫描中 (1)");
    else if (state.hlScanState == 2) scanStateText = QStringLiteral("停止扫描 (2)");
    else scanStateText = QString::number(state.hlScanState);
    setLabelValue(hlScanStateVal, scanStateText);

    QString errText;
    if (state.hlErrorCode == 0) errText = QStringLiteral("0 (未扫描)");
    else if (state.hlErrorCode == 1) errText = QStringLiteral("1 (未扫到标签)");
    else if (state.hlErrorCode == 2) errText = QStringLiteral("2 (成功扫到标签)");
    else if (state.hlErrorCode == -1) errText = QStringLiteral("-1 (读卡器异常)");
    else errText = QString::number(state.hlErrorCode);
    setLabelValue(hlErrorCodeVal, errText);

    setLabelValue(hlRfidSwVerVal, state.swVersion.isEmpty() ? QString("-") : state.swVersion);
    setLabelValue(hlRfidHwVerVal, state.hwVersion.isEmpty() ? QString("-") : state.hwVersion);
    setLabelValue(hlRfidMfgVal, state.manufacturer.isEmpty() ? QString("-") : state.manufacturer);
    setLabelValue(hlRfidDevIdVal, state.deviceId.isEmpty() ? QString("-") : state.deviceId);
    setLabelValue(hlRfidProtoVerVal, state.hlProtoVer >= 0 ? QString::number(state.hlProtoVer) : QString("-"));
    setLabelValue(hlRfidProjectNoVal, state.hlProjectNo >= 0 ? QString::number(state.hlProjectNo) : QString("-"));
}
