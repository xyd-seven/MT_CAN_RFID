#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QGridLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QDir>
#include <QTextStream>
#include <QIcon>
#include <QRegExp>
#include <QScrollBar>
#include <QInputDialog>

namespace {
const QStringList DeviceTypeNames = {
    "ZQWL-UCANFD-100C", "ZQWL-UCAN-101C", "ZQWL-UCANFD-100K", "ZQWL-UCAN-101K",
    "ZQWL-UCANFD-100E", "ZQWL-UCAN-101E", "ZQWL-UCANFD-200U", "ZQWL-UCAN-201U",
    "ZQWL-UCANFD-200C", "ZQWL-UCAN-201C", "ZQWL-UCAN-401U", "ZQWL-UCANFD-400U"
};

const int DeviceTypeIndexes[] = {42, 3, 42, 3, 42, 3, 41, 4, 41, 4, 200, 201};
constexpr int MaxManualPayloadBytes = 64;
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    canthread(nullptr),
    maxLogRows(5000),
    canStarted(false),
    stressRefreshTimer(new QTimer(this)),
    canAutoSaveCheckBox(nullptr),
    oneClickStartButton(nullptr),
    canDeviceStatusValue(nullptr),
    topCanStatusValue(nullptr),
    topRfidStatusValue(nullptr),
    topStressStatusValue(nullptr),
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
    qjRfidAddrValue(nullptr),
    qjRfidResultValue(nullptr),
    qjRfidAppStatusValue(nullptr),
    qjRfidAlarmValue(nullptr),
    qjRfidUidValue(nullptr),
    qjRfidPwdValue(nullptr),
    qjRfidModelValue(nullptr),
    qjRfidSupplierValue(nullptr),
    qjRfidSerialValue(nullptr),
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
    qjOtaAnomalyEnableCheck(nullptr),
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
    qjRfidVendorValue(nullptr),
    qjRfidModelCodeValue(nullptr),
    qjRfidFwStrValue(nullptr),
    qjRfidHwStrValue(nullptr)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/images/MT_RFID.png"));

    // Instantiate background services first to avoid nullpointer dereferences during UI setup/loading config
    canthread = new CANThread();
    qingjuCanManager = new QingjuCanManager(canthread, this);
    qingjuRfidService = new QingjuRfidService(qingjuCanManager, this);
    qingjuOtaService = new QingjuOtaService(qingjuCanManager, this);
    rs485Manager = new Rs485Manager(this);
    rs485RfidService = new Rs485RfidService(rs485Manager, this);
    serialOpened = false;
    devicePanel = nullptr;
    serialDevicePanel = nullptr;

    connect(rs485Manager, &Rs485Manager::frameReceived, this, [this](const QByteArray &data, const QString &decodeText) {
        addSerialFrameToList(false, data, decodeText);
    });
    connect(rs485Manager, &Rs485Manager::frameSent, this, [this](const QByteArray &data, const QString &decodeText) {
        addSerialFrameToList(true, data, decodeText);
    });
    connect(rs485Manager, &Rs485Manager::portDisconnected, this, &MainWindow::handleRs485Disconnect);
    connect(rs485RfidService, &Rs485RfidService::stateUpdated, this, &MainWindow::updateRs485RfidPanel);
    connect(rs485RfidService, &Rs485RfidService::commandFinished, this, &MainWindow::onRs485CommandFinished);

    ui->filterModeCombo->setCurrentIndex(2);
    ui->ABIT1Combo->setCurrentIndex(2);
    ui->protocolCombo->setCurrentIndex(0);
    ui->frameTypeCombo->setCurrentIndex(0);

    QStringList listHeader;
    listHeader << "时间" << "通道" << "收/发" << "ID" << "Frame" << "类型" << "DLC" << "CAN-FD" << "数据" << "协议解析";

