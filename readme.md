# 飞秋 Mac 版 (Feiq for macOS)

基于 Qt 5 + C++11 的 macOS 版飞秋 — 局域网即时通讯工具。

实现了飞秋扩展协议（兼容 IPMSG），可与 Windows 版飞秋互通。核心引擎库与 UI 层完全分离（MVC 架构），引擎层零 Qt 依赖，理论上可移植到任意 Unix/Linux 平台。

---

## 功能特性

### 基础通讯
- ✅ 文本消息收发（与 Windows 飞秋互通）
- ✅ 文件收发（含拖拽）
- ✅ **文件夹传输**（递归目录收发）
- ✅ 表情收发（兼容飞秋表情格式）
- ✅ 窗口抖动
- ✅ **图片收发**（飞秋图片协议 + 聊天区内联显示）
- ✅ **图文混排**（文字 + 多张图片在同一气泡内联显示）
- ✅ **一对多群发**（BroadcastDialog 多选好友）
- ✅ 好友自动发现（UDP 广播）
- ✅ 手动指定 IP 添加好友
- ✅ 自定义网段穿透路由广播限制

### 界面体验
- ✅ **QQ 风格双窗口**（好友列表 + 独立聊天窗口）
- ✅ **消息气泡**（蓝色右对齐自发 / 白色左对齐对方，灰色背景）
- ✅ **好友三分组**：本机 / 在线好友 / 离线好友（含历史联系人）
- ✅ **与本机自对话**（不走网络，可作为记事本）
- ✅ 首字母彩色圆角矩形头像（颜色由 IP 哈希生成）
- ✅ 四色状态圆点（🟢 在线 / 🟡 离开 / 🔴 忙碌 / ⚫ 离线）
- ✅ 未读消息角标 + 来消息自动置顶
- ✅ 好友搜索
- ✅ 好友右键菜单（发消息 / 发文件 / 查看信息）
- ✅ QSS 统一样式表（现代 macOS 风格）
- ✅ 聊天窗口 Cmd+W 关闭

### 截图与图片
- ✅ **截图工具**（macOS 交互式框选，预览确认后发送）
- ✅ **Cmd+V 粘贴图片**（从任意 app 复制图片直接粘贴发送）
- ✅ 离线好友发送前立即拦截提示，不等超时

### 历史记录
- ✅ SQLite 持久化，打开聊天窗口自动加载最近 40 条
- ✅ 滚动到顶部自动加载更早历史（分页懒加载）
- ✅ **MAC 地址为主 key**：对端换 IP 后历史记录自动续接
- ✅ 友好时间格式（当天 / 昨天 / 本周 / 更早）

### 状态与通知
- ✅ 在线 / 离开 / 忙碌状态（协议级别收发）
- ✅ macOS 通知中心集成
- ✅ Dock 图标未读角标
- ✅ 状态栏显示在线好友数 + 本机 IP

### 智能功能
- ✅ **快捷回复**（输入框为空点发送弹菜单，默认 6 条，可自定义）
- ✅ **自动应答**（可配置回复文本，AUTORETOPT 协议防死循环）
- ✅ **可视化设置界面**（用户 / 网络 / 快捷回复 / 关于 四 Tab）
- ✅ 按沟通频率排序好友（可选插件）
- ✅ 插件管理（菜单入口，支持启用/禁用）
- ✅ 日志系统（可配置开关，默认关闭，写入 `~/.feiq/feiq.log`）

### 尚未实现
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

### 编译

```bash
# macOS (Homebrew 安装 Qt5)
brew install qt@5

cd feiq
/usr/local/opt/qt@5/bin/qmake feiq.pro -spec macx-clang CONFIG+=sdk_no_version_check
make -j$(sysctl -n hw.ncpu)
```

### 部署运行

> ⚠️ 必须严格按以下顺序执行，否则会出现崩溃或权限重复弹窗问题

