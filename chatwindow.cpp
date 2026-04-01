#include "chatwindow.h"
#include "mainwindow.h"
#include "emoji.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QShortcut>
#include "feiqlib/history.h"

ChatWindow::ChatWindow(const Fellow* fellow, FeiqEngine* engine, Settings* settings, MainWindow* mainWin, QWidget *parent)
    : QWidget(parent),
      mFellow(fellow),
      mEngine(engine),
      mSettings(settings),
      mMainWin(mainWin)
{
    setupUI();

    // 初始化 RecvTextEdit - 设置当前好友
    mRecvTextEdit->setCurFellow(mFellow);

    // 初始化 Emoji 对话框
    mChooseEmojiDlg = new ChooseEmojiDlg(this);
    connect(mChooseEmojiDlg, SIGNAL(choose(QString)), mSendTextEdit, SLOT(insertPlainText(QString)));

    // 连接发送文本框信号
    connect(mSendTextEdit, SIGNAL(acceptDropFiles(QList<QFileInfo>)), this, SLOT(sendFiles(QList<QFileInfo>)));
    if (mSettings->value("app/send_by_enter", true).toBool())
    {
        connect(mSendTextEdit, SIGNAL(enterPressed()), this, SLOT(sendText()));
        connect(mSendTextEdit, SIGNAL(ctrlEnterPressed()), mSendTextEdit, SLOT(newLine()));
    }
    else
    {
        connect(mSendTextEdit, SIGNAL(ctrlEnterPressed()), this, SLOT(sendText()));
        connect(mSendTextEdit, SIGNAL(enterPressed()), mSendTextEdit, SLOT(newLine()));
    }

    // 连接消息区文件链接点击
    connect(mRecvTextEdit, SIGNAL(navigateToFileTask(IdType,IdType,bool)), this, SLOT(navigateToFileTask(IdType,IdType,bool)));

    // 设置窗口标题
    setWindowTitle(QString("与 %1 的会话").arg(fellow->getName().c_str()));
    resize(520, 480);

    // 加载历史消息
    loadHistory();
}

ChatWindow::~ChatWindow()
{
    delete mChooseEmojiDlg;
}

void ChatWindow::setupUI()
{
    // === 整体垂直布局 ===
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === 顶部标题栏 ===
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName("chatTitleBar");
    titleBar->setFixedHeight(48);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(16, 0, 16, 0);

    // 在线状态圆点
    auto *statusDot = new QLabel(this);
    statusDot->setFixedSize(10, 10);
    if (mFellow->isOnLine()) {
        statusDot->setStyleSheet("background-color: #4CAF50; border-radius: 5px;");
    } else {
        statusDot->setStyleSheet("background-color: #9E9E9E; border-radius: 5px;");
    }

    mTitleLabel = new QLabel(QString("%1  (%2)").arg(mFellow->getName().c_str()).arg(mFellow->getIp().c_str()), this);
    mTitleLabel->setObjectName("chatTitleLabel");

    titleLayout->addWidget(statusDot);
    titleLayout->addSpacing(8);
    titleLayout->addWidget(mTitleLabel);
    titleLayout->addStretch();

    mainLayout->addWidget(titleBar);

    // === 工具栏 ===
    mToolBar = new QToolBar(this);
    mToolBar->setObjectName("chatToolBar");
    mToolBar->setIconSize(QSize(20, 20));
    mToolBar->setMovable(false);

    auto *actEmoji = mToolBar->addAction("😊 表情");
    connect(actEmoji, &QAction::triggered, this, &ChatWindow::openChooseEmojiDlg);

    auto *actFile = mToolBar->addAction("📎 文件");
    connect(actFile, &QAction::triggered, [this](){ sendFile(); });

    auto *actKnock = mToolBar->addAction("👋 抖动");
    connect(actKnock, &QAction::triggered, this, &ChatWindow::sendKnock);

    mainLayout->addWidget(mToolBar);

    // === 聊天区域 (Splitter: 消息显示 + 输入框) ===
    auto *chatSplitter = new QSplitter(Qt::Vertical, this);
    chatSplitter->setObjectName("chatSplitter");

    mRecvTextEdit = new RecvTextEdit(this);
    mRecvTextEdit->setObjectName("recvEdit");
    mRecvTextEdit->setReadOnly(true);
    mRecvTextEdit->setFocusPolicy(Qt::ClickFocus);

    // 输入区容器（输入框 + 发送按钮）
    auto *inputContainer = new QWidget(this);
    inputContainer->setObjectName("inputContainer");
    auto *inputLayout = new QVBoxLayout(inputContainer);
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(4);

    mSendTextEdit = new SendTextEdit(this);
    mSendTextEdit->setObjectName("sendEdit");
    mSendTextEdit->setAcceptRichText(false);
    mSendTextEdit->setFocusPolicy(Qt::StrongFocus);

    // 发送按钮行
    auto *btnRow = new QWidget(this);
    auto *btnLayout = new QHBoxLayout(btnRow);
    btnLayout->setContentsMargins(8, 2, 8, 6);
    btnLayout->addStretch();

    auto *sendHintLabel = new QLabel(this);
    if (mSettings->value("app/send_by_enter", true).toBool())
        sendHintLabel->setText("Enter 发送, Ctrl+Enter 换行");
    else
        sendHintLabel->setText("Ctrl+Enter 发送, Enter 换行");
    sendHintLabel->setObjectName("sendHintLabel");

    mSendBtn = new QPushButton("发送", this);
    mSendBtn->setObjectName("sendBtn");
    mSendBtn->setFixedSize(80, 30);
    connect(mSendBtn, &QPushButton::clicked, this, &ChatWindow::sendText);

    btnLayout->addWidget(sendHintLabel);
    btnLayout->addSpacing(8);
    btnLayout->addWidget(mSendBtn);

    inputLayout->addWidget(mSendTextEdit);
    inputLayout->addWidget(btnRow);

    chatSplitter->addWidget(mRecvTextEdit);
    chatSplitter->addWidget(inputContainer);
    chatSplitter->setStretchFactor(0, 3);
    chatSplitter->setStretchFactor(1, 1);

    mainLayout->addWidget(chatSplitter, 1);

    setLayout(mainLayout);
}

