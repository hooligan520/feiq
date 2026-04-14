#include "chatwindow.h"
#include "mainwindow.h"
#include "emoji.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QShortcut>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QProcess>
#include <QDir>
#include <QStandardPaths>
#include <QImage>
#include <QScrollBar>
#include <QUrl>
#include <QFileInfo>
#include <QRegExp>
#include <QDebug>
#include <thread>
#include <chrono>
#include <iostream>
#include "feiqlib/history.h"
#include "feiqlib/tcpsocket.h"
#include "feiqlib/ipmsg.h"

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

    // 设置"我"的信息用于头像生成
    QString myName = settings->value("user/name", "我").toString();
    QString myIp = mainWin->getLocalIp();
    mRecvTextEdit->setMyInfo(myName, myIp);

    // 初始化 Emoji 对话框
    mChooseEmojiDlg = new ChooseEmojiDlg(this);
    connect(mChooseEmojiDlg, SIGNAL(choose(QString)), mSendTextEdit, SLOT(insertPlainText(QString)));

    // 连接发送文本框信号
    connect(mSendTextEdit, SIGNAL(acceptDropFiles(QList<QFileInfo>)), this, SLOT(sendFiles(QList<QFileInfo>)));
    connect(mSendTextEdit, &SendTextEdit::pasteImage, this, [this](QPixmap pix) {
        // 把剪贴板图片保存为临时 PNG，走截图预览流程
        QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString filePath = tempDir + "/feiq_paste_" + timestamp + ".png";
        if (pix.save(filePath, "PNG"))
        {
            setScreenshotPreview(filePath);
        }
    });
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

    // 连接滚动到顶部信号 - 加载更多历史
    connect(mRecvTextEdit, &RecvTextEdit::scrolledToTop, this, &ChatWindow::loadMoreHistory);

    // Cmd+W 关闭窗口
    auto *closeShortcut = new QShortcut(QKeySequence("Ctrl+W"), this);
    connect(closeShortcut, &QShortcut::activated, this, &ChatWindow::close);

    // 设置窗口标题
    setWindowTitle(QString("与 %1 的会话").arg(fellow->getName().c_str()));
    resize(700, 600);

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

    // === 聊天区域 (Splitter: 消息显示 + 输入框) ===
    auto *chatSplitter = new QSplitter(Qt::Vertical, this);
    chatSplitter->setObjectName("chatSplitter");

    mRecvTextEdit = new RecvTextEdit(this);
    mRecvTextEdit->setObjectName("recvEdit");
    mRecvTextEdit->setReadOnly(true);
    mRecvTextEdit->setFocusPolicy(Qt::ClickFocus);

    // === 工具栏（表情/文件/图片等）放在聊天记录和输入框之间 ===
    mToolBar = new QToolBar(this);
    mToolBar->setObjectName("chatToolBar");
    mToolBar->setIconSize(QSize(20, 20));
    mToolBar->setMovable(false);

    auto *actEmoji = mToolBar->addAction("😊 表情");
    connect(actEmoji, &QAction::triggered, this, &ChatWindow::openChooseEmojiDlg);

    auto *actFile = mToolBar->addAction("📎 文件");
    connect(actFile, &QAction::triggered, [this](){ sendFile(); });

    auto *actImage = mToolBar->addAction("🖼 图片");
    connect(actImage, &QAction::triggered, this, &ChatWindow::sendImage);

    auto *actKnock = mToolBar->addAction("👋 抖动");
    connect(actKnock, &QAction::triggered, this, &ChatWindow::sendKnock);

    auto *actCapture = mToolBar->addAction("📷 截图");
    connect(actCapture, &QAction::triggered, this, &ChatWindow::captureScreen);

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

    // 工具栏容器（包裹在 widget 中以便加入 splitter）
    auto *toolBarContainer = new QWidget(this);
    toolBarContainer->setFixedHeight(32);
    auto *toolBarLayout = new QVBoxLayout(toolBarContainer);
    toolBarLayout->setContentsMargins(0, 0, 0, 0);
    toolBarLayout->setSpacing(0);
    toolBarLayout->addWidget(mToolBar);

    chatSplitter->addWidget(toolBarContainer);
    chatSplitter->addWidget(inputContainer);
    chatSplitter->setStretchFactor(0, 3);
    chatSplitter->setStretchFactor(1, 0); // 工具栏不伸缩
    chatSplitter->setStretchFactor(2, 1);

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
    // 如果有待发截图，先发截图（文字内容一起发）
    if (!mPendingScreenshotPath.isEmpty())
    {
        QString path = mPendingScreenshotPath;
        clearScreenshotPreview();  // 先清预览

        auto fellow = checkCurFellow();
        if (!fellow) return;
        if (!fellow->isSelf() && !fellow->isOnLine())
        {
            mRecvTextEdit->addWarning("对方不在线，截图无法发送");
            return;
        }
        // 先发文字（如果有）
        auto text = mSendTextEdit->toPlainText().trimmed();
        if (!text.isEmpty())
        {
            auto textContent = make_shared<TextContent>();
            textContent->text = text.toStdString();
            if (fellow->isSelf())
            {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                displayContentWithRetry(textContent, now, false, 0);
                mEngine->getHistory().addRecord(
                    fellow->getIp(), fellow->getName(), fellow->getMac(),
                    now, true, static_cast<int>(ContentType::Text), textContent->text);
            }
            else
            {
                auto ret = mEngine->send(fellow, textContent);
                showResult(ret, textContent.get());
            }
            mSendTextEdit->clear();
        }
        // 再发截图
        if (fellow->isSelf())
        {
            // 本机自发自收：截图直接内联显示（不走文件传输）
            auto imgContent = make_shared<ImageContent>();
            imgContent->localPath = path.toStdString();
            // imageId 用时间戳保证唯一
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            imgContent->imageId = std::to_string(now);

            // 把图片复制到 ~/.feiq/images/ 目录，保持与正常图片相同的存储路径
            QString feiqImgDir = QDir::homePath() + "/.feiq/images";
            QDir().mkpath(feiqImgDir);
            QString imgFileName = QString("%1_%2.png")
                .arg(now).arg(QString::fromStdString(imgContent->imageId));
            QString destPath = feiqImgDir + "/" + imgFileName;
            QFile::copy(path, destPath);
            imgContent->localPath = destPath.toStdString();

            displayContentWithRetry(imgContent, now, false, 0);
            mEngine->getHistory().addRecord(
                fellow->getIp(), fellow->getName(), fellow->getMac(),
                now, true, static_cast<int>(ContentType::Image),
                imgFileName.toStdString());
        }
        else
        {
            sendFile(path.toStdString());
        }
        return;
    }

    auto text = mSendTextEdit->toPlainText();
    if (text.isEmpty())
    {
        showQuickReplyMenu();
        return;
    }

    auto fellow = checkCurFellow();
    if (!fellow) return;

    // 离线好友直接提示，不发送
    if (!fellow->isSelf() && !fellow->isOnLine())
    {
        mRecvTextEdit->addWarning("对方不在线，消息无法发送");
        return;
    }

    auto content = make_shared<TextContent>();
    content->text = text.toStdString();

    if (fellow->isSelf())
    {
        // 本机自发自收：不走网络，直接回显为"自己发送"的气泡并记入历史
        mSendTextEdit->clear();
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        // 显示到聊天窗口（isFellow=false 代表自己发的）
        displayContentWithRetry(content, now, false, 0);
        // 写历史
        mEngine->getHistory().addRecord(
            fellow->getIp(), fellow->getName(), fellow->getMac(),
            now, true, static_cast<int>(ContentType::Text), content->text);
    }
    else
    {
        auto ret = mEngine->send(fellow, content);
        showResult(ret, content.get());
        mSendTextEdit->clear();
    }
}

