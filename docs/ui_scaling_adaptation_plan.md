# 小分辨率与高 DPI 屏幕 UI 自适应修改方案

## 1. 背景与目标

当前软件已启用 Qt 高 DPI 基础能力：

```cpp
QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
```

但主界面和部分业务面板仍存在较多固定尺寸、固定高度、固定宽度布局。高 DPI 或小分辨率环境下，Qt 会放大字体和控件，而固定尺寸区域没有同步扩展，容易出现：

- 字体裁切。
- 标签和值重叠。
- 面板高度不足。
- 日志区挤压上方状态区。
- 窗口超出屏幕可用区域。
- 表格和密集控件在 1366x768 或 150% 缩放下显示不完整。

本方案目标：

- 保证软件在小分辨率和高 DPI 屏幕下可用。
- 减少固定像素尺寸导致的遮挡和重叠。
- 让密集页面支持滚动，而不是强行压缩。
- 优先保持现有业务逻辑不变，仅调整布局和尺寸策略。

## 2. 当前问题点

### 2.1 固定主窗口尺寸

当前主界面存在：

```cpp
setMinimumSize(1280, 820);
resize(1280, 820);
```

风险：

- 1366x768 屏幕下，窗口高度超过可用区域。
- Windows 125%/150% 缩放后，逻辑尺寸可用高度进一步变小。
- 用户无法把窗口缩小到可用范围。

### 2.2 固定宽度控制栏

当前存在类似：

```cpp
ui->groupBox->setFixedWidth(250);
ui->groupBox_2->setFixedWidth(250);
```

风险：

- 小屏下左侧固定宽度挤压右侧内容。
- 高 DPI 下文字变宽，250px 反而不够。
- 固定宽度无法跟随字体尺寸和屏幕宽度调整。

### 2.3 固定高度控件

当前部分控件使用固定高度或较小最小高度，例如：

- 压力测试进度条固定高度。
- 产线检测结果横幅固定高度。
- 部分信息面板固定宽度。

风险：

- 中文字体在高 DPI 下出现裁切。
- 进度条、标签、分组边框互相挤压。

### 2.4 密集页面没有滚动容器

RFID 监控、OTA、RS485、产线检测等页面信息密集，当前主要依赖布局压缩。

风险：

- 小屏下布局被压扁。
- 标签和值贴在一起。
- 下方日志区挤压上方状态区。

## 3. 适配原则

### 3.1 少用固定尺寸

优先使用：

- `setMinimumWidth`
- `setMinimumHeight`
- `setSizePolicy`
- `setColumnStretch`
- `setRowStretch`
- `QScrollArea`

避免：

- `setFixedWidth`
- `setFixedHeight`
- 过大的 `setMinimumSize`
- 使用固定像素字体大小强行撑 UI

### 3.2 小屏允许滚动

密集页面不应无限压缩。

当屏幕空间不足时，应允许页面纵向滚动。

优先对以下区域增加 `QScrollArea`：

- RFID 监控页。
- 产线检测页。
- OTA 升级页。
- RS485 设备信息页。
- RS485 OTA/高级参数页。

### 3.3 内容优先级

小屏下应保证关键操作和结果优先显示：

1. 连接状态。
2. 当前协议状态。
3. 主要操作按钮。
4. 产线 PASS / FAIL。
5. 当前进度和成功率。
6. 日志和详细字段。

日志区可以缩小或滚动，不能挤压核心结果区。

## 4. 总体改造方案

### 4.1 主窗口尺寸动态计算

将固定 `1280x820` 改为根据屏幕可用区域设置初始尺寸。

建议逻辑：

```cpp
QScreen *screen = QGuiApplication::primaryScreen();
QRect available = screen == nullptr ? QRect(0, 0, 1280, 820) : screen->availableGeometry();

const int targetWidth = qMin(1280, available.width() - 40);
const int targetHeight = qMin(820, available.height() - 60);

resize(qMax(1024, targetWidth), qMax(680, targetHeight));
setMinimumSize(1024, 680);
```

说明：

- 默认仍尽量使用 `1280x820`。
- 小屏下自动缩小到可用区域内。
- 最小尺寸建议降到 `1024x680`。
- 更小屏幕依赖滚动区域保证可用。

### 4.2 主内容区增加滚动能力

当前主界面中部包含左侧设备控制、右侧 RFID/压测/OTA 面板，下方还有日志表。

建议：