void ChatWindow::handleViewEvent(shared_ptr<ViewEvent> event)
{
    readEvent(event.get());
}

void ChatWindow::bringToFront()
{
    show();
    raise();
    activateWindow();
    mSendTextEdit->setFocus();
}

void ChatWindow::closeEvent(QCloseEvent *event)
{
    emit windowClosed(mFellow);
    event->accept();
}

void ChatWindow::showEvent(QShowEvent *event)
{
    Q_UNUSED(event);
    mSendTextEdit->setFocus();
}

void ChatWindow::sendText()
{
    auto text = mSendTextEdit->toPlainText();
    if (text.isEmpty())
    {
        mRecvTextEdit->addWarning("发送空文本是不科学的，驳回");
        return;
    }

    auto fellow = checkCurFellow();
    if (fellow)
    {
        auto content = make_shared<TextContent>();
        content->text = text.toStdString();
        auto ret = mEngine->send(fellow, content);
        showResult(ret, content.get());
        mSendTextEdit->clear();
    }
}

void ChatWindow::sendFile()
{
    auto fellow = checkCurFellow();
    if (!fellow)
        return;

    QFileDialog fdlg(this, "选择要发送的文件");
    fdlg.setFileMode(QFileDialog::ExistingFiles);
    if (fdlg.exec() == QDialog::Accepted)
    {
        auto list = fdlg.selectedFiles();
        auto count = list.count();
        for (int i = 0; i < count; i++)
        {
            auto path = list.at(i);
            sendFile(path.toStdString());
        }
    }
}

void ChatWindow::sendFile(string filepath)
{
    auto content = FileContent::createFileContentToSend(filepath);
    auto fellow = checkCurFellow();
    if (!fellow)
        return;

    if (content == nullptr)
    {
        mRecvTextEdit->addWarning("获取文件"+QString(filepath.c_str())+"的信息失败，不发送");
    }
    else
    {
        auto fileContent = shared_ptr<FileContent>(std::move(content));
        auto ret = mEngine->send(fellow, fileContent);
        showResult(ret, fileContent.get());
    }
}