void ChatWindow::showQuickReplyMenu()
{
    // 默认快捷回复
    static const QStringList defaultReplies = {
        "好的 👍",
        "收到！",
        "稍等一下",
        "马上来",
        "在忙，等会回你",
        "已处理，请查收"
    };

    QString saved = mSettings->value("app/quick_replies", "").toString();
    QStringList replies = saved.isEmpty() ? defaultReplies
                                          : saved.split('\n', Qt::SkipEmptyParts);

    if (replies.isEmpty())
    {
        mRecvTextEdit->addWarning("没有快捷回复，请在设置中添加");
        return;
    }

    QMenu menu(this);
    menu.setTitle("快捷回复");
    for (const QString& reply : replies)
    {
        if (reply.trimmed().isEmpty()) continue;
        QAction* act = menu.addAction(reply.trimmed());
        connect(act, &QAction::triggered, this, [this, reply]() {
            mSendTextEdit->setPlainText(reply.trimmed());
            // 光标移到末尾
            QTextCursor cursor = mSendTextEdit->textCursor();
            cursor.movePosition(QTextCursor::End);
            mSendTextEdit->setTextCursor(cursor);
            mSendTextEdit->setFocus();
        });
    }

    // 菜单弹出位置：发送按钮上方
    QPoint pos = mSendBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height() - 4));
    menu.exec(pos);
}

