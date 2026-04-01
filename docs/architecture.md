# 飞秋 Mac 版 — 架构设计文档

> 最后更新：2026-04-01

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
8. [配置系统](#8-配置系统)
9. [编译与依赖](#9-编译与依赖)

---

## 1. 总体架构

项目采用 **MVC（Model-View-Controller）** 分层架构，核心引擎库与 UI 层完全分离：

```
┌─────────────────────────────────────────────────────────┐
│                    View 层 (Qt Widgets)                   │
│                                                           │
│  MainWindow ─┬─ FellowListWidget → FellowItemWidget     │
│              ├─ ChatWindow (多实例)                       │
│              │   ├─ RecvTextEdit (消息气泡)               │
│              │   ├─ SendTextEdit                          │
│              │   └─ ChooseEmojiDlg (表情选择)             │
│              ├─ SettingsDialog (偏好设置)                 │
│              ├─ SearchFellowDlg (搜索好友)               │
│              ├─ FileManagerDlg (文件传输管理)             │
│              └─ AddFellowDialog (手动添加好友)            │
│                                                           │
│  IFeiqView (接口) ◀── MainWindow 实现                    │
├───────────────────────────┬───────────────────────────────┤
│                           │                               │
│  Controller 层            │     Model 层                  │
│  FeiqEngine               │     FeiqModel                 │
│  ├─ 协议收发调度          │     ├─ Fellow 列表管理        │
│  ├─ 事件分发              │     └─ 好友增删改查           │
│  ├─ 文件传输协调          │                               │
│  └─ 自动应答              │     History (SQLite)          │
│                           │     └─ 聊天记录增删查         │
├───────────────────────────┴───────────────────────────────┤
│                     通讯层                                 │
│  FeiqCommu → UdpCommu (UDP 消息/广播)                     │
│            → TcpServer / TcpSocket (文件传输)             │
├───────────────────────────────────────────────────────────┤
│  平台层 (osx/)              │  插件层 (plugin/)           │
│  ├─ macOS 通知中心          │  ├─ IPlugin 接口            │
│  └─ Dock Badge              │  └─ RankUser 好友排序       │
└─────────────────────────────────────────────────────────────┘
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
│   ├── fellow.h             # 好友数据结构 + 状态枚举
│   ├── content.h            # 消息内容（文本/文件/振动等）
│   ├── post.h               # 接收到的网络数据包
│   ├── ifeiqview.h          # View 层接口定义（MVC 的 V 接口）
│   ├── protocol.h           # 收发协议抽象
│   ├── ipmsg.h              # IPMSG/飞秋协议常量定义
│   ├── feiqcommu.h/cpp      # 飞秋通讯封装
│   ├── udpcommu.h/cpp       # UDP 通讯实现
│   ├── tcpserver.h/cpp      # TCP 服务端（文件传输）
│   ├── tcpsocket.h/cpp      # TCP Socket 封装
│   ├── history.h/cpp        # 聊天记录（SQLite 持久化）
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
├── chatwindow.h/cpp         # 聊天窗口（独立窗口）
├── fellowitemwidget.h/cpp   # 好友列表项自定义 Widget
├── fellowlistwidget.h/cpp   # 好友列表管理器
├── settingsdialog.h/cpp     # 偏好设置对话框
├── recvtextedit.h/cpp       # 消息显示区（气泡样式）
├── sendtextedit.h/cpp       # 消息输入区
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
- **自动应答**：收到消息时自动回复，带 `IPMSG_AUTORETOPT` 标志防止死循环
- **间隔检测**：定时向自定义网段发送上线通知，穿透屏蔽广播包的路由器

```cpp
// 关键 API
pair<bool, string> start();                          // 启动引擎
pair<bool, string> send(fellow, content);            // 发送消息
pair<bool, string> sendFiles(fellow, files);         // 发送文件
void sendImOnLine(ip);                               // 发送上线通知
void sendAbsence(status);                            // 发送离开/忙碌状态
void setAutoReply(enable, text);                     // 设置自动应答
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
    // ...
};
```

#### History — 聊天记录

使用 **SQLite** 存储，支持按好友 IP 查询：

- 表结构：`chat_records(id, fellow_ip, fellow_name, fellow_mac, timestamp, is_self, content_type, content_text)`
- 支持新旧两套接口（新接口 `addRecord/queryByIp`，旧接口 `add/query` 兼容保留）
- `ChatWindow` 打开时自动加载最近 30 条历史记录

### 3.2 UI 层

#### MainWindow — 好友列表主窗口

类似 QQ 的好友列表窗口（280×600），包含：

- **搜索栏**：实时过滤好友
- **好友列表**：`FellowListWidget` 管理，支持在线/离线分组
- **状态栏**：显示在线好友数 / 总好友数 + 用户名 (IP)
- **菜单栏**：设置、插件管理、状态切换（在线/离开/忙碌）、添加好友等

MainWindow 实现了 `IFeiqView` 接口，作为引擎的事件回调目标。

#### ChatWindow — 独立聊天窗口

每个好友一个独立窗口（520×480），包含：

- **标题栏**：对方用户名 + IP
- **工具栏**：表情、发送文件、窗口抖动
- **消息显示区**：`RecvTextEdit`（气泡样式，自发绿色右对齐/对方白色左对齐）
- **输入区**：`SendTextEdit` + 发送按钮

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

- 头像：首字母彩色圆形（颜色由 IP 哈希决定）
- 状态圆点：🟢 在线 / 🟡 离开 / 🔴 忙碌 / ⚫ 离线
- 角标：未读消息数

#### SettingsDialog — 偏好设置

Tab 式设置界面：

| Tab | 设置项 |
|-----|--------|
| 👤 用户 | 用户名、主机名、发送方式（Enter/Ctrl+Enter） |
| 🌐 网络 | 自定义网段、通知开关、自动应答开关+回复文本 |

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
    → FeiqEngine::send(fellow, content)
      → ContentSender 序列化消息
        → FeiqCommu::send()
          → UdpCommu 发送 UDP 包
            → 启动 AsynWait 等待 SendCheck 回包
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

### 数据库 Schema

```sql
CREATE TABLE IF NOT EXISTS chat_records (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    fellow_ip     TEXT NOT NULL,
    fellow_name   TEXT,
    fellow_mac    TEXT,
    timestamp     INTEGER NOT NULL,   -- 毫秒级时间戳
    is_self       INTEGER NOT NULL,   -- 0=对方, 1=自己
    content_type  INTEGER NOT NULL,   -- ContentType 枚举
    content_text  TEXT                -- 文本内容或文件名
);
CREATE INDEX IF NOT EXISTS idx_fellow_ip ON chat_records(fellow_ip);
CREATE INDEX IF NOT EXISTS idx_timestamp ON chat_records(timestamp);
```

### 读写流程

- **写入**：消息收发时，`ChatWindow` 调用 `History::addRecord()` 写入
- **读取**：`ChatWindow` 打开时调用 `History::queryByIp()` 加载最近 30 条
- **使用 Prepared Statements** 避免 SQL 注入，提升性能

---

## 7. 好友列表管理

`FellowListWidget` 管理 `QListWidget`，功能包括：

### 分组显示

好友列表分为"在线"和"离线"两个分组，通过 QListWidgetItem 的 `Qt::UserRole+100` 标记分组 header：

```
📶 在线 (3)
├── 张三  192.168.1.10  🟢
├── 李四  192.168.1.11  🟡
└── 王五  192.168.1.12  🔴
📴 离线 (2)
├── 赵六  192.168.1.20  ⚫
└── 孙七  192.168.1.21  ⚫
```

### 排序策略

- 默认按 IP 排序
- 启用 RankUser 插件后按沟通频率排序
- 未读消息的好友自动置顶

### 右键菜单

好友列表项支持右键菜单：发送消息、发送文件、查看好友信息

---

## 8. 配置系统

配置文件路径：`~/.feiq_setting.ini`，通过 `Settings` 类（QSettings 封装）管理。

### 配置项一览

| 配置键 | 说明 | 默认值 |
|--------|------|--------|
| `user/name` | 用户名 | 系统用户名 |
| `user/host` | 主机名 | 系统主机名 |
| `app/title` | 窗口标题 | "飞秋" |
| `app/send_by_enter` | 发送方式 | 0（Ctrl+Enter 发送） |
| `app/enable_notify` | 启用通知 | true |
| `app/auto_reply_enable` | 启用自动应答 | false |
| `app/auto_reply_text` | 自动应答文本 | "" |
| `network/custom_group` | 自定义广播网段 | "" |
| `rank_user/enable` | 启用好友排序插件 | 1 |

---

## 9. 编译与依赖

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
# macOS (Homebrew Qt5)
cd feiq
/usr/local/opt/qt@5/bin/qmake feiq.pro -spec macx-clang CONFIG+=sdk_no_version_check
make -j$(sysctl -n hw.ncpu)

# 运行
open feiq.app
```

### Qt 模块依赖

```
QT += core gui network
# Qt5 额外:
QT += widgets
# macOS 额外:
QT += macextras
```

### 系统库依赖

```
LIBS += -liconv -lsqlite3
# macOS:
LIBS += -framework Foundation
```