void ChatWindow::sendFiles(QList<QFileInfo> files)
{
    auto fellow = checkCurFellow();
    if (!fellow)
        return;

    for (auto file : files)
    {
        if (file.isFile())
        {
            sendFile(file.absoluteFilePath().toStdString());
        }
        else
        {
            mRecvTextEdit->addWarning("不支持发送："+file.absoluteFilePath());
        }
    }
}

void ChatWindow::sendKnock()
{
    auto fellow = checkCurFellow();
    if (fellow)
    {
        auto content = make_shared<KnockContent>();
        auto ret = mEngine->send(fellow, content);
        showResult(ret, content.get());
    }
}

void ChatWindow::openChooseEmojiDlg()
{
    mChooseEmojiDlg->exec();
}

void ChatWindow::navigateToFileTask(IdType packetNo, IdType fileId, bool upload)
{
    // 通知主窗口打开文件管理对话框
    auto task = mEngine->getModel().findTask(packetNo, fileId, upload ? FileTaskType::Upload : FileTaskType::Download);
    // 由 MainWindow 处理文件管理
    Q_UNUSED(task);
}

void ChatWindow::showResult(pair<bool, string> ret, const Content *content)
{
    if (ret.first)
        mRecvTextEdit->addMyContent(content, QDateTime::currentDateTime().currentMSecsSinceEpoch());
    else
        mRecvTextEdit->addWarning(ret.second.c_str());
}

shared_ptr<Fellow> ChatWindow::checkCurFellow()
{
    auto fellow = mEngine->getModel().getShared(mFellow);
    if (fellow == nullptr)
    {
        mRecvTextEdit->addWarning("好友已离线或不存在");
    }
    return fellow;
}

void ChatWindow::readEvent(const ViewEvent *event)
{
    if (event->what == ViewEventType::SEND_TIMEO)
    {
        auto e = static_cast<const SendTimeoEvent*>(event);
        auto simpleText = simpleTextOf(e->content.get());
        if (simpleText.length()>20){
            simpleText = simpleText.left(20)+"...";
        }
        mRecvTextEdit->addWarning("发送超时:"+simpleText);
    }
    else if (event->what == ViewEventType::MESSAGE)
    {
        auto e = static_cast<const MessageViewEvent*>(event);
        auto time = e->when.time_since_epoch().count();
        for (auto content : e->contents)
        {
            if (e->fellow == nullptr)
                mRecvTextEdit->addMyContent(content.get(), time);
            else
                mRecvTextEdit->addFellowContent(content.get(), time);
        }
    }
}

QString ChatWindow::simpleTextOf(const Content *content)
{
    switch (content->type()) {
    case ContentType::Text:
        return static_cast<const TextContent*>(content)->text.c_str();
    case ContentType::File:
        return static_cast<const FileContent*>(content)->filename.c_str();
    case ContentType::Knock:
        return "窗口抖动";
    default:
        return "***";
    }
}

void ChatWindow::loadHistory()
{
    auto& history = mEngine->getHistory();
    auto records = history.queryByIp(mFellow->getIp(), 30);

    if (records.empty()) return;

    // 显示"历史消息"分隔线
    mRecvTextEdit->addWarning("—— 以上是历史消息 ——");

    for (auto& record : records)
    {
        if (record.contentType == static_cast<int>(ContentType::Text))
        {
            auto content = make_shared<TextContent>();
            content->text = record.contentText;
            if (record.isSelf)
                mRecvTextEdit->addMyContent(content.get(), record.timestamp);
            else
                mRecvTextEdit->addFellowContent(content.get(), record.timestamp);
        }
        else if (record.contentType == static_cast<int>(ContentType::Knock))
        {
            auto content = make_shared<KnockContent>();
            if (record.isSelf)
                mRecvTextEdit->addMyContent(content.get(), record.timestamp);
            else
                mRecvTextEdit->addFellowContent(content.get(), record.timestamp);
        }
        else if (record.contentType == static_cast<int>(ContentType::File))
        {
            // 文件记录显示为文本提示
            QString fileText = record.isSelf ?
                QString("📤 发送了文件: %1").arg(QString::fromStdString(record.contentText)) :
                QString("📥 收到文件: %1").arg(QString::fromStdString(record.contentText));
            mRecvTextEdit->addWarning(fileText);
        }
    }

    // 分隔线
    mRecvTextEdit->addWarning("—— 以下是新消息 ——");
}
