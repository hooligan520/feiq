#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QFont>

SettingsDialog::SettingsDialog(Settings* settings, QWidget *parent)
    : QDialog(parent), mSettings(settings)
{
    setWindowTitle("飞秋设置");
    setMinimumSize(420, 480);
    resize(420, 520);
    setupUI();
    loadSettings();
}

void SettingsDialog::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === 内容区域 (Tab Widget) ===
    auto *tabWidget = new QTabWidget(this);
    tabWidget->setObjectName("settingsTabWidget");

    // --- Tab 1: 用户设置 ---
    auto *userTab = new QWidget();
    auto *userLayout = new QVBoxLayout(userTab);
    userLayout->setContentsMargins(24, 20, 24, 20);
    userLayout->setSpacing(16);

    // 用户名
    auto *userGroup = new QGroupBox("用户信息", userTab);
    auto *userForm = new QFormLayout(userGroup);
    userForm->setContentsMargins(16, 20, 16, 16);
    userForm->setSpacing(12);
    userForm->setLabelAlignment(Qt::AlignRight);

    mUserNameEdit = new QLineEdit(userTab);
    mUserNameEdit->setPlaceholderText("显示名称（必填）");
    mUserNameEdit->setMinimumHeight(30);
    userForm->addRow("用户名:", mUserNameEdit);

    userLayout->addWidget(userGroup);

    // 发送方式
    auto *sendGroup = new QGroupBox("发送方式", userTab);
    auto *sendLayout = new QVBoxLayout(sendGroup);
    sendLayout->setContentsMargins(16, 20, 16, 16);
    sendLayout->setSpacing(8);

    mSendByEnter = new QRadioButton("按 Enter 发送消息，Ctrl+Enter 换行", userTab);
    mSendByCtrlEnter = new QRadioButton("按 Ctrl+Enter 发送消息，Enter 换行", userTab);
    sendLayout->addWidget(mSendByEnter);
    sendLayout->addWidget(mSendByCtrlEnter);

    userLayout->addWidget(sendGroup);
    userLayout->addStretch();

    tabWidget->addTab(userTab, "👤 用户");

    // --- Tab 2: 网络设置 ---
    auto *netTab = new QWidget();
    auto *netLayout = new QVBoxLayout(netTab);
    netLayout->setContentsMargins(24, 20, 24, 20);
    netLayout->setSpacing(16);

    auto *netGroup = new QGroupBox("自定义网段", netTab);
    auto *netGroupLayout = new QVBoxLayout(netGroup);
    netGroupLayout->setContentsMargins(16, 20, 16, 16);
    netGroupLayout->setSpacing(8);

    auto *netHint = new QLabel("多个网段用 | 分隔，以 . 结尾自动扫描整段\n例如: 192.168.1.|10.0.0.", netTab);
    netHint->setObjectName("settingsHintLabel");
    netHint->setWordWrap(true);
    netGroupLayout->addWidget(netHint);

    mCustomGroupEdit = new QTextEdit(netTab);
    mCustomGroupEdit->setPlaceholderText("192.168.1.|10.0.0.");
    mCustomGroupEdit->setMaximumHeight(100);
    netGroupLayout->addWidget(mCustomGroupEdit);

    netLayout->addWidget(netGroup);

    // 通知设置
    auto *notifyGroup = new QGroupBox("通知", netTab);
    auto *notifyLayout = new QVBoxLayout(notifyGroup);
    notifyLayout->setContentsMargins(16, 20, 16, 16);

    mEnableNotify = new QCheckBox("启用系统通知", netTab);
    notifyLayout->addWidget(mEnableNotify);

    netLayout->addWidget(notifyGroup);

    // 自动应答
    auto *autoReplyGroup = new QGroupBox("自动应答", netTab);
    auto *autoReplyLayout = new QVBoxLayout(autoReplyGroup);
    autoReplyLayout->setContentsMargins(16, 20, 16, 16);
    autoReplyLayout->setSpacing(8);

    mEnableAutoReply = new QCheckBox("收到消息时自动回复", netTab);
    autoReplyLayout->addWidget(mEnableAutoReply);

    auto *autoReplyHint = new QLabel("自动回复内容:", netTab);
    autoReplyHint->setObjectName("settingsHintLabel");
    autoReplyLayout->addWidget(autoReplyHint);

    mAutoReplyText = new QTextEdit(netTab);
    mAutoReplyText->setPlaceholderText("例如: 我现在不在，稍后回复您");
    mAutoReplyText->setMaximumHeight(80);
    mAutoReplyText->setEnabled(false);
    autoReplyLayout->addWidget(mAutoReplyText);

    connect(mEnableAutoReply, &QCheckBox::toggled, mAutoReplyText, &QTextEdit::setEnabled);

    netLayout->addWidget(autoReplyGroup);
    netLayout->addStretch();

    tabWidget->addTab(netTab, "🌐 网络");

    // --- Tab 3: 快捷回复 ---
    auto *replyTab = new QWidget();
    auto *replyLayout = new QVBoxLayout(replyTab);
    replyLayout->setContentsMargins(24, 20, 24, 20);
    replyLayout->setSpacing(12);

    auto *replyGroup = new QGroupBox("快捷回复列表", replyTab);
    auto *replyGroupLayout = new QVBoxLayout(replyGroup);
    replyGroupLayout->setContentsMargins(16, 20, 16, 16);
    replyGroupLayout->setSpacing(8);

    auto *replyHint = new QLabel("每行一条快捷回复。输入框为空时点发送按钮可弹出菜单选择。", replyTab);
    replyHint->setObjectName("settingsHintLabel");
    replyHint->setWordWrap(true);
    replyGroupLayout->addWidget(replyHint);

    mQuickRepliesEdit = new QTextEdit(replyTab);
    mQuickRepliesEdit->setPlaceholderText("好的 👍\n收到！\n稍等一下\n马上来\n在忙，等会回你");
    replyGroupLayout->addWidget(mQuickRepliesEdit);

    replyLayout->addWidget(replyGroup);

    tabWidget->addTab(replyTab, "⚡ 快捷回复");
    auto *aboutTab = new QWidget();
    auto *aboutLayout = new QVBoxLayout(aboutTab);
    aboutLayout->setContentsMargins(24, 40, 24, 20);
    aboutLayout->setAlignment(Qt::AlignCenter);

    auto *appIcon = new QLabel("🐦", aboutTab);
    appIcon->setAlignment(Qt::AlignCenter);
    appIcon->setStyleSheet("font-size: 48px;");
    aboutLayout->addWidget(appIcon);

    auto *appName = new QLabel("飞秋 for Mac", aboutTab);
    appName->setAlignment(Qt::AlignCenter);
    appName->setStyleSheet("font-size: 18px; font-weight: bold; color: #333; margin-top: 8px;");
    aboutLayout->addWidget(appName);

    auto *appVer = new QLabel("基于 IPMSG 协议的局域网即时通讯", aboutTab);
    appVer->setAlignment(Qt::AlignCenter);
    appVer->setStyleSheet("font-size: 12px; color: #999; margin-top: 4px;");
    aboutLayout->addWidget(appVer);

    auto *configPath = new QLabel(QString("配置文件: %1").arg(mSettings->fileName()), aboutTab);
    configPath->setAlignment(Qt::AlignCenter);
    configPath->setStyleSheet("font-size: 11px; color: #BBB; margin-top: 16px;");
    configPath->setWordWrap(true);
    configPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    aboutLayout->addWidget(configPath);

    aboutLayout->addStretch();

    tabWidget->addTab(aboutTab, "ℹ️ 关于");

    mainLayout->addWidget(tabWidget, 1);

    // === 底部按钮 ===
    auto *btnWidget = new QWidget(this);
    btnWidget->setObjectName("settingsBtnBar");
    btnWidget->setFixedHeight(52);
    auto *btnLayout = new QHBoxLayout(btnWidget);
    btnLayout->setContentsMargins(20, 8, 20, 8);

    auto *restartHint = new QLabel("部分设置需重启后生效", this);
    restartHint->setObjectName("settingsHintLabel");
    btnLayout->addWidget(restartHint);
    btnLayout->addStretch();

    mCancelBtn = new QPushButton("取消", this);
    mCancelBtn->setFixedSize(72, 30);
    connect(mCancelBtn, &QPushButton::clicked, this, &SettingsDialog::onCancel);
    btnLayout->addWidget(mCancelBtn);

    mSaveBtn = new QPushButton("保存", this);
    mSaveBtn->setObjectName("settingsSaveBtn");
    mSaveBtn->setFixedSize(72, 30);
    connect(mSaveBtn, &QPushButton::clicked, this, &SettingsDialog::onSave);
    btnLayout->addWidget(mSaveBtn);

    mainLayout->addWidget(btnWidget);

    setLayout(mainLayout);
}

