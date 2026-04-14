# 飞秋 / IPMSG 协议文档

> 本文档基于本项目源码分析 + IPMSG Draft-9 官方协议规范 + 飞秋扩展协议逆向整理。
> 适用于本项目（macOS 版飞秋）的开发和维护。

---

## 目录

1. [协议概述](#1-协议概述)
2. [数据包格式](#2-数据包格式)
3. [命令字（Command）](#3-命令字command)
4. [选项标志（Option Flags）](#4-选项标志option-flags)
5. [核心流程](#5-核心流程)
   - 5.1 [用户发现（上线/下线）](#51-用户发现上线下线)
   - 5.2 [消息收发](#52-消息收发)
   - 5.3 [文件传输](#53-文件传输)
   - 5.4 [目录传输](#54-目录传输)
6. [飞秋扩展协议](#6-飞秋扩展协议)
   - 6.1 [版本号字段（MAC 地址嵌入）](#61-版本号字段mac-地址嵌入)
   - 6.2 [窗口抖动](#62-窗口抖动)
   - 6.3 [正在输入 / 输入结束](#63-正在输入--输入结束)
   - 6.4 [图片发送](#64-图片发送)
   - 6.5 [打开聊天窗口通知](#65-打开聊天窗口通知)
   - 6.6 [文本格式扩展](#66-文本格式扩展)
7. [加密协议](#7-加密协议)
8. [编码与字符集](#8-编码与字符集)
9. [常量速查表](#9-常量速查表)
10. [本项目实现状态](#10-本项目实现状态)

---

## 1. 协议概述

飞秋（FeiQ）基于日本人白水启章（Shirouzu Hiroaki）编写的 **IP Messenger (IPMSG)** 协议，并在此基础上进行了扩展。

### 基本特性

| 特性 | 说明 |
|:---|:---|
| 传输层 | UDP + TCP |
| 默认端口 | **2425**（TCP 和 UDP 共用） |
| 网络架构 | **无中心服务器**，P2P 模式 |
| UDP 用途 | 用户发现、消息收发、控制命令 |
| TCP 用途 | 文件和目录的实际数据传输 |
| 字符编码 | 默认 **GBK**（飞秋 Windows 版），可选 UTF-8 |
| 用户识别 | 基于 IP + MAC 地址 |

### 协议分层

```
┌──────────────────────────────┐
│  飞秋扩展协议                  │  窗口抖动、图片、输入状态...
├──────────────────────────────┤
│  IPMSG v9 协议               │  消息、文件传输、加密...
├──────────────────────────────┤
│  UDP / TCP (端口 2425)        │
├──────────────────────────────┤
│  IP 网络                      │
└──────────────────────────────┘
```

---

## 2. 数据包格式

所有 IPMSG/飞秋的 UDP 数据包和 TCP 握手包遵循统一的字符串格式：

```
Version : PacketNo : SenderName : SenderHost : CommandNo : AdditionalSection
```

### 字段说明

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `Version` | 字符串 | 协议版本。标准 IPMSG 为 `"1"`；飞秋在此字段中嵌入了扩展信息（见 [6.1](#61-版本号字段mac-地址嵌入)） |
| `PacketNo` | 整数 | 数据包序号，全局递增，用于消息确认和文件关联 |
| `SenderName` | 字符串 | 发送者用户名（GBK 编码） |
| `SenderHost` | 字符串 | 发送者主机名（GBK 编码） |
| `CommandNo` | 32位整数 | 命令字 = 基本命令（低 8 位）+ 选项标志（高 24 位） |
| `AdditionalSection` | 字节流 | 附加数据，格式取决于命令类型 |

### 分隔符

| 符号 | ASCII 值 | 用途 |
|:---|:---|:---|
| `:` | 0x3A | 字段分隔符（6 个字段之间） |
| `\0` | 0x00 | 消息文本与文件信息之间的分隔 |
| `\a` (BEL) | 0x07 | 多个文件信息之间的分隔（`FILELIST_SEPARATOR`） |
| `\b` (BS) | 0x08 | 名称中冒号的替代字符（`HOSTLIST_DUMMY`） |

### 名称中的冒号处理

由于冒号是字段分隔符，用户名和主机名中的冒号 `:` 在打包时会被替换为 `\b`（0x08），解包时再还原。

### 本项目打包实现

```cpp
// feiqcommu.cpp - pack()
stringstream os;
os << mVersion        // 版本号（含 MAC 等扩展信息）
   << sep             // ':'
   << packetNo        // 包序号
   << sep
   << mName           // 用户名（GBK）
   << sep
   << mHost           // 主机名（GBK）
   << sep
   << cmdId           // 命令字
   << sep;
sender.write(os);     // 附加数据
os.put(0);            // 终止符
```

### 本项目解包实现

```cpp
// feiqcommu.cpp - dumpRaw()
// 按 ':' 分割前 5 项：version, packetNo, pcName, host, cmdId
// 剩余部分作为 extra（附加数据）
```

---

## 3. 命令字（Command）

命令字是 32 位整数，**低 8 位**为基本命令 ID，**高 24 位**为选项标志。

### 提取方法

```cpp
#define IS_CMD_SET(cmd, test)  (((cmd) & 0xFF) == test)   // 检查命令 ID
#define IS_OPT_SET(cmd, opt)   (((cmd) & opt) == opt)     // 检查选项标志
```

### IPMSG 标准命令

| 常量 | 值 | 方向 | 说明 |
|:---|:---|:---|:---|
| `IPMSG_NOOPERATION` | `0x00` | - | 无操作 |
| `IPMSG_BR_ENTRY` | `0x01` | 广播 | 用户上线通知 |
| `IPMSG_BR_EXIT` | `0x02` | 广播 | 用户下线通知 |
| `IPMSG_ANSENTRY` | `0x03` | 单播 | 响应上线通知（我在线） |
| `IPMSG_BR_ABSENCE` | `0x04` | 广播 | 离开/忙碌状态变更，或用户名变更 |
| `IPMSG_BR_ISGETLIST` | `0x10` | 广播 | 请求主机列表（服务器模式） |
| `IPMSG_OKGETLIST` | `0x11` | 单播 | 可以提供列表 |
| `IPMSG_GETLIST` | `0x12` | 单播 | 获取列表 |
| `IPMSG_ANSLIST` | `0x13` | 单播 | 返回列表 |
| `IPMSG_SENDMSG` | `0x20` | 单播 | **发送消息** |
| `IPMSG_RECVMSG` | `0x21` | 单播 | 消息接收确认（附加区 = 原始 PacketNo） |
| `IPMSG_READMSG` | `0x30` | 单播 | 消息已读通知（封闭消息） |
| `IPMSG_DELMSG` | `0x31` | 单播 | 消息删除通知 |
| `IPMSG_ANSREADMSG` | `0x32` | 单播 | 响应已读通知 |
| `IPMSG_GETINFO` | `0x40` | 单播 | 请求协议版本信息 |
| `IPMSG_SENDINFO` | `0x41` | 单播 | 返回协议版本信息 |
| `IPMSG_GETABSENCEINFO` | `0x50` | 单播 | 查询是否离开 |
| `IPMSG_SENDABSENCEINFO` | `0x51` | 单播 | 回复离开状态 |
| `IPMSG_GETFILEDATA` | `0x60` | TCP | 请求文件数据 |
| `IPMSG_RELEASEFILES` | `0x61` | 单播 | 取消/释放文件 |
| `IPMSG_GETDIRFILES` | `0x62` | TCP | 请求目录数据 |
| `IPMSG_GETPUBKEY` | `0x72` | 单播 | 请求 RSA 公钥 |
| `IPMSG_ANSPUBKEY` | `0x73` | 单播 | 返回 RSA 公钥 |

### 飞秋扩展命令

| 常量 | 值 | 方向 | 说明 |
|:---|:---|:---|:---|
| `IPMSG_OPEN_YOU` | `0x77` | 单播 | 通知对方已打开聊天窗口（无附加数据） |
| `IPMSG_INPUTING` | `0x79` | 单播 | 正在输入（无附加数据） |
| `IPMSG_INPUT_END` | `0x7A` | 单播 | 输入结束（无附加数据） |
| `IPMSG_SENDIMAGE` | `0xC0` | 单播 | 发送图片 |
| `IPMSG_KNOCK` | `0xD1` | 单播 | 窗口抖动 |

---

## 4. 选项标志（Option Flags）

### 通用选项（适用于所有命令）

| 常量 | 值 | 说明 |
|:---|:---|:---|
| `IPMSG_ABSENCEOPT` | `0x00000100` | 离开/忙碌模式标记 |
| `IPMSG_SERVEROPT` | `0x00000200` | 服务器模式 |
| `IPMSG_DIALUPOPT` | `0x00010000` | 拨号用户 |
| `IPMSG_FILEATTACHOPT` | `0x00200000` | **附带文件信息** |
| `IPMSG_ENCRYPTOPT` | `0x00400000` | 消息已加密 |
| `IPMSG_UTF8OPT` | `0x00800000` | 使用 UTF-8 编码 |

### 发送命令选项（与 `IPMSG_SENDMSG` 配合）

| 常量 | 值 | 说明 |
|:---|:---|:---|
| `IPMSG_SENDCHECKOPT` | `0x00000100` | 要求接收方确认（发送 `IPMSG_RECVMSG`） |
| `IPMSG_SECRETOPT` | `0x00000200` | 封闭消息（对方打开后发送 `IPMSG_READMSG`） |
| `IPMSG_BROADCASTOPT` | `0x00000400` | 广播消息 |
| `IPMSG_MULTICASTOPT` | `0x00000800` | 多播消息 |
| `IPMSG_NOPOPUPOPT` | `0x00001000` | 不弹窗 |
| `IPMSG_AUTORETOPT` | `0x00002000` | 自动回复（防止 Ping-Pong 死循环） |
| `IPMSG_RETRYOPT` | `0x00004000` | 重发标志 |
| `IPMSG_PASSWORDOPT` | `0x00008000` | 密码保护 |
| `IPMSG_NOLOGOPT` | `0x00020000` | 不记录日志 |
| `IPMSG_NEWMUTIOPT` | `0x00040000` | 新的多播选项 |
| `IPMSG_NOADDLISTOPT` | `0x00080000` | 不自动添加到好友列表 |
| `IPMSG_READCHECKOPT` | `0x00100000` | 已读确认（Ver8+） |
| `IPMSG_SECRETEXOPT` | `READCHECKOPT \| SECRETOPT` | 扩展封闭消息 |

### 组合规则

```
CommandNo = 基本命令 | 选项1 | 选项2 | ...
```

**示例**：发送消息并要求确认回执：
```
CommandNo = IPMSG_SENDMSG | IPMSG_SENDCHECKOPT = 0x00000020 | 0x00000100 = 0x00000120
```

**防自动回复死循环**：
```cpp
#define IPMSG_NO_REPLY_OPTS (IPMSG_BROADCASTOPT | IPMSG_AUTORETOPT)
// 收到带有这些标志的消息时，不应自动回复
```

---

## 5. 核心流程

### 5.1 用户发现（上线/下线）

```
┌─────────┐                         ┌─────────┐
│  新用户  │                         │ 在线用户 │
└────┬────┘                         └────┬────┘
     │                                    │
     │── BR_ENTRY (广播 255.255.255.255)─>│  "我上线了"
     │                                    │
     │<──── ANSENTRY (单播) ──────────────│  "我在线，这是我的信息"
     │                                    │
     │  [更新好友列表]                      │  [更新好友列表]
```

#### 上线流程

1. 用户启动后，向广播地址 `255.255.255.255` 及自定义广播组发送 `IPMSG_BR_ENTRY`
2. 附加区包含用户名（GBK 编码）
3. 在线用户收到后：
   - 将新用户加入好友列表
   - 单播回复 `IPMSG_ANSENTRY`，附加区包含自己的用户名

#### 下线流程

1. 用户退出时，广播 `IPMSG_BR_EXIT`
2. 收到方将该用户标记为离线
3. **无需回复**

#### 本项目实现

```cpp
// 上线 — feiqengine.cpp
void FeiqEngine::sendImOnLine(const string& ip) {
    SendImOnLine imOnLine(mName);
    if (ip.empty()) {
        mCommu.send("255.255.255.255", imOnLine);    // 广播
        broadcastToCurstomGroup(imOnLine);            // 自定义网段
    } else {
        mCommu.send(ip, imOnLine);                    // 单播
    }
}

// 收到好友上线 — 自动回复 ANSENTRY
void FeiqEngine::onBrEntry(shared_ptr<Post> post) {
    AnsBrEntry ans(mName);
    mCommu.send(post->from->getIp(), ans);
}
```

### 5.2 消息收发

#### 发送消息

```
┌──────┐                              ┌──────┐
│ 发送方│                              │ 接收方│
└──┬───┘                              └──┬───┘
   │                                      │
   │── SENDMSG | SENDCHECKOPT ──────────>│  附加区 = 消息文本(GBK)
   │                                      │
   │<──────── RECVMSG ──────────────────│  附加区 = 原始 PacketNo
   │                                      │
   │  [清除等待超时]                        │
```

#### 附加区格式（纯文本消息）

```
消息文本(GBK编码)
```

如果消息包含格式信息（飞秋扩展）：

```
消息文本{格式信息}
```

#### 消息确认机制

1. 发送方设置 `IPMSG_SENDCHECKOPT` 选项
2. 接收方收到后回复 `IPMSG_RECVMSG`，附加区为原始 PacketNo
3. 发送方收到确认后清除超时等待
4. 若超时未收到确认，标记发送失败

#### 本项目实现

```cpp
// 发送文本 — SendTextContent
int cmdId() { return IPMSG_SENDMSG | IPMSG_SENDCHECKOPT; }
void write(ostream& os) {
    if (content->format.empty())
        os << encOut->convert(content->text);           // 纯文本
    else
        os << encOut->convert(content->text)
           << "{" << encOut->convert(content->format) << "}";  // 带格式
}

// 超时处理 — 5 秒未收到确认
mAsyncWait.addWaitPack(content->packetNo, handler, 5000);
```

### 5.3 文件传输

文件传输采用 **UDP 握手 + TCP 数据传输** 的混合模式。

#### 完整流程

```
┌──────┐                              ┌──────┐
│ 发送方│                              │ 接收方│
└──┬───┘                              └──┬───┘
   │                                      │
   │── SENDMSG | FILEATTACHOPT (UDP) ───>│  附加区 = 文本\0文件信息
   │                                      │
   │<────── TCP 连接 (端口 2425) ────────│  接收方主动连接
   │                                      │
   │<── GETFILEDATA (TCP) ──────────────│  packetNo:fileId:offset
   │                                      │
   │── 原始文件数据流 (TCP) ────────────>│  无格式，纯数据
   │                                      │
   │  [传输完成，TCP 断开]                  │
```

#### 文件信息附加区格式

消息的附加区中，文本内容和文件信息之间用 `\0` 分隔：

```
消息文本 \0 文件1信息 \a 文件2信息 \a ...
```

每个文件信息的格式：

```
fileId : filename : size : modifyTime : fileType [: 扩展属性...] \a
```

| 字段 | 格式 | 说明 |
|:---|:---|:---|
| `fileId` | 十进制整数 | 文件 ID，用于后续 TCP 请求 |
| `filename` | GBK 字符串 | 文件名（冒号用 `::` 转义） |
| `size` | 十六进制 | 文件大小（字节） |
| `modifyTime` | 十六进制 | 修改时间（Unix 时间戳） |
| `fileType` | 十六进制 | 文件类型（见文件类型常量） |

#### TCP 文件请求格式

接收方通过 TCP 连接到发送方端口 2425 后，发送标准 IPMSG 包：

```
Version:PacketNo:Name:Host:IPMSG_GETFILEDATA:packetNo:fileId:offset:
```

其中附加区内容：

```
packetNo : fileId : offset :
```

**注意**：这里的 `packetNo`、`fileId`、`offset` 均为**十六进制**。

#### 文件类型常量

| 常量 | 值 | 说明 |
|:---|:---|:---|
| `IPMSG_FILE_REGULAR` | `0x01` | 普通文件 |
| `IPMSG_FILE_DIR` | `0x02` | 目录 |
| `IPMSG_FILE_RETPARENT` | `0x03` | 返回上级目录 |
| `IPMSG_FILE_SYMLINK` | `0x04` | 符号链接 |
| `IPMSG_FILE_CDEV` | `0x05` | 字符设备（Unix） |
| `IPMSG_FILE_BDEV` | `0x06` | 块设备（Unix） |
| `IPMSG_FILE_FIFO` | `0x07` | FIFO 管道（Unix） |
| `IPMSG_FILE_RESFORK` | `0x10` | 资源 Fork（Mac） |

#### 文件属性选项

| 常量 | 值 | 说明 |
|:---|:---|:---|
| `IPMSG_FILE_RONLYOPT` | `0x0100` | 只读 |
| `IPMSG_FILE_HIDDENOPT` | `0x1000` | 隐藏 |
| `IPMSG_FILE_EXHIDDENOPT` | `0x2000` | macOS 隐藏 |
| `IPMSG_FILE_ARCHIVEOPT` | `0x4000` | 归档 |
| `IPMSG_FILE_SYSTEMOPT` | `0x8000` | 系统文件 |

#### 本项目实现

```cpp
// 文件信息打包 — SendFileContent
void write(ostream& os) {
    os << (char)0                          // 文本与文件信息分隔符
       << to_string(content->fileId) << sep
       << encOut->convert(filename) << sep
       << std::hex << content->size << sep // 十六进制大小
       << content->modifyTime << sep       // 十六进制时间
       << content->fileType << sep
       << FILELIST_SEPARATOR;              // 0x07 文件间分隔
}

// TCP 下载请求 — SendRequestFile
void write(ostream& os) {
    os << std::hex << packetNo << sep
       << fileid << sep
       << offset << sep;
}
```

### 5.4 目录传输

目录传输使用 `IPMSG_GETDIRFILES` 命令，与文件传输类似但数据格式更复杂：

```
header-size : filename : file-size : fileattr : contents-data
```

- 遍历目录时，遇到 `IPMSG_FILE_DIR` 表示进入子目录
- 遇到 `IPMSG_FILE_RETPARENT` 表示返回上级目录
- 递归处理直到传输完成

> ⚠️ **本项目状态**：目录传输尚未实现，收到目录类型文件时会回复 "Mac飞秋还不支持接收目录"。

---

## 6. 飞秋扩展协议

飞秋在标准 IPMSG 协议基础上增加了一些私有扩展，这些扩展未在 IPMSG 官方文档中定义。

### 6.1 版本号字段（MAC 地址嵌入）

标准 IPMSG 的版本号字段为 `"1"`，飞秋将其扩展为嵌入 MAC 地址和其他信息：

```
1_lbt6_0#128#<MAC地址>#0#0#0#4001#9
```

#### 字段解析

```
1_lbt6_0 # 128 # MAC地址 # 0 # 0 # 0 # 4001 # 9
  │         │      │       │   │   │    │      │
  │         │      │       │   │   │    │      └─ 协议版本 9
  │         │      │       │   │   │    └──────── 能力标志
  │         │      │       │   │   └───────────── 未知
  │         │      │       │   └───────────────── 未知
  │         │      │       └───────────────────── 未知
  │         │      └───────────────────────────── MAC 地址（12位十六进制，如 a1b2c3d4e5f6）
  │         └──────────────────────────────────── 未知标志
  └────────────────────────────────────────────── 飞秋版本标识
```

#### MAC 地址提取

```cpp
// feiqcommu.cpp - dumpVersionInfo()
// 提取第2个 '#' 和第3个 '#' 之间的内容作为 MAC 地址
VersionInfo info;
// version = "1_lbt6_0#128#a1b2c3d4e5f6#0#0#0#4001#9"
//                         ^               ^
//                       begin            end
info.mac = version.substr(begin, end - begin);
// info.mac = "a1b2c3d4e5f6"
```

#### 用途

- **识别同一用户的不同实例**：MAC 地址用于判断是否为同一台机器
- **屏蔽自己的包**：收到消息时，比对 MAC 地址过滤自己发出的广播包

### 6.2 窗口抖动

```
命令: IPMSG_KNOCK (0xD1)
附加区: 无
```

类似 QQ 的"窗口抖动"功能，发送后接收方窗口会震动。

#### 本项目实现

```cpp
// 发送
class SendKnockContent : public ContentSender {
    int cmdId() { return IPMSG_KNOCK; }
    void write(ostream& os) { (void)os; }  // 无附加数据
};

// 接收
class RecvKnock : public RecvProtocol {
    bool read(shared_ptr<Post> post) {
        if (IS_CMD_SET(post->cmdId, IPMSG_KNOCK)) {
            post->contents.push_back(make_shared<KnockContent>());
        }
        return false;
    }
};
```

### 6.3 正在输入 / 输入结束

```
正在输入: IPMSG_INPUTING (0x79)  — 无附加数据
输入结束: IPMSG_INPUT_END (0x7A) — 无附加数据
```

当用户在输入框中打字时，向对方发送"正在输入"状态；停止输入时发送"输入结束"。

> **注意**：根据代码注释，发送消息后也会跟一条 `IPMSG_INPUTING`，但不会跟 `INPUT_END`，原因不明。

> ⚠️ **本项目状态**：协议已定义，但 UI 层未实现显示。

### 6.4 图片发送

> ✅ **本项目状态**：已完整实现图片 UDP 分片接收与内联显示（2026-04-02 逆向完成）。

#### 6.4.1 协议概述

飞秋图片发送**不使用 TCP**，而是通过 **UDP 分片**直接将图片二进制数据嵌入多个 IPMSG 包中发送。这与文件传输（TCP）完全不同。

| 属性 | 说明 |
|:---|:---|
| 传输方式 | **UDP 分片**（每个 UDP 包独立的 IPMSG 报文） |
| 命令字 | `IPMSG_SENDIMAGE (0xC0)` |
| 选项标志 | `IPMSG_FILEATTACHOPT (0x00200000)` |
| 分片大小 | **512 字节**（固定，最后一个分片可能不足 512 字节） |

#### 6.4.2 文本消息中的图片占位符

发送图文混排消息时，飞秋 Windows 先发送文本包（`IPMSG_SENDMSG`），文本中用特殊占位符标记图片位置：

```
格式: /~#><imageId><B~
示例: "111/~#>a3b4c5d6<B~222" 表示文字 111 + 图片 + 文字 222
```

- `imageId`：十六进制字符串，8个十六进制字符（4字节），标识该图片
- 同一条消息中可以有多个图片占位符，对应多张图片
- 文本包发送后，图片分片包紧随其后异步发送（存在时序竞争）

#### 6.4.3 图片分片包格式

每个图片分片是一个完整的 IPMSG UDP 包（`IPMSG_SENDIMAGE | IPMSG_FILEATTACHOPT`），**extra 区**结构如下：

```
<元信息头>#\0<JPEG二进制数据>
           ↑↑
           ASCII '#' + NUL 字节，分隔元信息和二进制数据
```

**元信息头**格式（字段之间用 `|` 分隔）：

```
fields[0] = imageId       十六进制字符串（如 "a3b4c5d6"）
fields[1] = totalSize     图片总大小（十进制，单位字节）
fields[2] = offset        本分片在完整图片中的字节偏移（十进制）
fields[3] = totalChunks   图片总分片数（十进制，可能为0/省略）
fields[4] = chunkSeq      本分片序号（十进制，可能为0/省略）
fields[5] = chunkSize     本分片数据字节数（十进制，通常512）
fields[6..] = 其他保留字段
```

**示例元信息**（实际抓包）：
```
a3b4c5d6|353687|0|691|0|512|0|2|0|00000000#\0<512字节JPEG数据>
```

> ⚠️ **注意**：`totalSize`、`offset`、`chunkSeq` 均为十进制整数字符串，不是十六进制。
> 字段数量不固定，解析时应按序取 fields[0]、fields[1]、fields[2]，多余字段忽略。

#### 6.4.4 接收侧处理流程

```
收到 SENDIMAGE UDP 包
    │
    ├─ 解析 extra 中的 imageId 和 totalSize
    ├─ 找到或创建该 imageId 的缓存 buffer
    ├─ 将本分片二进制数据追加到 buffer
    │
    └─ 判断 buffer.size() >= totalSize？
           │Yes
           ├─ 截断到精确的 totalSize（最后一个分片可能有尾部填充）
           ├─ 保存到 ~/.feiq/images/<timestamp>_<imageId>.jpg
           └─ 通知 UI 显示图片（ViewEvent）
```

> ⚠️ **关键陷阱**：最后一个 chunk 的 UDP payload 固定 512 字节，但实际有效数据可能不足 512 字节。
> 追加后必须 `data.resize(totalSize)` 截断，否则 JPEG 文件尾部有垃圾数据，Qt 无法解码。

#### 6.4.5 时序问题

发送方先发文本包，再发图片分片，但由于网络原因，接收方可能：
1. 先收到文本包，图片文件还不存在 → 显示"图片加载中..."
2. 后续图片分片陆续到达 → 文件写入完成后 UI 轮询检测，最多重试 20×500ms=10秒

本项目采用**延迟重试**方案（`displayContentWithRetry`），而非 TCP 握手确认。

#### 6.4.6 本项目实现细节

**文本包处理（RecvText::replaceImgPlaceholders）**：
```
/~#>imageId<B~  →  \x01IMG:imageId\x01  （内部特殊标记）
```
文本内容保持为单个 TextContent（一个气泡），不拆分。

**图片包处理（RecvImage + FeiqEngine::handleImageChunk）**：
```cpp
// 关键数据结构
struct ImageChunkInfo {
    string imageId;         // 图片标识
    size_t totalSize;       // 图片总大小（来自第一个分片的 fields[1]）
    vector<char> data;      // 已累积的二进制数据
    shared_ptr<Fellow> from;
    long long lastChunkTime; // 最后收到分片的时间戳（ms），用于超时检测
};
```

**图片文件命名**：`<receiveTimestampMs>_<imageId>.jpg`
- 不同时刻收到的同一张图（相同 imageId）不会互相覆盖
- 历史记录中 contentText 字段存储完整文件名（含时间戳前缀）

**UI 渲染（RecvTextEdit::textToHtml）**：
- 识别 `\x01IMG:imageId\x01` 标记
- 在 `~/.feiq/images/` 下搜索 `*_imageId.jpg`，取最新文件
- 存在且可解码 → 内联 `<img>` 标签（最大 240×240）
- 不存在 → 显示"🖼 图片加载中..."占位符

### 6.5 打开聊天窗口通知

```
命令: IPMSG_OPEN_YOU (0x77)
附加区: 无
```

当用户打开与某好友的聊天窗口时发送此命令，对方可据此显示"对方已打开聊天窗口"。

> ⚠️ **本项目状态**：协议已定义，未实现。

### 6.6 文本格式扩展

飞秋在消息文本中嵌入格式信息：

```
消息文本{格式信息}
```

格式信息位于大括号 `{}` 中，紧跟在消息文本后面。具体格式内容为飞秋私有定义。

#### 本项目实现

```cpp
// 接收时解析
unique_ptr<TextContent> createTextContent(const string& raw) {
    auto begin = raw.find('{');
    auto end = raw.find("}", begin + 1);
    if (begin != npos && end != npos) {
        content->text = raw.substr(0, begin);           // 纯文本
        content->format = raw.substr(begin+1, end-begin-1); // 格式信息
    } else {
        content->text = raw;
    }
}
```

#### 特殊文本过滤

飞秋有些特殊消息以 `/~#>` 开头、`<B~` 结尾，这类消息会被过滤掉不显示：

```cpp
if (startsWith(tc->text, "/~#>") && endsWith(tc->text, "<B~")) {
    rejected = true;  // 过滤掉
}
```

---

## 7. 加密协议

IPMSG v9 支持基于 RSA + 对称密钥的加密消息传输。

### 加密能力标志

| 常量 | 值 | 说明 |
|:---|:---|:---|
| `IPMSG_RSA_512` | `0x00000001` | RSA 512 位 |
| `IPMSG_RSA_1024` | `0x00000002` | RSA 1024 位 |
| `IPMSG_RSA_2048` | `0x00000004` | RSA 2048 位 |
| `IPMSG_RC2_40` | `0x00001000` | RC2 40 位 |
| `IPMSG_RC2_128` | `0x00004000` | RC2 128 位 |
| `IPMSG_RC2_256` | `0x00008000` | RC2 256 位 |
| `IPMSG_BLOWFISH_128` | `0x00020000` | Blowfish 128 位 |
| `IPMSG_BLOWFISH_256` | `0x00040000` | Blowfish 256 位 |
| `IPMSG_AES_128` | `0x00100000` | AES 128 位 |
| `IPMSG_AES_192` | `0x00200000` | AES 192 位 |
| `IPMSG_AES_256` | `0x00400000` | AES 256 位 |
| `IPMSG_SIGN_MD5` | `0x10000000` | MD5 签名 |
| `IPMSG_SIGN_SHA1` | `0x20000000` | SHA1 签名 |

### 加密流程

```
┌──────┐                              ┌──────┐
│ 发送方│                              │ 接收方│
└──┬───┘                              └──┬───┘
   │                                      │
   │── GETPUBKEY (能力标志) ────────────>│
   │                                      │
   │<── ANSPUBKEY (能力标志:E:N) ───────│  E=公钥指数, N=模数
   │                                      │
   │  [生成对称密钥 K]                     │
   │  [用 RSA 公钥加密 K => EncK]          │
   │  [用 K 加密消息 => EncMsg]            │
   │                                      │
   │── SENDMSG|ENCRYPTOPT ─────────────>│  附加区 = 算法标志:EncK:EncMsg
   │                                      │  [用 RSA 私钥解密 EncK => K]
   │                                      │  [用 K 解密 EncMsg => 原文]
```

### 加密消息附加区格式

```
加密算法标志 : RSA加密的对称密钥(hex) : 对称密钥加密的消息(hex) [: 签名(hex)]
```

### 填充方式

- RSA: PKCS#1 ECB
- RC2 / Blowfish: PKCS#5 CBC

> ⚠️ **本项目状态**：协议常量已定义，有空壳 `GetPubKey` 类，但加密功能**未实现**。

---

## 8. 编码与字符集

### 默认编码

飞秋 Windows 版使用 **GBK** 编码。本项目在收发消息时进行 GBK ↔ UTF-8 转换：

```cpp
// encoding.cpp
Encoding* encIn  = new Encoding("GBK", "UTF-8");   // 接收：GBK -> UTF-8
Encoding* encOut = new Encoding("UTF-8", "GBK");    // 发送：UTF-8 -> GBK
```

### UTF-8 选项

IPMSG 协议定义了 `IPMSG_UTF8OPT` (0x00800000) 标志，当此标志设置时表示消息使用 UTF-8 编码。

> ⚠️ **本项目状态**：目前固定使用 GBK 收发，未检测 `IPMSG_UTF8OPT`。

### 换行符

消息中的换行符标准化为 Unix 类型 `\n` (0x0A)。

---

## 9. 常量速查表

### 命令 ID 速查

| 值 | 命令 | 本项目是否实现 |
|:---|:---|:---|
| `0x01` | BR_ENTRY（上线） | ✅ |
| `0x02` | BR_EXIT（下线） | ✅ |
| `0x03` | ANSENTRY（在线回复） | ✅ |
| `0x04` | BR_ABSENCE（状态变更） | ✅ |
| `0x20` | SENDMSG（发消息） | ✅ |
| `0x21` | RECVMSG（消息确认） | ✅ |
| `0x30` | READMSG（已读通知） | ✅ |
| `0x32` | ANSREADMSG（已读回复） | ❌ |
| `0x40` | GETINFO（请求版本） | ❌ |
| `0x50` | GETABSENCEINFO（查询离开） | ❌ |
| `0x60` | GETFILEDATA（请求文件） | ✅ |
| `0x62` | GETDIRFILES（请求目录） | ✅ |
| `0x72` | GETPUBKEY（请求公钥） | ⚠️ 空壳 |
| `0x77` | OPEN_YOU（打开窗口） | ❌ |
| `0x79` | INPUTING（正在输入） | ❌ |
| `0x7A` | INPUT_END（输入结束） | ❌ |
| `0xC0` | SENDIMAGE（图片） | ✅ 完整接收 + 内联显示 |
| `0xD1` | KNOCK（窗口抖动） | ✅ |

### 端口

```
UDP 2425 — 控制命令 + 消息
TCP 2425 — 文件传输
```

---

## 10. 本项目实现状态

### 已实现功能

| 功能 | 状态 | 备注 |
|:---|:---|:---|
| 用户发现（上线/下线） | ✅ 完整 | 支持广播 + 自定义网段 |
| 文本消息收发 | ✅ 完整 | 支持格式扩展、发送确认、超时重试 |
| 文件传输（单文件） | ✅ 完整 | 支持上传和下载，进度通知 |
| 文件夹传输 | ✅ 完整 | 发送侧递归打包，接收侧递归重建目录 |
| 窗口抖动 | ✅ 完整 | 收发均支持，动画效果 |
| GBK/UTF-8 编码转换 | ✅ 完整 | 使用 iconv |
| MAC 地址识别 | ✅ 完整 | macOS 通过 en0 获取，用户换 IP 历史续接 |
| 消息确认机制 | ✅ 完整 | SENDCHECK + 5 秒超时 |
| 已读回执 | ✅ 完整 | READCHECK |
| 自定义广播组 | ✅ 完整 | 定时发送上线通知 |
| 离开/忙碌状态 | ✅ 完整 | BR_ABSENCE 收发，四色状态圆点 |
| 自动应答 | ✅ 完整 | AUTORETOPT 防死循环 |
| 图片收发（UDP 分片） | ✅ 完整 | 内联显示，图文混排，多图支持 |
| 聊天记录持久化 | ✅ 完整 | SQLite，MAC 为主 key，分页加载 |

### 部分实现

| 功能 | 状态 | 说明 |
|:---|:---|:---|
| 公钥交换 | ⚠️ 空壳类 | `GetPubKey` 已定义但无逻辑 |

### 未实现

| 功能 | 协议支持 | 说明 |
|:---|:---|:---|
| 加密传输 | ✅ IPMSG 标准 | 需要 RSA/AES 实现 |
| 正在输入提示 | ✅ 飞秋扩展 | 协议已定义，UI 未实现 |
| 打开窗口通知 | ✅ 飞秋扩展 | 协议已定义，未实现 |
| 加密消息 | ✅ RSA+AES | 常量已定义，实现复杂度高 |
| 主机列表服务 | ✅ IPMSG 标准 | GETLIST 系列命令未实现 |
| UTF-8 自动检测 | ✅ UTF8OPT | 目前固定用 GBK |

---

## 参考资料

1. **IPMSG 协议官方规范** — Shirouzu Hiroaki, Draft-9 (1996/02/21, 修订 2003/01/14)
2. **飞鸽传书(IPMSG)协议翻译稿** — wanpengcoder 翻译自 Mr.Kanazawa 英文文档
3. **飞鸽飞秋协议详解** — CSDN zanfeng
4. **IPMsg 飞鸽传书网络协议解析手记** — CSDN whatnamecaniuse
5. **本项目源码** — `feiqlib/` 目录下的协议实现代码
