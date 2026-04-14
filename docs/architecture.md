# 飞秋 Mac 版 — 架构设计文档

> 最后更新：2026-04-14

---

## 目录

1. [总体架构](#1-总体架构)
2. [目录结构](#2-目录结构)
3. [核心模块详解](#3-核心模块详解)
   - 3.1 [引擎层（feiqlib/）](#31-引擎层feiqlib)
   - 3.2 [UI 层](#32-ui-层)
   - 3.3 [平台层（osx/）](#33-平台层osx)
   - 3.4 [插件层（plugin/）](#34-插件层plugin)
4. [数据流与消息传递](#4-数据流与消息传递)
5. [窗口管理模型](#5-窗口管理模型)
6. [聊天记录持久化](#6-聊天记录持久化)
7. [好友列表管理](#7-好友列表管理)
8. [图片收发机制](#8-图片收发机制)
9. [配置系统](#9-配置系统)
10. [日志系统](#10-日志系统)
11. [编译与部署](#11-编译与部署)

---

## 1. 总体架构

项目采用 **MVC（Model-View-Controller）** 分层架构，核心引擎库与 UI 层完全分离：

```
┌─────────────────────────────────────────────────────────────┐
│                    View 层 (Qt Widgets)                       │
│                                                               │
│  MainWindow ─┬─ FellowListWidget → FellowItemWidget         │
│              ├─ ChatWindow (多实例)                           │
│              │   ├─ RecvTextEdit (消息气泡)                   │
│              │   ├─ SendTextEdit (输入框，支持粘贴图片)        │
│              │   └─ ChooseEmojiDlg (表情选择)                 │
│              ├─ SettingsDialog (偏好设置)                     │
│              ├─ BroadcastDialog (一对多群发)                  │
│              ├─ SearchFellowDlg (搜索好友)                   │
│              ├─ FileManagerDlg (文件传输管理)                 │
│              └─ AddFellowDialog (手动添加好友)                │
│                                                               │
│  IFeiqView (接口) ◀── MainWindow 实现                        │
├───────────────────────────┬───────────────────────────────────┤
│                           │                                   │
│  Controller 层            │     Model 层                      │
│  FeiqEngine               │     FeiqModel                     │
│  ├─ 协议收发调度          │     ├─ Fellow 列表管理            │
│  ├─ 事件分发              │     └─ 好友增删改查               │
│  ├─ 文件/图片传输协调     │                                   │
│  └─ 自动应答              │     History (SQLite)              │
│                           │     └─ 聊天记录增删查             │
│                           │                                   │
│                           │     Logger (单例)                 │
│                           │     └─ 写 ~/.feiq/feiq.log        │
├───────────────────────────┴───────────────────────────────────┤
│                     通讯层                                     │
│  FeiqCommu → UdpCommu (UDP 消息/广播)                         │
│            → TcpServer / TcpSocket (文件/图片传输)            │
├───────────────────────────────────────────────────────────────┤
│  平台层 (osx/)              │  插件层 (plugin/)               │
│  ├─ macOS 通知中心          │  ├─ IPlugin 接口                │
│  └─ Dock Badge              │  └─ RankUser 好友排序           │
└───────────────────────────────────────────────────────────────┘
```

**核心设计原则：**

- **引擎层零 Qt 依赖**：`feiqlib/` 全部使用 C++11 标准库，理论上可移植到任意 Unix/Linux 平台
- **接口解耦**：引擎通过 `IFeiqView` 接口回调 UI 层，UI 层调用引擎的公开方法
- **跨线程安全**：引擎的网络线程通过 `MsgQueueThread` + Qt 信号槽机制安全地向 UI 线程分发事件

---

## 2. 目录结构

```
feiq/
├── feiqlib/                 # 核心引擎库（纯 C++11，与 UI 无关）
│   ├── feiqengine.h/cpp     # 核心控制器（MVC 的 C）
│   ├── feiqmodel.h/cpp      # 数据模型（MVC 的 M，好友列表）
│   ├── fellow.h             # 好友数据结构 + 状态枚举（含 isSelf 标记）
│   ├── content.h            # 消息内容（文本/文件/图片/振动等）
│   ├── post.h               # 接收到的网络数据包
│   ├── ifeiqview.h          # View 层接口定义（MVC 的 V 接口）
│   ├── protocol.h           # 收发协议抽象
│   ├── ipmsg.h              # IPMSG/飞秋协议常量定义
│   ├── feiqcommu.h/cpp      # 飞秋通讯封装
│   ├── udpcommu.h/cpp       # UDP 通讯实现
│   ├── tcpserver.h/cpp      # TCP 服务端（文件/图片传输）
│   ├── tcpsocket.h/cpp      # TCP Socket 封装
│   ├── history.h/cpp        # 聊天记录（SQLite 持久化，MAC 为主 key）
│   ├── logger.h             # 日志系统（单例，FEIQ_LOG 宏，写文件）
│   ├── filetask.h/cpp       # 文件传输任务
│   ├── encoding.h/cpp       # 字符编码转换（GBK ↔ UTF-8）
│   ├── msgqueuethread.h     # 消息队列线程（跨线程通信）
│   ├── asynwait.h/cpp       # 异步等待（等待回包/超时）
│   ├── parcelable.h         # 序列化接口
│   ├── uniqueid.h/cpp       # 唯一 ID 生成器
│   ├── utils.h/cpp          # 工具函数
│   └── defer.h/cpp          # RAII 延迟执行
│
├── osx/                     # macOS 平台特有代码
│   ├── notification.h/mm    # macOS 通知中心 API
│   ├── notificationimpl.h/mm # 通知委托实现
│   └── osxplatform.h/cpp    # Dock Badge 等平台功能
│
├── plugin/                  # 插件系统
│   ├── iplugin.h/cpp        # 插件接口 + PluginManager
│   └── rankuser.h/cpp       # 好友排序插件（按沟通频率）
│
├── res/                     # 资源文件
│   ├── style.qss            # 全局 QSS 样式表
│   └── *.gif                # 表情图片（96 个）
│
├── docs/                    # 项目文档
│   ├── protocol.md          # 飞秋/IPMSG 协议文档
│   └── architecture.md      # 本文档（架构设计）
│
├── mainwindow.h/cpp/ui      # 主窗口（好友列表）
├── chatwindow.h/cpp         # 聊天窗口（独立窗口，支持截图预览/粘贴图片）
├── fellowitemwidget.h/cpp   # 好友列表项自定义 Widget
├── fellowlistwidget.h/cpp   # 好友列表管理器（三分组：本机/在线/离线）
├── settingsdialog.h/cpp     # 偏好设置对话框（含快捷回复 Tab）
├── broadcastdialog.h/cpp    # 一对多群发对话框
├── recvtextedit.h/cpp       # 消息显示区（气泡样式，支持内联图片）
├── sendtextedit.h/cpp       # 消息输入区（支持 Cmd+V 粘贴图片）
├── searchfellowdlg.h/cpp/ui # 搜索好友对话框
├── filemanagerdlg.h/cpp     # 文件传输管理对话框
├── addfellowdialog.h/cpp/ui # 添加好友对话框
├── chooseemojidlg.h/cpp/ui  # 表情选择对话框
├── chooseemojiwidget.h/cpp  # 表情选择 Widget
├── emoji.h/cpp              # 表情解析
├── feiqwin.h/cpp            # 顶层窗口包装（管理 MainWindow 生命周期）
├── settings.h/cpp           # QSettings 封装
├── platformdepend.h/cpp     # 平台适配抽象层
├── main.cpp                 # 程序入口
├── feiq.pro                 # qmake 项目文件
└── default.qrc              # Qt 资源文件
```

---

## 3. 核心模块详解

### 3.1 引擎层（feiqlib/）

#### FeiqEngine — 核心控制器

`FeiqEngine` 是整个应用的"大脑"，负责：

- **协议收发调度**：注册各种 `RecvProtocol`，根据命令字分派到对应处理函数
- **好友发现**：处理上线广播 `onBrEntry()`、下线 `onBrExit()`、状态变更 `onBrAbsence()`
- **消息处理**：`onMsg()` 解析接收到的文本/文件消息，分发给 View 层
- **文件传输**：通过 `TcpServer` 提供文件下载服务，`downloadFile()` 发起文件接收
- **图片传输**：实现飞秋图片协议（UDP 分片传输），`handleImageChunk()` 组装图片数据
- **自动应答**：收到消息时自动回复，带 `IPMSG_AUTORETOPT` 标志防止死循环
- **间隔检测**：定时向自定义网段发送上线通知，穿透屏蔽广播包的路由器
- **历史好友加载**：启动时从 History 查询所有历史好友，以离线状态显示在列表中

```cpp
// 关键 API
pair<bool, string> start();                          // 启动引擎
pair<bool, string> send(fellow, content);            // 发送消息
pair<bool, string> sendFiles(fellow, files);         // 发送文件
void sendImOnLine(ip);                               // 发送上线通知
void sendAbsence(status);                            // 发送离开/忙碌状态
void setAutoReply(enable, text);                     // 设置自动应答
History& getHistory();                               // 获取历史记录引用
```

#### FeiqModel — 数据模型

管理所有已发现的好友列表，提供增删改查：

```cpp
shared_ptr<Fellow> find(ip);                         // 按 IP 查找好友
shared_ptr<Fellow> add(fellow);                      // 添加好友
void remove(fellow);                                 // 移除好友（下线）
vector<shared_ptr<Fellow>> all();                    // 获取所有好友
```

#### Fellow — 好友数据结构

```cpp
struct Fellow {
    string ip, mac, name, host, group;
    AbsenceStatus absenceStatus;    // Online / Away / Busy / Offline
    bool mIsSelf;                   // 是否为本机（自己）
    // ...
};
```

`isSelf()` 标记用于区分本机条目——本机条目不走网络，发送消息直接本地回显。

#### History — 聊天记录

使用 **SQLite** 存储，支持按好友查询：

- 表结构：`chat_records(id, fellow_ip, fellow_name, fellow_mac, timestamp, is_self, content_type, content_text)`
- **用户唯一 Key**：以 MAC 地址为主 key，IP 为辅。查询时先按 MAC 查找（优先），找到后自动更新 IP 字段；MAC 查不到时降级按 IP 查（兼容无 MAC 客户端）。对端换 IP 后历史记录自动续接。
- `queryAllFellows()`：启动时调用，返回所有历史好友（按最后消息时间倒序）
- `queryByIp(ip, limit, offset)`：分页查询，支持滚动加载

#### Logger — 日志系统

```cpp
// 使用方式
FEIQ_LOG("收到消息: " << msg);

// 开启日志（默认关闭）
// ~/.feiq/setting.ini
[app]
debug_log=true
```

- 日志写入 `~/.feiq/feiq.log`
- 默认关闭，配置开启后追加写入
- 所有调试输出均使用 `FEIQ_LOG` 宏，不使用 `cout`

### 3.2 UI 层

#### MainWindow — 好友列表主窗口

类似 QQ 的好友列表窗口（280×600），包含：

- **搜索栏**：实时过滤好友
- **好友列表**：`FellowListWidget` 管理，支持三分组（本机/在线/离线）
- **状态栏**：显示在线好友数 / 总好友数 + 用户名 (IP)
- **菜单栏**：设置、插件管理、状态切换（在线/离开/忙碌）、一对多群发、添加好友等

MainWindow 实现了 `IFeiqView` 接口，作为引擎的事件回调目标。

#### ChatWindow — 独立聊天窗口

每个好友一个独立窗口（700×600），包含：

- **标题栏**：对方用户名 + IP
- **工具栏**（聊天记录与输入框之间）：表情、发送文件、发送图片、窗口抖动、截图
- **消息显示区**：`RecvTextEdit`（QQ 风格气泡，对方白色左对齐 / 自己蓝色右对齐）
- **截图预览条**：截图或粘贴图片后在输入框上方显示预览缩略图，确认后发送
- **输入区**：`SendTextEdit` + 快捷回复菜单 + 发送按钮

**快捷键：**
- `Cmd+W`：关闭聊天窗口
- `Cmd+V`：粘贴剪贴板图片（走截图预览流程）
- `Enter` / `Ctrl+Enter`：发送（可在设置中切换）

**空输入点发送**：弹出快捷回复下拉菜单，点击后填入输入框。

**本机自对话**：`isSelf()` 时消息不走网络，直接本地回显并记录历史（可用于记笔记）。

**离线保护**：所有发送操作（消息/文件/图片/截图/抖动）在发送前检查对方在线状态，离线时立即提示，不等待超时。

#### FellowItemWidget — 好友列表项

自定义 Widget，布局如下：

```
┌────────────────────────┐
│ ┌────┐                 │
│ │ 头像│ 用户名     🟢   │
│ │ 首字│ 192.168.1.x    │
│ └────┘          [3]    │
└────────────────────────┘
```

- 头像：首字母彩色圆角矩形（QQ 风格，颜色由 IP 哈希决定）
- 状态圆点：🟢 在线 / 🟡 离开 / 🔴 忙碌 / ⚫ 离线
- 角标：未读消息数

#### FellowListWidget — 好友列表管理器

三分组布局：

```
本机
└── [S] suwenkuang (你)       ← 本机条目，点击可自对话
📶 在线好友 (N)
├── [H] hooli  192.168.1.13  🟢
└── ...
📴 离线好友 (M)
├── [X] xxx    192.168.1.20  ⚫  ← 有历史记录的离线好友
└── ...
```

- 本机分组单独置顶，不计入在线/离线计数
- 离线好友：启动时从历史记录加载，以灰色样式显示
- 搜索、右键菜单、未读角标均跳过分组 header

#### SettingsDialog — 偏好设置

Tab 式设置界面：

| Tab | 设置项 |
|-----|--------|
| 👤 用户 | 用户名、发送方式（Enter/Ctrl+Enter） |
| 🌐 网络 | 自定义网段、通知开关、自动应答开关+回复文本 |
| ⚡ 快捷回复 | 每行一条快捷回复，可自由增删改 |
| ℹ️ 关于 | 版本信息、配置文件路径 |

主机名自动从系统获取（`QSysInfo::machineHostName()`），无需手动配置。

#### RecvTextEdit — 消息显示区

- QQ 风格气泡渲染（HTML + QTextBlockFormat）
- 自己发送：蓝色气泡 `#0099FF` + 白字，右对齐
- 对方发送：白色气泡 `#FFFFFF` + 深灰字，左对齐（头像缩进 44px）
- 图片内联显示：文本中的 `\x01IMG:imageId\x01` 标记在渲染时替换为 `<img>` 标签
- 友好时间格式：当天→HH:mm，昨天→昨天 HH:mm，一周内→星期X HH:mm，更早→日期
- 历史消息分页：打开加载最近 40 条，滚到顶部自动加载更早 40 条

#### SendTextEdit — 消息输入区

- 支持 `Cmd+V` 粘贴图片：拦截键盘事件，检测剪贴板图片数据，emit `pasteImage(QPixmap)` 信号
- 剪贴板中没有图片时，`Cmd+V` 正常粘贴文本

### 3.3 平台层（osx/）

- **macOS 通知中心**：接收消息时弹出系统通知，支持点击跳转和内联回复
- **Dock Badge**：未读消息数显示在 Dock 图标上

### 3.4 插件层（plugin/）

- **IPlugin**：插件基类，提供 `name()` 和 `init()` 接口
- **PluginManager**：单例，管理所有插件的注册和启用/禁用
- **RankUser**：好友排序插件，按沟通频率排序好友列表

---

## 4. 数据流与消息传递

### 接收消息流程

```
UDP 数据包到达
  → UdpCommu 接收
    → FeiqCommu 协议解析，生成 Post
      → FeiqEngine 根据 cmdId 分派到 onMsg()
        → 创建 ViewEvent
          → MsgQueueThread 入队
            → Qt 信号 feiqViewEvent 跨线程投递
              → MainWindow::handleFeiqViewEvent()
                → 有聊天窗口? → ChatWindow::handleViewEvent() 直接显示
                → 无聊天窗口? → 加入 mUnshownEvents + 系统通知 + 角标
```

### 发送消息流程

```
用户点击"发送"
  → ChatWindow::sendText()
    → 检查 isSelf? → 本地回显 + addRecord（不走网络）
    → 检查 isOnLine? → 否则立即提示"对方不在线"
    → FeiqEngine::send(fellow, content)
      → ContentSender 序列化消息
        → FeiqCommu::send()
          → UdpCommu 发送 UDP 包
            → 启动 AsynWait 等待 SendCheck 回包
```

### 图片接收流程

```
收到 IPMSG_SENDIMAGE UDP 包（含 imageId + packetNo）
  → FeiqEngine::handleImageStart()：创建 ImageChunkInfo 记录
  → TCP 连接到发送端，请求图片数据
    → handleImageChunk()：按 offset 顺序接收 512 字节块
      → 所有 chunk 收齐后：info.data.resize(totalSize) 精确截断
        → saveAndNotifyImage()：保存到 ~/.feiq/images/<ts>_<imageId>.jpg
          → emit IMAGE ViewEvent → ChatWindow 内联显示
  → 超时机制：5 秒未收到 chunk 的图片，保存已接收部分
```

### 跨线程通信机制

引擎的网络线程与 Qt UI 线程之间通过以下机制安全通信：

1. `MsgQueueThread<ViewEvent>` 将事件入队
2. 在 UI 线程注册的回调函数中取出事件
3. 通过 `emit feiqViewEvent(event)` 信号投递到 Qt 事件循环
4. `handleFeiqViewEvent` 在 UI 线程中安全处理

---

## 5. 窗口管理模型

采用类似 QQ 的**双窗口模式**：

- **MainWindow**：始终存在的好友列表窗口
- **ChatWindow**：按需创建的独立聊天窗口，每个好友最多一个

```cpp
// MainWindow 中的聊天窗口管理
unordered_map<const Fellow*, ChatWindow*> mChatWindows;

// 查找或创建聊天窗口
ChatWindow* findOrCreateChatWindow(const Fellow* fellow);

// 聊天窗口关闭时从 map 移除
void onChatWindowClosed(const Fellow* fellow);
```

未读消息管理：

```cpp
unordered_map<const Fellow*, list<UnshownMessage>> mUnshownEvents;
```

当用户打开某好友的聊天窗口时，该好友的所有未读消息会被 flush 到聊天窗口中。

---

## 6. 聊天记录持久化

### 数据库路径

`~/.feiq/history.db`（旧路径 `~/.feiq_history.db` 自动迁移）

### 数据库 Schema

```sql
CREATE TABLE IF NOT EXISTS fellows (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    ip         TEXT NOT NULL,
    mac        TEXT,
    name       TEXT,
    last_time  INTEGER
);

CREATE TABLE IF NOT EXISTS chat_records (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    fellow_ip     TEXT NOT NULL,
    fellow_name   TEXT,
    fellow_mac    TEXT,
    timestamp     INTEGER NOT NULL,   -- 毫秒级时间戳
    is_self       INTEGER NOT NULL,   -- 0=对方, 1=自己
    content_type  INTEGER NOT NULL,   -- ContentType 枚举
    content_text  TEXT                -- 文本内容或图片文件名
);
CREATE INDEX IF NOT EXISTS idx_fellow_ip ON chat_records(fellow_ip);
CREATE INDEX IF NOT EXISTS idx_timestamp ON chat_records(timestamp);
```

### 用户唯一 Key 设计

```
查找 fellow_id(ip, mac):
  1. mac 非空 → 按 mac 查 fellows 表
     找到 → 更新 ip 字段（对端换 IP 自动续接历史）
  2. mac 查不到 → 按 ip 查（兼容旧数据/无 MAC 客户端）
  3. 都找不到 → 插入新记录
```

这确保了：**对端换 IP 后历史记录不断裂**，同一台机器的聊天记录始终连续。

### 读写流程

- **写入**：消息收发时，`ChatWindow` 调用 `History::addRecord()` 写入
- **读取**：`ChatWindow` 打开时调用 `History::queryByIp(ip, limit, offset)` 分页加载，默认最近 40 条
- **历史好友**：启动时调用 `History::queryAllFellows()` 加载所有历史好友（以离线状态显示）
- **使用 Prepared Statements** 避免 SQL 注入，提升性能

---

## 7. 好友列表管理

`FellowListWidget` 管理 `QListWidget`，功能包括：

### 三分组显示

```
本机 (1)
└── suwenkuang
📶 在线好友 (3)
├── 张三  192.168.1.10  🟢
├── 李四  192.168.1.11  🟡（离开）
└── 王五  192.168.1.12  🔴（忙碌）
📴 离线好友 (2)
├── 赵六  192.168.1.20  ⚫
└── 孙七  192.168.1.21  ⚫
```

- 本机分组固定置顶，单独计数
- 分组 header 使用 `Qt::UserRole+100` 标记，搜索/右键/统计均跳过

### 排序策略

- 默认按 IP 排序
- 启用 RankUser 插件后按沟通频率排序
- 未读消息的好友自动置顶

### 右键菜单

好友列表项支持右键菜单：发送消息、发送文件、查看好友信息

---

## 8. 图片收发机制

### 图片存储

所有图片保存在 `~/.feiq/images/` 目录，文件名格式：

```
<timestampMs>_<imageId>.jpg    # 如：1744614528123_a8875087.jpg
```

时间戳前缀保证同一 `imageId` 的图片多次发送不覆盖旧文件。

### 图文混排

飞秋图文混排消息格式：`文字[占位符]文字`

处理流程：
1. `FeiqEngine::replaceImgPlaceholders()`：将 `/~#>imageId<B~` 替换为 `\x01IMG:imageId\x01` 标记，保持为单个 `TextContent`（一个气泡）
2. `RecvTextEdit::textToHtml()`：识别标记，按 `~/.feiq/images/*_imageId.jpg` 通配符查找本地文件
   - 文件存在 → 内联 `<img>` 标签（最大 300×300）
   - 文件不存在 → 显示"🖼 图片加载中..."占位
3. `ChatWindow::displayContentWithRetry()`：最多重试 20 次（每次 500ms），等待图片文件就绪后重新渲染

### 本机截图/粘贴图片流程

```
截图（screencapture -i）或 Cmd+V 粘贴
  → 保存到 /tmp/feiq_screenshot_时间戳.png
    → setScreenshotPreview()：在输入框上方显示预览缩略图（最大 240x160）
      → 用户按发送（或回车）
        → 对方/网络：sendFile() 以文件形式发送
        → 本机自对话：复制到 ~/.feiq/images/，ImageContent 内联显示
```

### 图片数据完整性

接收端关键处理：
- `handleImageChunk()` 最后：`info.data.resize(totalSize)` 精确截断到声明大小，去除最后 chunk 的多余字节
- 超时机制：某张图片 5 秒未收到新 chunk，强制保存已接收数据

---

## 9. 配置系统

配置文件路径：`~/.feiq/setting.ini`（旧路径 `~/.feiq_setting.ini` 自动迁移），通过 `Settings` 类（QSettings 封装）管理。

### 配置项一览

| 配置键 | 说明 | 默认值 |
|--------|------|--------|
| `user/name` | 用户名 | 系统用户名 |
| `app/send_by_enter` | 发送方式（true=Enter） | true |
| `app/enable_notify` | 启用通知 | true |
| `app/auto_reply_enable` | 启用自动应答 | false |
| `app/auto_reply_text` | 自动应答文本 | "" |
| `app/quick_replies` | 快捷回复列表（\n 分隔） | 内置6条 |
| `app/debug_log` | 开启日志文件 | false |
| `network/custom_group` | 自定义广播网段（\| 分隔） | "" |
| `rank_user/enable` | 启用好友排序插件 | 1 |

### 快捷回复默认值

```
好的 👍
收到！
稍等一下
马上来
在忙，等会回你
已处理，请查收
```

---

## 10. 日志系统

```cpp
// feiqlib/logger.h
class Logger {
public:
    static Logger& instance();
    void log(const string& msg);
    bool isEnabled() const;
};

#define FEIQ_LOG(x) \
    if (Logger::instance().isEnabled()) { \
        std::ostringstream _oss; _oss << x; \
        Logger::instance().log(_oss.str()); \
    }
```

- 日志文件：`~/.feiq/feiq.log`（追加写入）
- 通过配置 `app/debug_log=true` 开启
- 所有调试输出使用 `FEIQ_LOG`，不使用 `cout`（避免十六进制模式污染等问题）

---

## 11. 编译与部署

### 环境要求

| 依赖 | 版本 |
|------|------|
| Qt | 5.x（推荐 5.15） |
| C++ | C++11 |
| SQLite | 3.x |
| iconv | 系统自带 |
| macOS SDK | 10.13+ |

### 编译命令

```bash
cd feiq
/usr/local/opt/qt@5/bin/qmake feiq.pro -spec macx-clang CONFIG+=sdk_no_version_check
make -j$(sysctl -n hw.ncpu)
```

### 部署命令（完整，缺一不可）

```bash
pkill -x feiq 2>/dev/null; sleep 1
/usr/local/opt/qt@5/bin/macdeployqt feiq.app        # 打包 Qt 框架进 .app
cp -R feiq.app /Applications/
codesign --force --deep --sign - /Applications/feiq.app  # ad-hoc 签名（必须）
open -a /Applications/feiq.app                           # 用 open 启动（必须）
```

**注意事项：**
- 必须从 `/Applications` 启动，macOS 防火墙 UDP 白名单路径是 `/Applications/feiq.app`
- 必须用 `open` 命令启动，不能直接执行 `.app/Contents/MacOS/feiq`（否则继承 Homebrew 环境变量，导致 Qt 框架双重加载崩溃）
- ad-hoc 签名（`-`）让 macOS 识别为同一 app，录屏/UDP 权限不会因重新部署而重置
- 第一次部署后需在「系统设置 → 隐私与安全性 → 录屏与系统录音」手动授权一次

### Qt 模块依赖

```
QT += core gui network widgets macextras
```

### 系统库依赖

```
LIBS += -liconv -lsqlite3 -framework Foundation
```