- 保留整体主布局。
- 中部业务区域使用 `QScrollArea` 包裹，避免小屏挤压。
- 日志表区域保留伸缩，但设置合理最小高度。

示意：

```text
运行状态
业务区域 QScrollArea
实时日志
```

小屏时：

- 业务区域内部滚动。
- 日志区可以保持较小高度。

### 4.3 左侧控制栏改为弹性宽度

将：

```cpp
setFixedWidth(250)
```

改为：

```cpp
setMinimumWidth(220)
setMaximumWidth(320)
setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred)
```

说明：

- 小屏最低 220。
- 高 DPI 下可扩展到 320。
- 不再死锁布局。

### 4.4 结果/状态类面板使用固定行高策略

对于产线检测这种结果区，应明确：

- 结果横幅有足够高度。
- 进度条独立一行。
- 统计项独立网格，不和日志区争抢高度。

建议：

```cpp
resultGroup->setMinimumHeight(300);
resultLayout->setRowMinimumHeight(0, 72); // 横幅
resultLayout->setRowMinimumHeight(1, 22); // 进度条
for (int row = 2; row <= 7; ++row) {
    resultLayout->setRowMinimumHeight(row, 22);
}
```

### 4.5 标签和值列统一最小宽度

避免中文字段名和值重叠。

建议封装辅助函数：

```cpp
void configureTwoColumnFormLayout(QGridLayout *layout)
{
    layout->setHorizontalSpacing(16);
    layout->setVerticalSpacing(6);
    layout->setColumnMinimumWidth(0, 72);
    layout->setColumnMinimumWidth(2, 72);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(3, 1);
}
```

适用区域：

- 产线检测结果区。
- RFID 状态区。
- 设备信息区。
- OTA 状态区。
- RS485 设备信息区。

## 5. 分模块修改建议

### 5.1 主界面框架

修改点：

- `setupCompactMainLayout()`
- 主窗口尺寸策略。
- 左侧控制栏固定宽度。
- 中间业务区域是否包 `QScrollArea`。

建议：

- 先调整 `setMinimumSize` 和 `resize`。
- 再取消固定宽度。
- 最后再逐步加滚动区域，降低一次性改动风险。

### 5.2 产线检测 Tab

当前产线检测 Tab 是高优先级适配对象。

建议：

- 结果区最小高度固定到 `300` 左右。
- 过程日志最小高度降低到 `120~160`，并允许伸缩。
- 页面整体放入 `QScrollArea`，小屏时可滚动。
- 大号 PASS / FAIL 横幅使用 `QFont`，不要用样式表 `font-size`。
- 避免横幅 `padding` 导致文本裁切。

推荐布局：

```text
扫码输入区
检测结果区
过程日志区
```

其中：

- 扫码输入区固定高度。
- 检测结果区固定最小高度。
- 日志区可伸缩。

### 5.3 RFID 监控页

问题：

- 美团诊断控制、状态、设备信息、TAG 分片较多。
- 小屏下左右两列容易互相挤压。

建议：

- RFID 监控页包一层 `QScrollArea`。
- 左右两列保持 `setColumnStretch(0, 1)` 和 `setColumnStretch(1, 1)`。
- 对每个 `QGroupBox` 设置合理 `minimumWidth`，但避免 `fixedWidth`。

### 5.4 压力测试页

建议：

- 统计面板使用网格最小列宽。
- 进度条不要过小，建议 `minimumHeight >= 18`。
- 参数区按钮在小屏下允许换行或使用多行布局。

### 5.5 OTA 页

OTA 页面控件多，建议：

- 使用 `QScrollArea`。
- 固件路径标签允许换行或 elide。
- 异常注入区域可折叠或放入滚动区域。
- 进度和状态固定在上方，详细参数可滚动。

### 5.6 RS485 页面

建议：

- 设备信息组不使用 `setFixedWidth(240)`，改为最小/最大宽度。
- BB/FF/哈啰参数区放入滚动区域。
- 日志区保留伸缩。

## 6. 实施步骤

### Step 1：主窗口基础适配

修改：

- 动态计算初始窗口大小。
- 降低最小窗口尺寸。
- 替换左侧固定宽度。

验证：

- 1366x768 / 100%
- 1920x1080 / 125%
- 1920x1080 / 150%

### Step 2：产线检测 Tab 适配

修改：