    ui->tableWidget->setColumnCount(listHeader.count());
    ui->tableWidget->setHorizontalHeaderLabels(listHeader);
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
    canAutoSaveCheckBox = new QCheckBox(QStringLiteral("自动保存CAN"), ui->cleanListBtn->parentWidget());
    connect(canAutoSaveCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            bool needPrompt = logDirectory.isEmpty() || !QDir(logDirectory).exists();
            if (!needPrompt) {
                QMessageBox::StandardButton reply = QMessageBox::question(
                    this,
                    QStringLiteral("提示"),
                    QStringLiteral("当前已设置CAN日志保存目录为：\n%1\n\n是否需要修改保存目录？").arg(logDirectory),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No
                );
                if (reply == QMessageBox::Yes) {
                    needPrompt = true;
                }
            }

            if (needPrompt) {
                QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择CAN日志自动保存目录"), logDirectory);
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
        } else {
            rfidOnlineStatusValue->setText(QStringLiteral("离线"));
            rfidOnlineStatusValue->setStyleSheet("color: red; font-weight: bold;");

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
}

void MainWindow::setupRfidPanel()
{
    rfidTabs = new QTabWidget(ui->centralWidget);
    
    rfidStackedWidget = new QStackedWidget(rfidTabs);
    mtRfidPanel = createRfidMonitorTab(rfidStackedWidget);
    qjRfidPanel = createQjRfidMonitorPanel(rfidStackedWidget);
    bbRfidPanel = createBbRfidMonitorPanel(rfidStackedWidget);
    ffRfidPanel = createFfRfidMonitorPanel(rfidStackedWidget);
    
    rfidStackedWidget->addWidget(mtRfidPanel);
    rfidStackedWidget->addWidget(qjRfidPanel);
    rfidStackedWidget->addWidget(bbRfidPanel);
    rfidStackedWidget->addWidget(ffRfidPanel);
    
    rfidTabs->addTab(rfidStackedWidget, QStringLiteral("RFID监控"));
    
    QWidget *t2 = createStressTestTab(rfidTabs);
    rfidTabs->addTab(t2, QStringLiteral("压力测试"));
    
    QWidget *t3 = createOtaTab(rfidTabs);
    rfidTabs->addTab(t3, QStringLiteral("OTA升级"));
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

    layout->addWidget(topCanStatusValue, 0, 0);
    layout->addWidget(topRfidStatusValue, 0, 1);
    layout->addWidget(topStressStatusValue, 0, 2);
    layout->setColumnStretch(3, 1);
}

void MainWindow::setupCompactMainLayout()
{
    setMinimumSize(1280, 820);
    resize(1280, 820);

    const QList<QWidget *> centralChildren = ui->centralWidget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : centralChildren) {
        if (child->objectName().startsWith(QLatin1String("layoutWidget"))) {
            child->hide();
        }
    }

    ui->groupBox->setTitle(QStringLiteral("设备控制"));
    ui->groupBox->setFixedWidth(250);
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
    ui->groupBox_2->setFixedWidth(250);
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

    sendLayout->addWidget(new QLabel(QStringLiteral("ID"), sendPanel), 0, 0);
    sendLayout->addWidget(ui->sendIDEdit, 0, 1, 1, 2);
    sendLayout->addWidget(ui->sendBtn, 0, 3);
    sendLayout->addWidget(new QLabel(QStringLiteral("数据"), sendPanel), 1, 0);
    sendLayout->addWidget(ui->sendDataEdit, 1, 1, 1, 3);
    sendLayout->addWidget(ui->frameTypeCombo, 2, 0, 1, 2);
    sendLayout->addWidget(ui->protocolCombo, 2, 2);
    sendLayout->addWidget(ui->CANFDaccCheck, 2, 3);

    QVBoxLayout *groupLayout2 = new QVBoxLayout(ui->groupBox_2);
    groupLayout2->setContentsMargins(10, 20, 10, 10);
    groupLayout2->addWidget(sendPanel);

    ui->groupBox_3->setTitle(QStringLiteral("实时CAN日志"));
    QHBoxLayout *logLayout = new QHBoxLayout(ui->groupBox_3);
    logLayout->setContentsMargins(10, 20, 10, 10);
    logLayout->setSpacing(10);
    logLayout->addWidget(ui->tableWidget);

    QVBoxLayout *logButtonsLayout = new QVBoxLayout();
    logButtonsLayout->setContentsMargins(0, 0, 0, 0);
    logButtonsLayout->setSpacing(10);
    if (canAutoSaveCheckBox != nullptr) {
        logButtonsLayout->addWidget(canAutoSaveCheckBox);
    }
    logButtonsLayout->addWidget(ui->checkBox_4);
    show0x207LogCheck = new QCheckBox(QStringLiteral("显示0x207"), ui->groupBox_3);
    logButtonsLayout->addWidget(show0x207LogCheck);
    connect(show0x207LogCheck, &QCheckBox::stateChanged, this, [this](int) {
        saveAppConfig();
    });
    logButtonsLayout->addWidget(ui->cleanListBtn);
    if (saveCanLogButton != nullptr) {
        logButtonsLayout->addWidget(saveCanLogButton);
    }
    logButtonsLayout->addStretch();
    logLayout->addLayout(logButtonsLayout);

    // 左侧面板垂直容器布局
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    leftLayout->addWidget(ui->groupBox);
    leftLayout->addWidget(ui->groupBox_2);
    leftLayout->addStretch();

    // 建立整个主界面的动态 Grid 布局
    QGridLayout *mainLayout = new QGridLayout(ui->centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setHorizontalSpacing(10);
    mainLayout->setVerticalSpacing(10);

    if (statusGroup != nullptr) {
        mainLayout->addWidget(statusGroup, 0, 0, 1, 2);
    }
    mainLayout->addLayout(leftLayout, 1, 0);
    if (rfidTabs != nullptr) {
        mainLayout->addWidget(rfidTabs, 1, 1);
    }
    mainLayout->addWidget(ui->groupBox_3, 2, 0, 1, 2);

    mainLayout->setColumnStretch(0, 0); // 左栏固定
    mainLayout->setColumnStretch(1, 1); // 右栏伸缩
    mainLayout->setRowStretch(0, 0);    // 状态栏高度自适应
    mainLayout->setRowStretch(1, 0);    // 中间层高度自适应
    mainLayout->setRowStretch(2, 1);    // 底部日志框随窗口高度自动纵向伸缩

    connect(oneClickStartButton, &QPushButton::clicked, this, &MainWindow::oneClickStartCan);
    updateCanControlState(false, false, false);
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
        } else if (canInitialized) {
            topCanStatusValue->setText(QStringLiteral("CAN：已初始化"));
        } else if (deviceOpened) {
            topCanStatusValue->setText(QStringLiteral("CAN：设备已打开"));
        } else {
            topCanStatusValue->setText(QStringLiteral("CAN：未启动"));
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
            topRfidStatusValue->setText(QStringLiteral("RFID：未识别"));
            topRfidStatusValue->setStyleSheet(QString());
        }
    }
    if (!canStarted && stressTestService.stats().running) {
        stopStressTest(false);
        if (stressRemainingLabel != nullptr) {
            stressRemainingLabel->setText(QStringLiteral("测试中止：CAN未启动"));
        }
        logService.logRuntime(LogLevel::Warning, QStringLiteral("Stress test stopped because CAN is not started"));
    }
    updateControlsState();
}