void ChatWindow::sendFile()
{
    auto fellow = checkCurFellow();
    if (!fellow) return;
    if (!fellow->isSelf() && !fellow->isOnLine())
    {
        mRecvTextEdit->addWarning("对方不在线，文件无法发送");
        return;
    }

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
    if (!fellow) return;
    if (!fellow->isSelf() && !fellow->isOnLine())
    {
        mRecvTextEdit->addWarning("对方不在线，文件无法发送");
        return;
    }

    for (auto file : files)
    {
        if (file.isFile() || file.isDir())
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
    if (!fellow) return;
    if (!fellow->isSelf() && !fellow->isOnLine())
    {
        mRecvTextEdit->addWarning("对方不在线，无法发送抖动");
        return;
    }
    auto content = make_shared<KnockContent>();
    auto ret = mEngine->send(fellow, content);
    showResult(ret, content.get());
}

void ChatWindow::sendImage()
{
    auto fellow = checkCurFellow();
    if (!fellow) return;
    if (!fellow->isSelf() && !fellow->isOnLine())
    {
        mRecvTextEdit->addWarning("对方不在线，图片无法发送");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(
        this, "选择要发送的图片",
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        "图片文件 (*.png *.jpg *.jpeg *.gif *.bmp *.tiff);;所有文件 (*)"
    );

    if (!filePath.isEmpty())
    {
        sendFile(filePath.toStdString());
    }
}

void ChatWindow::captureScreen()
{
    // 生成临时文件路径
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString filePath = tempDir + "/feiq_screenshot_" + timestamp + ".png";

    // 调用 macOS screencapture 命令（-i 交互式框选，-s 只截选区）
    QProcess process;
    process.start("screencapture", QStringList() << "-i" << "-s" << filePath);
    process.waitForFinished(-1); // 等待用户完成截图

    // 检查文件是否生成（用户可能按 Esc 取消）
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || fileInfo.size() == 0)
    {
        return;
    }

    // 截图成功：显示预览，等用户确认后发送
    setScreenshotPreview(filePath);
    bringToFront();
    mSendTextEdit->setFocus();
}

void ChatWindow::setScreenshotPreview(const QString& filePath)
{
    // 先清除旧预览
    clearScreenshotPreview();

    mPendingScreenshotPath = filePath;

    // 创建预览条
    mScreenshotPreviewBar = new QWidget(this);
    mScreenshotPreviewBar->setObjectName("screenshotPreviewBar");
    mScreenshotPreviewBar->setStyleSheet(
        "#screenshotPreviewBar { background:#F0F4FF; border-top:1px solid #D0D8F0; padding:4px; }"
    );

    auto *barLayout = new QHBoxLayout(mScreenshotPreviewBar);
    barLayout->setContentsMargins(8, 6, 8, 6);
    barLayout->setSpacing(8);

    // 缩略图（只显示图，不显示文件名和提示）
    QPixmap pix(filePath);
    auto *thumbLabel = new QLabel(mScreenshotPreviewBar);
    if (!pix.isNull())
    {
        // 最大宽度240，高度按比例，最高160
        thumbLabel->setPixmap(pix.scaled(240, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    thumbLabel->setAlignment(Qt::AlignCenter);
    barLayout->addWidget(thumbLabel);
    barLayout->addStretch(1);

    // 关闭按钮（右上角）
    auto *closeBtn = new QPushButton("✕", mScreenshotPreviewBar);
    closeBtn->setFixedSize(22, 22);
    closeBtn->setStyleSheet("QPushButton { border:none; color:#999; font-size:13px; } QPushButton:hover { color:#333; }");
    connect(closeBtn, &QPushButton::clicked, this, &ChatWindow::clearScreenshotPreview);
    barLayout->addWidget(closeBtn, 0, Qt::AlignTop);

    // 把预览条插入到输入区上方：找到 inputContainer 在 splitter 里的位置，在其前面插入
    // 用一个外层 wrapper widget 包住 previewBar + inputContainer
    auto *splitter = qobject_cast<QSplitter*>(mSendTextEdit->parentWidget()->parentWidget());
    if (splitter)
    {
        // inputContainer 是 splitter 中 index=2 的 widget
        auto *inputContainer = splitter->widget(2);
        auto *wrapper = new QWidget(this);
        wrapper->setObjectName("inputWrapper");
        auto *wrapLayout = new QVBoxLayout(wrapper);
        wrapLayout->setContentsMargins(0, 0, 0, 0);
        wrapLayout->setSpacing(0);
        wrapLayout->addWidget(mScreenshotPreviewBar);
        // 把 inputContainer 从 splitter 移出，放入 wrapper
        inputContainer->setParent(wrapper);
        wrapLayout->addWidget(inputContainer);
        splitter->insertWidget(2, wrapper);
    }
}

void ChatWindow::clearScreenshotPreview()
{
    mPendingScreenshotPath.clear();
    if (mScreenshotPreviewBar)
    {
        // 把 inputContainer 从 wrapper 还原到 splitter
        auto *wrapper = mScreenshotPreviewBar->parentWidget();
        if (wrapper)
        {
            auto *splitter = qobject_cast<QSplitter*>(wrapper->parentWidget());
            if (splitter)
            {
                // inputContainer 是 wrapper layout 里 index=1 的 widget
                auto *wrapLayout = qobject_cast<QVBoxLayout*>(wrapper->layout());
                if (wrapLayout && wrapLayout->count() >= 2)
                {
                    auto *inputContainer = wrapLayout->itemAt(1)->widget();
                    if (inputContainer)
                    {
                        int idx = splitter->indexOf(wrapper);
                        inputContainer->setParent(nullptr);
                        splitter->insertWidget(idx, inputContainer);
                        splitter->setStretchFactor(idx, 1);
                    }
                }
            }
            wrapper->deleteLater();
        }
        mScreenshotPreviewBar = nullptr;
    }
}

void ChatWindow::openChooseEmojiDlg()
{
    mChooseEmojiDlg->exec();
}

void ChatWindow::navigateToFileTask(IdType packetNo, IdType fileId, bool upload)
{
    auto task = mEngine->getModel().findTask(packetNo, fileId, upload ? FileTaskType::Upload : FileTaskType::Download);
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
            // 如果是 ImageContent，检查是否已被文本消息引用
            if (content->type() == ContentType::Image)
            {
                auto ic = static_pointer_cast<ImageContent>(content);
                QString qImageId = QString::fromStdString(ic->imageId);

                if (mInlineImageIds.contains(qImageId))
                {
                    // 这个图片已经在文本气泡中引用了，跳过独立气泡显示
                    // （对应的 TextContent 会通过延迟重试机制显示图片）
                    continue;
                }
            }

            // 如果是 TextContent，提取其中的内联图片标记并注册
            if (content->type() == ContentType::Text)
            {
                auto tc = static_cast<TextContent*>(content.get());
                QString text = QString::fromStdString(tc->text);
                QRegExp rx("\\x01IMG:([0-9a-fA-F]+)\\x01");
                int offset = 0;
                bool hasInlineImg = false;
                while ((offset = rx.indexIn(text, offset)) != -1)
                {
                    mInlineImageIds.insert(rx.cap(1));
                    hasInlineImg = true;
                    offset += rx.matchedLength();
                }

                // 如果有内联图片标记，检查图片文件是否都已存在
                if (hasInlineImg)
                {
                    bool isFellow = (e->fellow != nullptr);
                    displayContentWithRetry(content, time, isFellow, 0);
                    continue; // 不走下面的直接显示
                }
            }

            if (e->fellow == nullptr)
                mRecvTextEdit->addMyContent(content.get(), time);
            else
            {
                mRecvTextEdit->addFellowContent(content.get(), time);
                // 收到对方的窗口抖动消息，触发抖动动画
                if (content->type() == ContentType::Knock)
                {
                    bringToFront();
                    shakeWindow();
                }
            }
        }
    }
}

void ChatWindow::displayContentWithRetry(shared_ptr<Content> content, long long time,
                                          bool isFellow, int retryCount)
{
    // 检查所有内联图片文件是否已就绪
    // 新文件名格式: <timestampMs>_<imageId>.jpg，用通配符 *_imageId.jpg 查找
    auto tc = static_cast<TextContent*>(content.get());
    QString text = QString::fromStdString(tc->text);
    QString imgDir = QDir::homePath() + "/.feiq/images";

    QRegExp rx("\\x01IMG:([0-9a-fA-F]+)\\x01");
    int offset = 0;
    bool allReady = true;
    while ((offset = rx.indexIn(text, offset)) != -1)
    {
        QString imageId = rx.cap(1);
        // 在 imgDir 下查找 *_imageId.jpg（取最新的一个）
        QDir dir(imgDir);
        QStringList filters;
        filters << ("*_" + imageId + ".jpg");
        auto matches = dir.entryList(filters, QDir::Files, QDir::Time);
        bool exists = !matches.isEmpty();
        qDebug() << "[displayContentWithRetry] retry=" << retryCount
                 << "imageId=" << imageId << "exists=" << exists;
        if (!exists)
        {
            allReady = false;
            break;
        }
        offset += rx.matchedLength();
    }

    if (allReady || retryCount >= 20) // 20次 × 500ms = 10秒超时
    {
        // 图片全部就绪（或重试次数耗尽），立即显示
        if (isFellow)
            mRecvTextEdit->addFellowContent(content.get(), time);
        else
            mRecvTextEdit->addMyContent(content.get(), time);
        return;
    }

    // 图片未就绪，延迟 500ms 重试
    QTimer::singleShot(500, this, [this, content, time, isFellow, retryCount]() {
        displayContentWithRetry(content, time, isFellow, retryCount + 1);
    });
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

void ChatWindow::downloadAndShowImage(shared_ptr<ImageContent> ic, shared_ptr<Fellow> fellow)
{
    // 在后台线程中通过 TCP 下载图片（飞秋图片协议）
    string ip = fellow->getIp();
    string imageId = ic->imageId;
    IdType packetNo = ic->packetNo;

    // 保存到应用私有目录（持久化，重启不丢失）
    QString imgDir = QDir::homePath() + "/.feiq/images";
    QDir().mkpath(imgDir);
    QString savePath = imgDir + "/" + QString::number(packetNo) + "_" + QString::fromStdString(imageId) + ".jpg";

    // 如果已经下载过，直接显示
    if (QFileInfo(savePath).exists() && QFileInfo(savePath).size() > 0)
    {
        showInlineImage(savePath);
        return;
    }

    RecvTextEdit* textEdit = mRecvTextEdit;
    FeiqEngine* engine = mEngine;

    // 后台线程下载图片
    std::thread([ip, imageId, packetNo, savePath, textEdit, engine](){
        cout << "[ImageDL] 开始下载图片: ip=" << ip
             << " packetNo=" << packetNo
             << " imageId=" << imageId << endl;

        // 使用 packImageRequest 构造请求包
        string request = engine->getCommu().packImageRequest(packetNo, imageId);
        if (request.empty())
        {
            cout << "[ImageDL] packImageRequest 失败" << endl;
            return;
        }

        // 手动创建 TCP 连接
        TcpSocket client;
        if (!client.connect(ip, IPMSG_PORT))
        {
            cout << "[ImageDL] TCP 连接失败: " << ip << ":" << IPMSG_PORT << endl;
            QMetaObject::invokeMethod(textEdit, [textEdit](){
                textEdit->addWarning("⚠ 图片下载失败：无法连接到对方");
            }, Qt::QueuedConnection);
            return;
        }

        cout << "[ImageDL] TCP 连接成功，发送请求 (" << request.size() << " bytes)" << endl;

        // 发送请求
        int ret = client.send(request.data(), request.size());
        if (ret < 0)
        {
            cout << "[ImageDL] 发送请求失败" << endl;
            QMetaObject::invokeMethod(textEdit, [textEdit](){
                textEdit->addWarning("⚠ 图片下载失败：发送请求失败");
            }, Qt::QueuedConnection);
            return;
        }

        // 读取图片二进制流
        std::vector<char> imgData;
        const int bufSize = 8192;
        char buf[8192];
        int timeoutCount = 0;
        const int maxTimeout = 3;

        while (timeoutCount < maxTimeout)
        {
            int got = client.recv(buf, bufSize, 2000);
            if (got > 0)
            {
                imgData.insert(imgData.end(), buf, buf + got);
                timeoutCount = 0;
            }
            else if (got == 0)
            {
                cout << "[ImageDL] 连接关闭，传输完成 (" << imgData.size() << " bytes)" << endl;
                break;
            }
            else
            {
                timeoutCount++;
                if (!imgData.empty())
                {
                    cout << "[ImageDL] 超时但已有数据 (" << imgData.size() << " bytes)，结束" << endl;
                    break;
                }
                cout << "[ImageDL] 超时 (" << timeoutCount << "/" << maxTimeout << ")，无数据" << endl;
            }
        }

        if (imgData.empty())
        {
            cout << "[ImageDL] 未收到任何数据" << endl;
            QMetaObject::invokeMethod(textEdit, [textEdit](){
                textEdit->addWarning("⚠ 图片下载失败：未收到数据");
            }, Qt::QueuedConnection);
            return;
        }

        cout << "[ImageDL] 图片下载完成: " << imgData.size() << " bytes" << endl;

        // 保存到文件
        FILE* fp = fopen(savePath.toStdString().c_str(), "wb");
        if (!fp) return;
        fwrite(imgData.data(), 1, imgData.size(), fp);
        fclose(fp);

        // 在主线程中更新 UI 显示内联图片
        QMetaObject::invokeMethod(textEdit, [textEdit, savePath](){
            QImage image(savePath);
            if (image.isNull()) return;

            int maxWidth = 240;
            int maxHeight = 240;
            if (image.width() > maxWidth || image.height() > maxHeight)
                image = image.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

            QUrl imgUrl = QUrl::fromLocalFile(savePath);
            textEdit->document()->addResource(QTextDocument::ImageResource, imgUrl, image);

            textEdit->moveCursor(QTextCursor::End);
            QString html = QString(
                "<table width=\"100%%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\">"
                "<tr><td align=\"left\">"
                "<div style=\"margin: 2px 60px 2px 4px;\">"
                "<img src=\"%1\" width=\"%2\" height=\"%3\" />"
                "</div>"
                "</td></tr></table>"
            ).arg(imgUrl.toString()).arg(image.width()).arg(image.height());
            textEdit->insertHtml(html);
            textEdit->append("");
            auto *sb = textEdit->verticalScrollBar();
            if (sb) sb->setValue(sb->maximum());
        }, Qt::QueuedConnection);
    }).detach();
}

void ChatWindow::showInlineImage(const QString& filePath)
{
    QImage image(filePath);
    if (image.isNull()) return;

    int maxWidth = 240;
    int maxHeight = 240;
    if (image.width() > maxWidth || image.height() > maxHeight)
        image = image.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QUrl imgUrl = QUrl::fromLocalFile(filePath);
    mRecvTextEdit->document()->addResource(QTextDocument::ImageResource, imgUrl, image);

    mRecvTextEdit->moveCursor(QTextCursor::End);
    QString html = QString(
        "<table width=\"100%%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\">"
        "<tr><td align=\"left\">"
        "<div style=\"margin: 2px 60px 2px 4px;\">"
        "<img src=\"%1\" width=\"%2\" height=\"%3\" />"
        "</div>"
        "</td></tr></table>"
    ).arg(imgUrl.toString()).arg(image.width()).arg(image.height());
    mRecvTextEdit->insertHtml(html);
    mRecvTextEdit->append("");
    auto *sb = mRecvTextEdit->verticalScrollBar();
    if (sb) sb->setValue(sb->maximum());
}

void ChatWindow::shakeWindow()
{
    // 窗口抖动动画：快速左右上下来回晃动
    auto *group = new QSequentialAnimationGroup(this);
    QPoint origin = pos();

    // 抖动偏移序列：左-右-上-下-左-右 逐渐减小
    const int offsets[] = {10, -10, 8, -8, 6, -6, 4, -4, 2, -2, 0};
    const int count = sizeof(offsets) / sizeof(offsets[0]);

    for (int i = 0; i < count; i++)
    {
        auto *anim = new QPropertyAnimation(this, "pos");
        anim->setDuration(40);
        anim->setEndValue(origin + QPoint(offsets[i], (i % 2 == 0) ? offsets[i] / 2 : -offsets[i] / 2));
        group->addAnimation(anim);
    }

    // 确保最终回到原位
    auto *finalAnim = new QPropertyAnimation(this, "pos");
    finalAnim->setDuration(40);
    finalAnim->setEndValue(origin);
    group->addAnimation(finalAnim);

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void ChatWindow::loadHistory()
{
    auto& history = mEngine->getHistory();
    auto records = history.queryByIp(mFellow->getIp(), HISTORY_PAGE_SIZE, 0);

    mHistoryLoadedCount = records.size();
    mHasMoreHistory = ((int)records.size() >= HISTORY_PAGE_SIZE);

    if (records.empty()) return;

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
            QString fileText = record.isSelf ?
                QString("📤 发送了文件: %1").arg(QString::fromStdString(record.contentText)) :
                QString("📥 收到文件: %1").arg(QString::fromStdString(record.contentText));
            mRecvTextEdit->addWarning(fileText);
        }
        else if (record.contentType == static_cast<int>(ContentType::Image))
        {
            // 图片记录：contentText 现在存的是完整文件名（如 1744553000000_a3b4c5d6.jpg）
            // 兼容旧格式：如果不含 '_'，则是旧的 imageId，拼 .jpg 后缀
            QString imgDir = QDir::homePath() + "/.feiq/images";
            QString contentText = QString::fromStdString(record.contentText);
            QString imgFileName = contentText.contains('_') ? contentText : (contentText + ".jpg");
            QString imgPath = imgDir + "/" + imgFileName;

            auto content = make_shared<ImageContent>();
            // imageId 取 '_' 后的部分（或整个 contentText，旧格式）
            int underIdx = contentText.lastIndexOf('_');
            QString imageId = (underIdx >= 0) ?
                contentText.mid(underIdx + 1).replace(".jpg", "") : contentText;
            content->imageId = imageId.toStdString();
            content->localPath = imgPath.toStdString();

            if (record.isSelf)
                mRecvTextEdit->addMyContent(content.get(), record.timestamp);
            else
                mRecvTextEdit->addFellowContent(content.get(), record.timestamp);
        }
    }
}

void ChatWindow::loadMoreHistory()
{
    if (!mHasMoreHistory) return;

    mRecvTextEdit->setLoadingHistory(true);

    auto& history = mEngine->getHistory();
    auto records = history.queryByIp(mFellow->getIp(), HISTORY_PAGE_SIZE, mHistoryLoadedCount);

    if (records.empty())
    {
        mHasMoreHistory = false;
        mRecvTextEdit->setLoadingHistory(false);
        return;
    }

    mHistoryLoadedCount += records.size();
    mHasMoreHistory = ((int)records.size() >= HISTORY_PAGE_SIZE);

    // 记录当前滚动位置和文档高度
    auto *sb = mRecvTextEdit->verticalScrollBar();
    int oldMax = sb ? sb->maximum() : 0;

    // 将旧内容保存
    QString oldHtml = mRecvTextEdit->toHtml();

    // 清空并重新渲染：先渲染更早的历史，再渲染之前的内容
    mRecvTextEdit->clear();
    mRecvTextEdit->resetTimestamp();

    // 如果还有更多，显示提示
    if (mHasMoreHistory)
    {
        mRecvTextEdit->addWarning("↑ 继续上滑加载更多 ↑");
    }

    // 渲染这批更早的历史
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
            QString fileText = record.isSelf ?
                QString("📤 发送了文件: %1").arg(QString::fromStdString(record.contentText)) :
                QString("📥 收到文件: %1").arg(QString::fromStdString(record.contentText));
            mRecvTextEdit->addWarning(fileText);
        }
        else if (record.contentType == static_cast<int>(ContentType::Image))
        {
            QString imgDir = QDir::homePath() + "/.feiq/images";
            QString contentText = QString::fromStdString(record.contentText);
            QString imgFileName = contentText.contains('_') ? contentText : (contentText + ".jpg");
            QString imgPath = imgDir + "/" + imgFileName;

            auto content = make_shared<ImageContent>();
            int underIdx = contentText.lastIndexOf('_');
            QString imageId = (underIdx >= 0) ?
                contentText.mid(underIdx + 1).replace(".jpg", "") : contentText;
            content->imageId = imageId.toStdString();
            content->localPath = imgPath.toStdString();

            if (record.isSelf)
                mRecvTextEdit->addMyContent(content.get(), record.timestamp);
            else
                mRecvTextEdit->addFellowContent(content.get(), record.timestamp);
        }
    }

    // 追加旧内容
    mRecvTextEdit->moveCursor(QTextCursor::End);
    mRecvTextEdit->insertHtml(oldHtml);

    // 恢复滚动位置（保持之前看到的内容不动）
    if (sb)
    {
        int newMax = sb->maximum();
        int delta = newMax - oldMax;
        sb->setValue(delta > 0 ? delta : 0);
    }

    // 延迟恢复 loading 标志
    QTimer::singleShot(500, this, [this]() {
        mRecvTextEdit->setLoadingHistory(false);
    });
}