void SettingsDialog::loadSettings()
{
    mUserNameEdit->setText(mSettings->value("user/name", "").toString());
    mCustomGroupEdit->setPlainText(mSettings->value("network/custom_group", "").toString());

    if (mSettings->value("app/send_by_enter", true).toBool())
        mSendByEnter->setChecked(true);
    else
        mSendByCtrlEnter->setChecked(true);

    mEnableNotify->setChecked(mSettings->value("app/enable_notify", true).toBool());

    mEnableAutoReply->setChecked(mSettings->value("app/auto_reply_enable", false).toBool());
    mAutoReplyText->setPlainText(mSettings->value("app/auto_reply_text", "").toString());
    mAutoReplyText->setEnabled(mEnableAutoReply->isChecked());

    // 快捷回复（默认内置常用回复）
    static const QString defaultReplies =
        "好的 👍\n收到！\n稍等一下\n马上来\n在忙，等会回你\n已处理，请查收";
    mQuickRepliesEdit->setPlainText(mSettings->value("app/quick_replies", defaultReplies).toString());
}

void SettingsDialog::saveSettings()
{
    auto userName = mUserNameEdit->text().trimmed();
    if (userName.isEmpty())
    {
        QMessageBox::warning(this, "提示", "用户名不能为空");
        mUserNameEdit->setFocus();
        return;
    }

    mSettings->setValue("user/name", userName);
    mSettings->setValue("network/custom_group", mCustomGroupEdit->toPlainText().trimmed());
    mSettings->setValue("app/send_by_enter", mSendByEnter->isChecked());
    mSettings->setValue("app/enable_notify", mEnableNotify->isChecked());
    mSettings->setValue("app/auto_reply_enable", mEnableAutoReply->isChecked());
    mSettings->setValue("app/auto_reply_text", mAutoReplyText->toPlainText().trimmed());
    mSettings->setValue("app/quick_replies", mQuickRepliesEdit->toPlainText());
    mSettings->sync();
}

void SettingsDialog::onSave()
{
    saveSettings();
    QMessageBox::information(this, "已保存", "设置已保存，部分设置需重启后生效");
    accept();
}

void SettingsDialog::onCancel()
{
    reject();
}