```bash
# 1. 停止旧进程
pkill -x feiq 2>/dev/null; sleep 1

# 2. 打包 Qt 框架（自包含 .app）
/usr/local/opt/qt@5/bin/macdeployqt feiq.app

# 3. 安装到 /Applications（macOS 防火墙 UDP 白名单路径）
cp -R feiq.app /Applications/

# 4. ad-hoc 签名（让 macOS 记住录屏/UDP 权限，避免每次重新弹授权）
codesign --force --deep --sign - /Applications/feiq.app

# 5. 用 open 启动（避免 Homebrew Qt 双重加载导致崩溃）
open -a /Applications/feiq.app
```

**首次部署**后需在「系统设置 → 隐私与安全性 → 录屏与系统录音」手动授权一次，之后不再提示。

### 数据目录

所有用户数据统一存放于 `~/.feiq/`，首次启动自动创建：

| 文件 | 说明 |
|------|------|
| `~/.feiq/setting.ini` | 用户配置（旧路径自动迁移） |
| `~/.feiq/history.db` | 聊天记录 SQLite 数据库（旧路径自动迁移） |
| `~/.feiq/images/` | 收到的图片缓存（`<timestampMs>_<imageId>.jpg`） |
| `~/.feiq/feiq.log` | 调试日志（默认关闭） |

### 配置说明

通过菜单「设置 → 偏好设置」可视化修改，也可直接编辑 `~/.feiq/setting.ini`：

```ini
[user]
name = YourName              # 用户名（必填）

[app]
send_by_enter = true         # true: Enter 发送; false: Ctrl+Enter 发送
enable_notify = true         # 启用系统通知
auto_reply_enable = false    # 启用自动应答
auto_reply_text =            # 自动应答内容
quick_replies = 好的 👍\n收到！\n稍等一下  # 快捷回复（\n 分隔）
debug_log = false            # 启用调试日志（写入 ~/.feiq/feiq.log）

[network]
custom_group = 192.168.1.|10.0.0.  # 自定义广播网段（| 分隔，. 结尾扫描整段）

[rank_user]
enable = 1                   # 按沟通频率排序好友
```

---

## 项目架构

```
feiq/
├── feiqlib/            # 核心引擎库（纯 C++11，与 UI 无关）
│   ├── feiqengine      # 核心控制器（MVC Controller）
│   ├── feiqmodel       # 数据模型（好友列表管理）
│   ├── history         # 聊天记录（SQLite 持久化）
│   ├── feiqcommu       # 飞秋通讯封装（UDP/TCP）
│   ├── logger          # 日志系统（FEIQ_LOG 宏）
│   └── ...             # 编码转换、协议定义、工具类
│
├── osx/                # macOS 平台特有（通知中心、Dock Badge）
├── plugin/             # 插件系统（好友排序等）
├── res/                # 资源（QSS 样式表 + 96 个表情 GIF）
├── docs/               # 项目文档
│
├── mainwindow          # 主窗口（好友列表 + 三分组）
├── chatwindow          # 独立聊天窗口（含截图/粘贴/快捷回复）
├── fellowitemwidget    # 好友列表项自定义 Widget
├── fellowlistwidget    # 好友列表管理器（分组/排序/搜索）
├── settingsdialog      # 偏好设置对话框（四 Tab）
├── broadcastdialog     # 一对多群发对话框
├── recvtextedit        # 消息显示区（气泡 + 图片内联）
└── sendtextedit        # 消息输入区（支持 Cmd+V 粘贴图片）
```

详细架构说明见 [docs/architecture.md](docs/architecture.md)，协议文档见 [docs/protocol.md](docs/protocol.md)。

---

## 界面预览

### 好友列表
- 三分组：**本机**（自对话）/ **在线好友** / **离线好友**（含历史联系人）
- 首字母彩色圆角矩形头像
- 四色状态圆点

### 聊天窗口
- 蓝色气泡（自发，右对齐）/ 白色气泡（对方，左对齐）/ 灰色背景
- 图文混排内联显示
- 工具栏：表情 / 文件 / 图片 / 抖动 / 截图
- 输入框为空点发送 → 弹快捷回复菜单

---

## 已知问题

- QTextEdit 不支持 GIF 动画，表情只显示第一帧
- 接收多张大图时（Windows 飞秋同时发送），第三张及以后的图片可能传输中断（Windows 端限制）