bool MainWindow::isOtaRunning() const
{
    if (m_otaStressRunning) {
        return true;
    }
    if (appConfig.load().protocolMode == 1) {
        QingjuOtaService::State state = qingjuOtaService->state();
        return state == QingjuOtaService::State::QueryProgram ||
               state == QingjuOtaService::State::StartUpgrade ||
               state == QingjuOtaService::State::SendData ||
               state == QingjuOtaService::State::FinishUpgrade;
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
    const int protocolMode = protocolModeCombo != nullptr ? protocolModeCombo->currentIndex() : 0;
    const bool is485Mode = (protocolMode == 2 || protocolMode == 3);

    if (is485Mode) {
        const bool rs485Scanning = rs485RfidService != nullptr && rs485RfidService->isScanning();
        
        // 串口面板
        if (serialPortCombo != nullptr) serialPortCombo->setEnabled(!serialOpened);
        if (serialBaudRateCombo != nullptr) serialBaudRateCombo->setEnabled(!serialOpened);
        if (serialRefreshBtn != nullptr) serialRefreshBtn->setEnabled(!serialOpened);
        if (serialOpenCloseBtn != nullptr) serialOpenCloseBtn->setEnabled(!rs485Scanning);
        if (serialOneClickStartBtn != nullptr) serialOneClickStartBtn->setEnabled(!rs485Scanning);

        // BB 面板
        if (bbStartBtn != nullptr) bbStartBtn->setEnabled(serialOpened && !rs485Scanning);
        if (bbStopBtn != nullptr) bbStopBtn->setEnabled(serialOpened && rs485Scanning);
        if (bbQueryOnceBtn != nullptr) bbQueryOnceBtn->setEnabled(serialOpened && !rs485Scanning);
        if (bbQueryModeCombo != nullptr) bbQueryModeCombo->setEnabled(serialOpened && !rs485Scanning);
        if (bbHostPollPeriodSpin != nullptr) bbHostPollPeriodSpin->setEnabled(serialOpened && !rs485Scanning);
        if (bbQueryInfoBtn != nullptr) bbQueryInfoBtn->setEnabled(serialOpened);
        if (bbSetPowerBtn != nullptr) bbSetPowerBtn->setEnabled(serialOpened);
        if (bbQueryPowerBtn != nullptr) bbQueryPowerBtn->setEnabled(serialOpened);
        if (bbPowerSpin != nullptr) bbPowerSpin->setEnabled(serialOpened);

        // FF 面板
        if (ffStartBtn != nullptr) ffStartBtn->setEnabled(serialOpened && !rs485Scanning);
        if (ffStopBtn != nullptr) ffStopBtn->setEnabled(serialOpened && rs485Scanning);
        if (ffQueryOnceBtn != nullptr) ffQueryOnceBtn->setEnabled(serialOpened && !rs485Scanning);
        if (ffQueryModeCombo != nullptr) ffQueryModeCombo->setEnabled(serialOpened && !rs485Scanning);
        if (ffHostPollPeriodSpin != nullptr) ffHostPollPeriodSpin->setEnabled(serialOpened && !rs485Scanning);
        if (ffQueryInfoBtn != nullptr) ffQueryInfoBtn->setEnabled(serialOpened);
        if (ffRebootBtn != nullptr) ffRebootBtn->setEnabled(serialOpened);
        if (ffQuerySwitchBtn != nullptr) ffQuerySwitchBtn->setEnabled(serialOpened);
        if (ffSetPowerBtn != nullptr) ffSetPowerBtn->setEnabled(serialOpened);
        if (ffQueryPowerBtn != nullptr) ffQueryPowerBtn->setEnabled(serialOpened);
        if (ffPowerSpin != nullptr) ffPowerSpin->setEnabled(serialOpened);
        if (ffMixerSpin != nullptr) ffMixerSpin->setEnabled(serialOpened);
        if (ffIfAmpSpin != nullptr) ffIfAmpSpin->setEnabled(serialOpened);
        if (ffThrdSpin != nullptr) ffThrdSpin->setEnabled(serialOpened);
        if (ffSetDemodBtn != nullptr) ffSetDemodBtn->setEnabled(serialOpened);
        if (ffQueryDemodBtn != nullptr) ffQueryDemodBtn->setEnabled(serialOpened);

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
        ui->sendBtn->setEnabled(false);
        if (oneClickStartButton != nullptr) oneClickStartButton->setEnabled(false);
        
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
    const bool is485Mode = (protocolMode == 2 || protocolMode == 3);

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
        int queryMode = 1;
        if (protocolMode == 2) { // BB
            intervalMs = bbHostPollPeriodSpin->value();
            queryMode = bbQueryModeCombo->currentData().toInt();
        } else { // FF
            intervalMs = ffHostPollPeriodSpin->value();
            queryMode = ffQueryModeCombo->currentData().toInt();
        }
        rs485RfidService->startScan(intervalMs, queryMode);
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
    const bool is485Mode = (protocolMode == 2 || protocolMode == 3);

    stressTestService.stop();
    stressRefreshTimer->stop();

    if (is485Mode) {
        rs485RfidService->stopScan();
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
    QGroupBox *rfidGroup = new QGroupBox(QStringLiteral("RFID定位器"), parent);

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

    layout->addWidget(controlGroup, 0, 0);
    layout->addWidget(tagGroup, 1, 0);
    layout->addWidget(statusGroup, 0, 1);
    layout->addWidget(deviceGroup, 1, 1);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);

    connect(rfidStartScanBtn, &QPushButton::clicked, this, [this]() {
        rfidScanning = true;
        sendRfidFrame(RfidProtocol::ControlFrameId, RfidProtocol::buildControlFrame(true));
    });
    connect(rfidStopScanBtn, &QPushButton::clicked, this, [this]() {
        rfidScanning = false;
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
    return rfidGroup;
}

QWidget *MainWindow::createStressTestTab(QWidget *parent)
{
    QWidget *stressWidget = new QWidget(parent);
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
    stressProgressBar->setFixedHeight(16);

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
    return stressWidget;
}

QWidget *MainWindow::createOtaTab(QWidget *parent)
{
    QWidget *otaWidget = new QWidget(parent);
    QGridLayout *layout = new QGridLayout(otaWidget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(6);

    otaQueryBtn = new QPushButton(QStringLiteral("查询APP/BOOT"), otaWidget);
    otaStartUpgradeBtn = new QPushButton(QStringLiteral("开始升级"), otaWidget);
    otaAbortUpgradeBtn = new QPushButton(QStringLiteral("中止升级"), otaWidget);
    otaSelectFileBtn = new QPushButton(QStringLiteral("选择固件"), otaWidget);

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
        }
    });
    connect(otaQueryBtn, &QPushButton::clicked, this, [this]() {
        if (appConfig.load().protocolMode == 1) { // 青桔协议
            if (canStarted) {
                qingjuOtaService->queryProgramStatus(selectedQingjuOtaTarget());
            } else {
                QMessageBox::warning(this, "警告", "请先启动 CAN 设备！");
            }
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

        if (appConfig.load().protocolMode == 1) { // 青桔协议
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
        if (appConfig.load().protocolMode == 1) { // 青桔协议
            if (m_otaStressRunning) {
                m_otaStressRunning = false;
                m_otaCooldownTimer->stop();
                ui->checkBox_4->setChecked(m_originalLogEnabled);
                updateOtaStressUI();
                logService.logRuntime(LogLevel::Info, "Qingju OTA stress test manually aborted.");
            }
            qingjuOtaService->abortUpgrade();
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

    return otaWidget;
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
}

void MainWindow::handleRfidFrame(const CanFrame &frame)
{
    if (rfidService.handleFrame(frame)) {
        updateRfidPanel(rfidService.state());
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
    if (topRfidStatusValue != nullptr) {
        const QString tagText = state.tag.isEmpty() ? QStringLiteral("未识别") : state.tag;
        topRfidStatusValue->setText(QStringLiteral("RFID：%1").arg(tagText));
        topRfidStatusValue->setStyleSheet(QString());
    }
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
    if (protocolModeCombo == nullptr || protocolModeCombo->currentIndex() != 1 || !frame.extendedFrame) {
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
        protocolModeCombo->setCurrentIndex(qBound(0, config.protocolMode, 3));
        onProtocolModeChanged(protocolModeCombo->currentIndex());
    }
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
        }
    }

    config.logDirectory = logDirectory;
    appConfig.save(config);
    logService.logRuntime(LogLevel::Info, QStringLiteral("Application settings saved"));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    const bool stressRunning = stressTestService.stats().running;
    const bool otaRunning = isOtaRunning();

    if (stressRunning || otaRunning) {
        QString taskName = otaRunning ? QStringLiteral("OTA固件升级") : QStringLiteral("压力测试");
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
        otaService.abortUpgrade();
    }
    if (stressRunning) {
        rfidScanning = false;
        stressTestService.stop();
    }

    // 关闭 485
    if (rs485RfidService != nullptr) {
        rs485RfidService->stopScan();
    }
    if (rs485Manager != nullptr) {
        rs485Manager->closePort();
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
    }
}

void MainWindow::on_cleanListBtn_clicked()
{
    ui->tableWidget->setRowCount(0);
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

    QStringList byteTextList = ui->sendDataEdit->text().split(QRegExp("\\s+"), Qt::SkipEmptyParts);
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
    QScrollBar *vBar = ui->tableWidget->verticalScrollBar();
    bool wasAtBottom = (vBar == nullptr || vBar->value() == vBar->maximum());

    while (ui->tableWidget->rowCount() >= maxLogRows) {
        ui->tableWidget->removeRow(0);
    }
    const int row = ui->tableWidget->rowCount();
    ui->tableWidget->insertRow(row);
    for(int i = 0; i < strList.count();i ++)
    {
        QTableWidgetItem *item = new QTableWidgetItem(strList.at(i),0);
        ui->tableWidget->setItem(row, i, item);
        if(i != strList.count() - 1)
            item->setTextAlignment(Qt::AlignCenter | Qt::AlignHCenter);
    }
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
    AppConfigData config = appConfig.load();
    config.protocolMode = index;
    appConfig.save(config);

    // 强制关闭所有通道，清理资源，达到物理通道的隔离！
    if (index == 0 || index == 1) { // CAN 模式下，强制停止 485 并关闭串口
        if (rs485RfidService != nullptr && rs485RfidService->isScanning()) {
            rs485RfidService->stopScan();
        }
        if (rs485Manager != nullptr && rs485Manager->isOpen()) {
            rs485Manager->closePort();
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

    if (index == 0) { // 美团协议
        qingjuRfidService->stopScan();
        if (topRfidStatusValue != nullptr) {
            topRfidStatusValue->setText(QStringLiteral("RFID：未识别"));
            topRfidStatusValue->setStyleSheet(QString());
        }
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
        if (otaQueryBtn != nullptr) otaQueryBtn->setText(QStringLiteral("查询目标 APP/BOOT"));
        if (otaStartUpgradeBtn != nullptr) otaStartUpgradeBtn->setText(QStringLiteral("开始目标升级"));
        
        otaStateValue->setText(qingjuOtaService->stateText());
        otaMessageValue->setText(qingjuOtaService->lastMessage().isEmpty() ? QStringLiteral("选择 NPK/RFR 后，点击“开始目标升级”或“查询目标 APP/BOOT”启动") : qingjuOtaService->lastMessage());
    } else { // RS485 协议（2: BB, 3: FF）
        int mode = index;
        rs485RfidService->setProtocolMode(mode);
        rs485Manager->setProtocolMode(mode);

        if (devicePanel != nullptr) devicePanel->hide();
        if (serialDevicePanel != nullptr) serialDevicePanel->show();
        if (rfidTabs != nullptr) {
            rfidTabs->setTabEnabled(2, false); // 禁用 OTA升级 Tab
            if (rfidTabs->currentIndex() == 2) {
                rfidTabs->setCurrentIndex(0); // 跳回监控页
            }
        }

        if (mode == 2) { // BB
            if (rfidStackedWidget != nullptr && bbRfidPanel != nullptr) {
                rfidStackedWidget->setCurrentWidget(bbRfidPanel);
            }
        } else { // FF
            if (rfidStackedWidget != nullptr && ffRfidPanel != nullptr) {
                rfidStackedWidget->setCurrentWidget(ffRfidPanel);
            }
        }

        if (stressStatsStackedWidget != nullptr && rs485StressPanel != nullptr) {
            stressStatsStackedWidget->setCurrentWidget(rs485StressPanel);
        }
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
    QStringList valList = valStr.split(QRegExp("\\s+"), QString::SkipEmptyParts);
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

void MainWindow::onSerialOpenCloseClicked()
{
    if (serialOpened) {
        if (rs485RfidService->isScanning()) {
            rs485RfidService->stopScan();
        }
        rs485Manager->closePort();
        serialOpened = false;
        serialStatusLabel->setText(QStringLiteral("串口已关闭"));
        serialStatusLabel->setStyleSheet("color: red; font-weight: bold;");
        serialOpenCloseBtn->setText(QStringLiteral("打开串口"));
        if (topRfidStatusValue != nullptr) {
            topRfidStatusValue->setText(QStringLiteral("RFID：离线"));
            topRfidStatusValue->setStyleSheet("color: red; font-weight: bold;");
        }
    } else {
        QString portName = serialPortCombo->currentText();
        int baudRate = serialBaudRateCombo->currentData().toInt();
        if (portName.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("未选择串口！"));
            return;
        }
        if (rs485Manager->openPort(portName, baudRate)) {
            serialOpened = true;
            serialStatusLabel->setText(QStringLiteral("已连接 (%1, %2)").arg(portName).arg(baudRate));
            serialStatusLabel->setStyleSheet("color: green; font-weight: bold;");
            serialOpenCloseBtn->setText(QStringLiteral("关闭串口"));
            
            rs485RfidService->queryDeviceInfo();
        } else {
            QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("串口打开失败，请检查端口是否被占用"));
        }
    }
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
        if (protocolModeCombo->currentIndex() == 2) {
            rs485RfidService->startScan(bbHostPollPeriodSpin->value(), bbQueryModeCombo->currentData().toInt());
        } else {
            rs485RfidService->startScan(ffHostPollPeriodSpin->value(), ffQueryModeCombo->currentData().toInt());
        }
        updateControlsState();
    } else {
        QString portName = serialPortCombo->currentText();
        int baudRate = serialBaudRateCombo->currentData().toInt();
        if (portName.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("未选择串口！"));
            return;
        }
        if (rs485Manager->openPort(portName, baudRate)) {
            serialOpened = true;
            serialStatusLabel->setText(QStringLiteral("已连接 (%1, %2)").arg(portName).arg(baudRate));
            serialStatusLabel->setStyleSheet("color: green; font-weight: bold;");
            serialOpenCloseBtn->setText(QStringLiteral("关闭串口"));
            
            if (protocolModeCombo->currentIndex() == 2) {
                rs485RfidService->startScan(bbHostPollPeriodSpin->value(), bbQueryModeCombo->currentData().toInt());
            } else {
                rs485RfidService->startScan(ffHostPollPeriodSpin->value(), ffQueryModeCombo->currentData().toInt());
            }
        } else {
            QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("一键启动失败，无法打开串口"));
        }
        updateControlsState();
    }
}

void MainWindow::handleRs485Disconnect()
{
    if (serialOpened) {
        if (rs485RfidService->isScanning()) {
            rs485RfidService->stopScan();
        }
        rs485Manager->closePort();
        serialOpened = false;
        serialStatusLabel->setText(QStringLiteral("串口异常断开"));
        serialStatusLabel->setStyleSheet("color: red; font-weight: bold;");
        serialOpenCloseBtn->setText(QStringLiteral("打开串口"));
        if (topRfidStatusValue != nullptr) {
            topRfidStatusValue->setText(QStringLiteral("RFID：断开"));
            topRfidStatusValue->setStyleSheet("color: red; font-weight: bold;");
        }

        // 自动终止压测进程，防止界面状态挂死
        if (stressTestService.stats().running) {
            stopStressTest(false);
        }

        updateControlsState();
        QMessageBox::critical(this, QStringLiteral("串口断开"), QStringLiteral("检测到串口已异常断开，轮询停止"));
    }
}

void MainWindow::updateRs485RfidPanel(const Rs485State &state)
{
    if (state.protocolMode == 2) { // BB
        setLabelValue(bbTagIdVal, state.tagId.isEmpty() ? QStringLiteral("-") : state.tagId);
        setLabelValue(bbPowerVal, state.transmitPower >= 0 ? QString("%1 (0.01dBm)").arg(state.transmitPower) : QString("-"));
        
        setLabelValue(bbRfidMfgVal, state.manufacturer.isEmpty() ? QString("-") : state.manufacturer);
        setLabelValue(bbRfidDevIdVal, state.deviceId.isEmpty() ? QString("-") : state.deviceId);
        setLabelValue(bbRfidHwVerVal, state.hwVersion.isEmpty() ? QString("-") : state.hwVersion);
        setLabelValue(bbRfidSwVerVal, state.swVersion.isEmpty() ? QString("-") : state.swVersion);
    } else { // FF
        setLabelValue(ffTagIdVal, state.tagId.isEmpty() ? QStringLiteral("-") : state.tagId);
        setLabelValue(ffPowerVal, state.transmitPower >= 0 ? QString("%1 (0.01dBm)").arg(state.transmitPower) : QString("-"));
        setLabelValue(ffMixerVal, state.ffMixer >= 0 ? QString::number(state.ffMixer) : QString("-"));
        setLabelValue(ffIfAmpVal, state.ffIfAmp >= 0 ? QString::number(state.ffIfAmp) : QString("-"));
        setLabelValue(ffThrdVal, state.ffThrd >= 0 ? QString::number(state.ffThrd) : QString("-"));
        setLabelValue(ffCardSwitchVal, state.ffCardSwitch ? QStringLiteral("启用") : QStringLiteral("关闭"));

        setLabelValue(ffRfidMfgVal, state.manufacturer.isEmpty() ? QString("-") : state.manufacturer);
        setLabelValue(ffRfidDevIdVal, state.deviceId.isEmpty() ? QString("-") : state.deviceId);
        setLabelValue(ffRfidHwVerVal, state.hwVersion.isEmpty() ? QString("-") : state.hwVersion);
        setLabelValue(ffRfidSwVerVal, state.swVersion.isEmpty() ? QString("-") : state.swVersion);
    }

    // 更新压测数据
    if (stressTestService.stats().running) {
        const int protocolMode = protocolModeCombo != nullptr ? protocolModeCombo->currentIndex() : 0;
        if (protocolMode == 2 || protocolMode == 3) {
            stressTestService.handleRs485State(
                state.protocolMode,
                state.tagId,
                state.errCode,
                state.isCommunicationTimeout,
                state.errorMsg
            );
            updateStressTestPanel(stressTestService.stats());
        }
    }
}

void MainWindow::addSerialFrameToList(bool isTx, const QByteArray &data, const QString &decodeText)
{
    if (!ui->checkBox_4->isChecked()) {
        return;
    }
    QStringList messageList;
    messageList << QDateTime::currentDateTime().time().toString("hh:mm:ss zzz");
    messageList << QStringLiteral("RS485");
    messageList << (isTx ? QStringLiteral("发送") : QStringLiteral("接收"));
    const int protocolMode = protocolModeCombo != nullptr ? protocolModeCombo->currentIndex() : 2;
    messageList << (protocolMode == 2 ? QStringLiteral("0xBB") : QStringLiteral("0xFF"));
    messageList << QStringLiteral("数据帧");
    messageList << QStringLiteral("-");
    messageList << QString::number(data.size());
    messageList << QStringLiteral("-");
    messageList << data.toHex(' ').toUpper();
    messageList << decodeText;
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

    infoGroup->setFixedWidth(240);
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
        rs485RfidService->startScan(bbHostPollPeriodSpin->value(), bbQueryModeCombo->currentData().toInt());
        updateControlsState();
    });
    connect(bbStopBtn, &QPushButton::clicked, this, [this]() {
        rs485RfidService->stopScan();
        updateControlsState();
    });
    connect(bbQueryOnceBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->triggerSingleQuery();
    });
    connect(bbQueryInfoBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->queryDeviceInfo();
    });
    connect(bbSetPowerBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->setPower(bbPowerSpin->value());
    });
    connect(bbQueryPowerBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->queryPower();
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

    ffRebootBtn = new QPushButton(QStringLiteral("设备重启"), controlGroup);
    ffQuerySwitchBtn = new QPushButton(QStringLiteral("启闭检测"), controlGroup);
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
    ffIfAmpSpin = new QSpinBox(demodGroup);
    ffIfAmpSpin->setRange(0, 7);
    ffIfAmpSpin->setPrefix(QStringLiteral("中频增益: "));
    ffThrdSpin = new QSpinBox(demodGroup);
    ffThrdSpin->setRange(0, 255);
    ffThrdSpin->setPrefix(QStringLiteral("解调阈值: "));
    
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

    infoGroup->setFixedWidth(240);
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
        rs485RfidService->startScan(ffHostPollPeriodSpin->value(), ffQueryModeCombo->currentData().toInt());
        updateControlsState();
    });
    connect(ffStopBtn, &QPushButton::clicked, this, [this]() {
        rs485RfidService->stopScan();
        updateControlsState();
    });
    connect(ffQueryOnceBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->triggerSingleQuery();
    });
    connect(ffRebootBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->ffReboot();
    });
    connect(ffQuerySwitchBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->ffQueryCardSwitch();
    });
    connect(ffQueryInfoBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->queryDeviceInfo();
    });
    connect(ffSetPowerBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->setPower(ffPowerSpin->value());
    });
    connect(ffQueryPowerBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->queryPower();
    });
    connect(ffSetDemodBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->ffSetDemodulatorParams(ffMixerSpin->value(), ffIfAmpSpin->value(), ffThrdSpin->value());
    });
    connect(ffQueryDemodBtn, &QPushButton::clicked, this, [this]() {
        if (!serialOpened) {
            QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("串口未打开"));
            return;
        }
        rs485RfidService->ffQueryDemodulatorParams();
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
