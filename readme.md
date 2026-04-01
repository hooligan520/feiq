# 飞秋 Mac 版 (Feiq for macOS)

基于 Qt 5 + C++11 的 macOS 版飞秋 — 局域网即时通讯工具。

实现了飞秋扩展协议（兼容 IPMSG），可与 Windows 版飞秋互通。核心引擎库与 UI 层完全分离（MVC 架构），引擎层零 Qt 依赖，理论上可移植到任意 Unix/Linux 平台。

---

## 功能特性

### 基础通讯
- ✅ 文本消息收发（与 Windows 飞秋互通）
- ✅ 文件收发
- ✅ 表情收发（兼容飞秋表情）
- ✅ 窗口抖动
- ✅ 好友自动发现（UDP 广播）
- ✅ 手动指定 IP 添加好友
- ✅ 自定义网段穿透路由广播限制

### 界面体验
- ✅ **好友列表与聊天窗口分离**（类似 QQ 的双窗口模式）
- ✅ **消息气泡样式**（自发消息绿色右对齐 / 对方消息白色左对齐 + 时间戳）
- ✅ **好友列表自定义头像**（首字母彩色圆形头像 + 在线状态圆点 + 未读角标）
- ✅ **好友在线/离线分组显示**
- ✅ **QSS 统一样式表**（PingFang SC 字体，现代 macOS 风格）
- ✅ 好友搜索
- ✅ 好友列表右键菜单（发消息/发文件/查看信息）

### 状态与通知
- ✅ **在线/离开/忙碌状态**（协议级别收发 + 四色状态圆点 🟢🟡🔴⚫）
- ✅ macOS 通知中心集成（支持内联回复）
- ✅ Dock 图标未读角标
- ✅ 状态栏显示在线好友数 + 本机 IP

### 智能功能
- ✅ **自动应答**（可配置回复文本，带 AUTORETOPT 协议防死循环）
- ✅ **聊天记录持久化**（SQLite 存储，打开聊天窗口自动加载历史）
- ✅ 未读消息好友自动置顶
- ✅ 按沟通频率排序好友（可选插件）
- ✅ **可视化偏好设置**（用户信息/网络/自动应答）
- ✅ **插件管理**（菜单入口，支持启用/禁用）

### 尚未实现
- ❌ 图片收发（飞秋图片协议未完全破解）
- ❌ 文件夹传输（飞秋使用自定义协议）
- ❌ 群组功能
- ❌ 加密传输

---

## 快速开始

### 环境要求

| 依赖 | 版本 |
|------|------|
| Qt | 5.x（推荐 5.15） |
| C++ | C++11 |
| SQLite | 3.x（系统自带） |
| macOS SDK | 10.13+ |

### 编译 & 运行

```bash
# macOS (Homebrew 安装 Qt5)
brew install qt@5

cd feiq
/usr/local/opt/qt@5/bin/qmake feiq.pro -spec macx-clang CONFIG+=sdk_no_version_check
make -j$(sysctl -n hw.ncpu)

# 运行
open feiq.app
```

### 配置文件

配置文件路径：`~/.feiq_setting.ini`，也可通过菜单栏「设置 → 偏好设置」可视化修改。

```ini
[user]
name = YourName          # 用户名
host = YourMacbook       # 主机名

[app]
title = 飞秋              # 窗口标题
send_by_enter = 0        # 0: Ctrl+Enter 发送; 1: Enter 发送
enable_notify = true     # 启用系统通知
auto_reply_enable = false # 启用自动应答
auto_reply_text =        # 自动应答内容

[network]
custom_group = 192.168.74.|192.168.82.  # 自定义广播网段（穿透路由限制）

[rank_user]
enable = 1               # 按沟通频率排序好友
```

---

## 项目架构

```
feiq/
├── feiqlib/            # 核心引擎库（纯 C++11，与 UI 无关）
│   ├── feiqengine      # 核心控制器（MVC 的 Controller）
│   ├── feiqmodel       # 数据模型（好友列表管理）
│   ├── history         # 聊天记录（SQLite 持久化）
│   ├── feiqcommu       # 飞秋通讯封装
│   ├── udpcommu        # UDP 通讯
│   ├── tcpserver/socket # TCP 文件传输
│   └── ...             # 编码转换、协议定义、工具类
│
├── osx/                # macOS 平台特有（通知中心、Dock Badge）
├── plugin/             # 插件系统（好友排序等）
├── res/                # 资源（样式表 + 96 个表情 GIF）
├── docs/               # 项目文档
│
├── mainwindow          # 主窗口（好友列表）
├── chatwindow          # 独立聊天窗口
├── fellowitemwidget    # 好友列表项自定义 Widget
├── fellowlistwidget    # 好友列表管理器（分组/排序/搜索）
├── settingsdialog      # 偏好设置对话框
├── recvtextedit        # 消息显示区（气泡样式）
└── ...                 # 其他 UI 组件
```

---

## 设计文档

详细的设计文档位于 `docs/` 目录：

| 文档 | 说明 |
|------|------|
| [docs/architecture.md](docs/architecture.md) | **架构设计文档** — MVC 分层、核心模块详解、数据流、窗口管理、持久化方案、配置系统 |
| [docs/protocol.md](docs/protocol.md) | **飞秋/IPMSG 协议文档** — 数据包格式、命令字、选项标志、核心流程、飞秋扩展协议、加密协议、编码规范 |

---

## 界面预览

### 双窗口模式

好友列表窗口（280×600）+ 独立聊天窗口（520×480），每个好友一个聊天窗口。

### 好友列表

- 首字母彩色圆形头像（颜色由 IP 哈希生成）
- 四色状态圆点（🟢 在线 / 🟡 离开 / 🔴 忙碌 / ⚫ 离线）
- 未读消息角标
- 在线/离线分组 header

### 消息气泡

- 自发消息：绿色气泡，右对齐
- 对方消息：白色气泡，左对齐
- 文件消息：卡片式样式
- 时间戳显示

---

## 开发者

### 架构说明

界面的实现与飞秋协议部分完全分离：

- **feiqlib/** 是通信、协议解析、MVC 架构部分，基于 C++11 封装，仅使用标准库 + Unix API，理论上可移植到任意 Unix/Linux 系统
- **UI 层**基于 Qt 实现，使用了部分 macOS 平台特性，如需移植到其他平台，可参考 `osx/` 目录适配对应的 native 特性

目前用到的 macOS 平台特性：
1. Dock 图标上的 Badge 文本（未读消息小红点）
2. macOS 通知中心的通知消息（支持内联回复）

### 贡献

欢迎提交 Pull Request。引用代码请注明出处。

---

## 已知问题

- QTextEdit 不支持 GIF 动画，表情只显示第一帧
- 文件夹传输暂不支持（飞秋使用自定义协议）
- 图片收发暂不支持（飞秋图片数据协议未完全破解）