- 调整结果区高度和行高。
- 检查 PASS / FAIL / 待扫码文字完整显示。
- 过程日志不挤压结果区。
- 页面必要时加入滚动。

验证：

- 待扫码。
- 写入中。
- 测试中。
- PASS。
- FAIL。

### Step 3：RFID/OTA/RS485 密集页面滚动化

修改：

- 逐个页面加 `QScrollArea`。
- 取消固定宽度。
- 调整表单列宽。

验证：

- 每个协议模式切换正常。
- 小屏下可以通过滚动看到所有控件。
- 不影响已有业务逻辑。

### Step 4：高 DPI 细节修复

修改：

- 避免样式表固定 `font-size`。
- 检查图标、按钮高度、表格行高。
- 必要时根据 `fontMetrics()` 计算最小高度。

示例：

```cpp
int textHeight = label->fontMetrics().height();
label->setMinimumHeight(textHeight + 12);
```

## 7. 验证清单

### 7.1 分辨率矩阵

| 分辨率 | 缩放 | 必测 |
| --- | --- | --- |
| 1366x768 | 100% | 是 |
| 1366x768 | 125% | 是 |
| 1920x1080 | 100% | 是 |
| 1920x1080 | 125% | 是 |
| 1920x1080 | 150% | 是 |
| 2560x1440 | 150% | 建议 |

### 7.2 页面矩阵

| 页面 | 检查点 |
| --- | --- |
| 主界面 | 窗口不超屏，状态栏完整 |
| RFID监控 | 所有控制可见或可滚动 |
| 压力测试 | 统计值不重叠 |
| OTA升级 | 路径、状态、按钮不重叠 |
| 产线检测 | PASS/FAIL/待扫码完整，日志不挤压结果区 |
| RS485页面 | 参数和设备信息不重叠 |

### 7.3 交互检查

- 窗口缩小时不出现控件覆盖。
- 高 DPI 下中文不裁切。
- Tab 切换后布局不跳变。
- 日志持续刷新时布局不抖动。
- 表格列宽仍可读。

## 8. 风险与注意事项

- 一次性重构全部 UI 风险较高，建议分阶段提交。
- `QScrollArea` 加入后要注意 `widgetResizable=true`。
- 不能为了适配小屏而牺牲产线 PASS/FAIL 结果的可见性。
- 固定像素字体要谨慎，尤其是中文大字。
- 布局修改后需要完整回归协议切换、OTA、压测、串口页面。

## 9. 推荐优先级

| 优先级 | 内容 | 原因 |
| --- | --- | --- |
| P0 | 产线检测 Tab 布局修复 | 当前已出现重叠，影响产线使用 |
| P1 | 主窗口动态尺寸 | 小屏和高 DPI 的根因之一 |
| P1 | 密集页面滚动化 | 防止控件继续互相挤压 |
| P2 | 左侧固定宽度弹性化 | 改善小屏横向空间 |
| P2 | 全页面 DPI 回归 | 降低后续 UI 问题 |

## 10. 建议结论

当前软件具备 Qt 高 DPI 基础能力，但布局层面仍然偏固定尺寸。建议优先完成：

1. 产线检测 Tab 布局稳定。
2. 主窗口动态尺寸。
3. 业务页面滚动化。
4. 固定宽高逐步替换为弹性尺寸。

这样可以从根本上减少小分辨率和高 DPI 下的字体裁切、控件重叠、窗口超屏问题。

## 11. 第二轮 P2 实施记录

本轮已按 P2 适配思路完成以下落地修改：

- 主窗口初始化尺寸改为按主屏可用区域动态计算，保留 1280x820 的推荐尺寸，同时允许缩小到 1024x680。
- 左侧设备控制、手动发送区域从固定宽度改为 220~320 的弹性宽度。
- 产线检测页已放入 `QScrollArea`，小屏或高 DPI 下允许滚动查看完整内容。
- RFID 监控页已放入 `QScrollArea`，避免诊断控制、TAG、状态和设备信息区互相挤压。
- 压力测试页已放入 `QScrollArea`，并将进度条从固定高度改为 18~24 的弹性高度。
- OTA 升级页已放入 `QScrollArea`，异常注入、状态、压力测试参数等密集控件不再强行压缩。
- BB/FF 设备信息面板固定宽度已改为 220~320 的弹性宽度。

验证结果：

- `git diff --check` 通过，仅存在 Git 换行符提示。
- Release 构建通过，输出目录为 `CAN_RFID_Qt/release/`。
