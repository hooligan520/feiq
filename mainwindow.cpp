#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "chatwindow.h"
#include <thread>
#include <QDir>
#include <QDebug>
#include <QMessageBox>
#include <QTextCodec>
#include <QDateTime>
#include <QMenu>
#include <QtMac>
#include <QNetworkInterface>
#include "addfellowdialog.h"
#include "platformdepend.h"
#include "feiqwin.h"
#include "fellowitemwidget.h"
#include "settingsdialog.h"
#include "plugin/iplugin.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    qRegisterMetaType<shared_ptr<ViewEvent>>("ViewEventSharedPtr");

    connect(this, SIGNAL(showErrorAndQuit(QString)), this, SLOT(onShowErrorAndQuit(QString)));

    //加载配置
    auto settingFilePath = QDir::home().filePath(".feiq_setting.ini");
    mSettings = new Settings(settingFilePath, QSettings::IniFormat);
    mSettings->setIniCodec(QTextCodec::codecForName("UTF-8"));
    mTitle = mSettings->value("app/title", "飞秋").toString();
    setWindowTitle(mTitle);

    //初始化搜索对话框
    mSearchFellowDlg = new SearchFellowDlg(this);
    connect(mSearchFellowDlg, SIGNAL(onFellowSelected(const Fellow*)),
            this, SLOT(finishSearch(const Fellow*)));

    connect(ui->actionRefreshFellows, SIGNAL(triggered(bool)), this, SLOT(refreshFellowList()));
    connect(ui->actionAddFellow, SIGNAL(triggered(bool)), this, SLOT(addFellow()));

    mSearchFellowDlg->setSearchDriver(std::bind(&MainWindow::fellowSearchDriver, this, placeholders::_1));

    //初始化文件管理对话框
    mDownloadFileDlg = new FileManagerDlg(this);
    mDownloadFileDlg->setEngine(&mFeiq);
    connect(this, SIGNAL(statChanged(FileTask*)), mDownloadFileDlg, SLOT(statChanged(FileTask*)));
    connect(this, SIGNAL(progressChanged(FileTask*)), mDownloadFileDlg, SLOT(progressChanged(FileTask*)));

    //初始化好友列表
    mFellowList.bindTo(ui->fellowListWidget);
    connect(&mFellowList, SIGNAL(select(const Fellow*)), this, SLOT(openChartTo(const Fellow*)));

    //初始化搜索栏 - 实时过滤（基于自定义 Widget 的数据）
    connect(ui->searchEdit, &QLineEdit::textChanged, [this](const QString& text){
        for (int i = 0; i < ui->fellowListWidget->count(); i++) {
            auto item = ui->fellowListWidget->item(i);
            // 跳过分组 header
            if (item->data(Qt::UserRole + 100).isValid()) {
                item->setHidden(!text.isEmpty()); // 搜索时隐藏分组标题
                continue;
            }
            if (text.isEmpty()) {
                item->setHidden(false);
            } else {
                auto *widget = qobject_cast<FellowItemWidget*>(ui->fellowListWidget->itemWidget(item));
                if (widget && widget->fellow()) {
                    auto name = QString::fromStdString(widget->fellow()->getName());
                    auto ip = QString::fromStdString(widget->fellow()->getIp());
                    auto host = QString::fromStdString(widget->fellow()->getHost());
                    auto visible = name.contains(text, Qt::CaseInsensitive)
                                || ip.contains(text, Qt::CaseInsensitive)
                                || host.contains(text, Qt::CaseInsensitive);
                    item->setHidden(!visible);
                } else {
                    item->setHidden(false);
                }
            }
        }
    });

    //初始化好友列表右键菜单
    ui->fellowListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->fellowListWidget, &QListWidget::customContextMenuRequested, [this](const QPoint& pos){
        auto *item = ui->fellowListWidget->itemAt(pos);
        if (!item) return;
        // 跳过分组 header
        if (item->data(Qt::UserRole + 100).isValid()) return;

        auto *widget = qobject_cast<FellowItemWidget*>(ui->fellowListWidget->itemWidget(item));
        if (!widget || !widget->fellow()) return;

        const Fellow* fellow = widget->fellow();

        QMenu menu(this);
        auto *actSendMsg = menu.addAction("💬 发送消息");
        auto *actSendFile = menu.addAction("📎 发送文件");
        menu.addSeparator();
        auto *actViewInfo = menu.addAction("ℹ️ 查看信息");

        auto *chosen = menu.exec(ui->fellowListWidget->mapToGlobal(pos));
        if (chosen == actSendMsg) {
            openChartTo(fellow);
        } else if (chosen == actSendFile) {
            openChartTo(fellow);
            auto* chatWin = findChatWindow(fellow);
            if (chatWin) {
                chatWin->sendFile();
            }
        } else if (chosen == actViewInfo) {
            QString info = QString("用户名: %1\nIP: %2\n主机名: %3\nMAC: %4\n版本: %5\n状态: %6")
                .arg(QString::fromStdString(fellow->getName()))
                .arg(QString::fromStdString(fellow->getIp()))
                .arg(QString::fromStdString(fellow->getHost()))
                .arg(QString::fromStdString(fellow->getMac()))
                .arg(QString::fromStdString(fellow->version()))
                .arg(QString::fromStdString(Fellow::absenceStatusStr(fellow->absenceStatus())));
            QMessageBox::information(this, "好友信息", info);
        }
    });

    //初始化菜单
    connect(ui->actionSearchFellow, SIGNAL(triggered(bool)), this, SLOT(openSearchDlg()));
    connect(ui->actionSettings, SIGNAL(triggered(bool)), this, SLOT(openSettings()));
    connect(ui->actionOpendl, SIGNAL(triggered(bool)), this, SLOT(openDownloadDlg()));
    connect(ui->actionPlugins, SIGNAL(triggered(bool)), this, SLOT(openPlugins()));

    //初始化状态菜单（互斥选择）
    auto *statusGroup = new QActionGroup(this);
    statusGroup->addAction(ui->actionStatusOnline);
    statusGroup->addAction(ui->actionStatusAway);
    statusGroup->addAction(ui->actionStatusBusy);
    statusGroup->setExclusive(true);

    connect(ui->actionStatusOnline, &QAction::triggered, [this](){ setMyStatus(AbsenceStatus::Online); });
    connect(ui->actionStatusAway, &QAction::triggered, [this](){ setMyStatus(AbsenceStatus::Away); });
    connect(ui->actionStatusBusy, &QAction::triggered, [this](){ setMyStatus(AbsenceStatus::Busy); });

    //初始化平台相关特性
    PlatformDepend::instance().setMainWnd(this);

    //初始化飞秋引擎
    connect(this, SIGNAL(feiqViewEvent(shared_ptr<ViewEvent>)), this, SLOT(handleFeiqViewEvent(shared_ptr<ViewEvent>)));

    //后台初始化通信
    std::thread thd(&MainWindow::initFeiq, this);
    thd.detach();
}

MainWindow::~MainWindow()
{
    // 关闭所有聊天窗口
    for (auto& pair : mChatWindows)
    {
        if (pair.second)
        {
            pair.second->disconnect(); // 断开信号避免回调
            delete pair.second;
        }
    }
    mChatWindows.clear();

    mFeiq.stop();
    mSettings->sync();
    delete mSettings;
    delete mSearchFellowDlg;
    delete mDownloadFileDlg;
    delete ui;
}

void MainWindow::setFeiqWin(FeiqWin *feiqWin)
{
    mFeiqWin = feiqWin;
    mFeiqWin->init(this);
}

void MainWindow::onNotifyClicked(const QString& fellowIp)
{
    qDebug()<<fellowIp;
    auto fellow = mFeiq.getModel().findFirstFellowOf(fellowIp.toStdString());
    if (fellow)
        openChartTo(fellow.get());
}

void MainWindow::onNotifyReplied(long notifyId, const QString &fellowIp, const QString &reply)
{
    auto fellow = mFeiq.getModel().findFirstFellowOf(fellowIp.toStdString());
    if (fellow)
    {
        //回复消息
        auto content = make_shared<TextContent>();
        content->text = reply.toStdString();

        mFeiq.send(fellow, content);

        //设为已回复
        auto msgRepliedTo = findUnshownMessage(notifyId);
        if (msgRepliedTo)
        {
            msgRepliedTo->replied=true;
        }

        //将自己的回复放入未显示列表
        auto event = make_shared<MessageViewEvent>();
        event->contents.push_back(content);
        event->fellow = nullptr;

        auto& msg = addUnshownMessage(fellow.get(), event);
        msg.read = true;

        updateUnshownHint(fellow.get());
    }
}

ChatWindow* MainWindow::findOrCreateChatWindow(const Fellow *fellow)
{
    auto it = mChatWindows.find(fellow);
    if (it != mChatWindows.end() && it->second != nullptr)
    {
        return it->second;
    }

    auto* chatWin = new ChatWindow(fellow, &mFeiq, mSettings, this);
    chatWin->setAttribute(Qt::WA_DeleteOnClose, false); // 不自动删除，由我们管理
    connect(chatWin, &ChatWindow::windowClosed, this, &MainWindow::onChatWindowClosed);
    mChatWindows[fellow] = chatWin;
    return chatWin;
}

ChatWindow* MainWindow::findChatWindow(const Fellow *fellow)
{
    auto it = mChatWindows.find(fellow);
    if (it != mChatWindows.end())
        return it->second;
    return nullptr;
}

void MainWindow::onChatWindowClosed(const Fellow *fellow)
{
    // 聊天窗口关闭后，不立即删除，保留聊天记录
    // 下次打开时可以恢复
}

void MainWindow::openChartTo(const Fellow *fellow)
{
    mFellowList.top(*fellow);

    auto* chatWin = findOrCreateChatWindow(fellow);

    // 刷新未读消息到聊天窗口
    flushUnshown(fellow);
    updateUnshownHint(fellow);

    chatWin->bringToFront();
}

void MainWindow::handleFeiqViewEvent(shared_ptr<ViewEvent> event)
{
    if (event->what == ViewEventType::FELLOW_UPDATE)
    {
        auto e = static_cast<FellowViewEvent*>(event.get());
        mFellowList.update(*(e->fellow.get()));
        updateStatusBar();
    }
    else if (event->what == ViewEventType::SEND_TIMEO || event->what == ViewEventType::MESSAGE)
    {
        auto e = static_cast<FellowViewEvent*>(event.get());
        auto fellow = e->fellow.get();

        // 查找该好友的聊天窗口
        auto* chatWin = findChatWindow(fellow);

        if (chatWin && chatWin->isVisible() && chatWin->isActiveWindow())
        {
            // 聊天窗口可见且活跃，直接显示
            chatWin->handleViewEvent(event);
        }
        else
        {
            // 放入未读队列
            auto& umsg = addUnshownMessage(fellow, event);
            notifyUnshown(umsg);
            updateUnshownHint(fellow);
        }
    }
}

void MainWindow::refreshFellowList()
{
    mFeiq.sendImOnLine();
}

void MainWindow::addFellow()
{
    AddFellowDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
    {
        auto ip = dlg.getIp();
        userAddFellow(ip);
    }
}

void MainWindow::finishSearch(const Fellow *fellow)
{
    mFellowList.top(*fellow);
    openChartTo(fellow);
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(mSettings, this);
    if (dlg.exec() == QDialog::Accepted) {
        // 刷新自动应答设置
        bool autoReplyEnabled = mSettings->value("app/auto_reply_enable", false).toBool();
        auto autoReplyText = mSettings->value("app/auto_reply_text", "").toString();
        mFeiq.setAutoReply(autoReplyEnabled, autoReplyText.toStdString());
    }
}

void MainWindow::openSearchDlg()
{
    mSearchFellowDlg->exec();
}

void MainWindow::openPlugins()
{
    auto& allPlugins = PluginManager::instance().allPlugins;

    QDialog dlg(this);
    dlg.setWindowTitle("插件管理");
    dlg.setMinimumSize(360, 300);
    dlg.resize(360, 300);

    auto *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto *hint = new QLabel(QString("已注册插件 (%1 个):").arg(allPlugins.size()), &dlg);
    hint->setStyleSheet("font-weight: bold; font-size: 13px; color: #333;");
    layout->addWidget(hint);

    for (auto& iter : allPlugins)
    {
        auto *pluginWidget = new QWidget(&dlg);
        auto *pluginLayout = new QHBoxLayout(pluginWidget);
        pluginLayout->setContentsMargins(8, 4, 8, 4);

        auto *nameLabel = new QLabel(QString(iter.first), &dlg);
        nameLabel->setStyleSheet("font-size: 13px;");
        pluginLayout->addWidget(nameLabel);

        pluginLayout->addStretch();

        auto *enableCheck = new QCheckBox("启用", &dlg);
        enableCheck->setChecked(mSettings->value(QString(iter.first) + "/enable", "1").toBool());
        pluginLayout->addWidget(enableCheck);

        // 保存设置的连接
        connect(enableCheck, &QCheckBox::toggled, [this, iter](bool checked) {
            mSettings->setValue(QString(iter.first) + "/enable", checked ? "1" : "0");
            mSettings->sync();
        });

        layout->addWidget(pluginWidget);
    }

    if (allPlugins.empty())
    {
        auto *emptyLabel = new QLabel("暂无已注册的插件", &dlg);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet("color: #999; font-size: 13px;");
        layout->addWidget(emptyLabel);
    }

    layout->addStretch();

    auto *noteLabel = new QLabel("* 插件启用/禁用需重启后生效", &dlg);
    noteLabel->setStyleSheet("color: #999; font-size: 11px;");
    layout->addWidget(noteLabel);

    auto *closeBtn = new QPushButton("关闭", &dlg);
    closeBtn->setFixedWidth(72);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    dlg.exec();
}

void MainWindow::openDownloadDlg()
{
    mDownloadFileDlg->show();
    mDownloadFileDlg->raise();
}

void MainWindow::onStateChanged(FileTask *fileTask)
{
    if (fileTask->getState()==FileTaskState::Finish)
    {
        auto title = QString(fileTask->getTaskTypeDes().c_str())+"完成";
        PlatformDepend::instance().showNotify(title,
                                              fileTask->getContent()->filename.c_str(),
                                              fileTask->fellow()->getIp().c_str());
    }
    else if (fileTask->getState()==FileTaskState::Error)
    {
        auto title = QString(fileTask->getTaskTypeDes().c_str())+"失败";
        auto content = QString(fileTask->getContent()->filename.c_str());
        content += "\n";
        content += fileTask->getDetailInfo().c_str();
        PlatformDepend::instance().showNotify(title,
                                              content,
                                              fileTask->fellow()->getIp().c_str());
    }

    if (mDownloadFileDlg->isVisible())
    {
        emit statChanged(fileTask);
    }
}

void MainWindow::onProgress(FileTask *fileTask)
{
    if (mDownloadFileDlg->isVisible())
    {
        emit progressChanged(fileTask);
    }
}

void MainWindow::onEvent(shared_ptr<ViewEvent> event)
{
    emit feiqViewEvent(event);
}

void MainWindow::onShowErrorAndQuit(const QString &text)
{
    QMessageBox::warning(this, "出错了，为什么？你猜！", text, "退出应用");

    QApplication::exit(-1);
}

vector<const Fellow *> MainWindow::fellowSearchDriver(const QString &text)
{
    auto fellows = mFeiq.getModel().searchFellow(text.toStdString());
    vector<const Fellow*> result;
    for (auto fellow : fellows)
    {
        result.push_back(fellow.get());
    }
    return result;
}

void MainWindow::initFeiq()
{
    //配置飞秋
    auto name = mSettings->value("user/name").toString();
    if (name.isEmpty())
    {
        emit showErrorAndQuit("请先打开【"+mSettings->fileName()+"】设置用户名(user/name)");
        return;
    }

    mFeiq.setMyName(name.toStdString());
    mFeiq.setMyHost(mSettings->value("user/host","feiq by cy").toString().toStdString());

    auto customGrroup = mSettings->value("network/custom_group", "").toString();
    if (!customGrroup.isEmpty())
    {
        auto list = customGrroup.split("|");
        for (int i = 0; i < list.size(); i++)
        {
            QString ipPrefix = list[i];
            if (ipPrefix.endsWith("."))
            {
                for (int j = 2; j < 254; j++)
                {
                    auto ip = ipPrefix+QString::number(j);
                    mFeiq.addToBroadcast(ip.toStdString());
                }
            }
        }
    }

    mFeiq.setView(this);

    // 初始化聊天记录数据库
    auto historyPath = QDir::home().filePath(".feiq_history.db");
    mFeiq.initHistory(historyPath.toStdString());

    mFeiq.enableIntervalDetect(60);

    // 加载自动应答设置
    if (mSettings->value("app/auto_reply_enable", false).toBool()) {
        auto replyText = mSettings->value("app/auto_reply_text", "").toString();
        mFeiq.setAutoReply(true, replyText.toStdString());
    }

    // 更新状态栏用户信息
    auto localIp = getLocalIp();
    ui->statusUserLabel->setText(name + " (" + localIp + ")");

    //启动飞秋
    auto ret = mFeiq.start();
    if (!ret.first)
    {
        emit showErrorAndQuit(ret.second.c_str());
    }

    qDebug()<<"feiq started";
}

void MainWindow::updateUnshownHint(const Fellow *fellow)
{
    auto it = mUnshownEvents.find(fellow);
    if (it != mUnshownEvents.end())
    {
        auto count = it->second.size();
        if (count == 0)
        {
            mFellowList.mark(*fellow, "");
        }
        else
        {
            mFellowList.mark(*fellow, QString::number(count));
        }
    }

    setBadgeNumber(getUnreadCount());
}

int MainWindow::getUnreadCount()
{
    auto begin = mUnshownEvents.begin();
    auto end = mUnshownEvents.end();
    auto count = 0;
    for (auto it = begin; it != end; it++)
    {
        for (auto msg : it->second)
        {
            if (msg.isUnread())
                ++count;
        }
    }
    return count;
}

void MainWindow::flushUnshown(const Fellow *fellow)
{
    auto it = mUnshownEvents.find(fellow);
    if (it != mUnshownEvents.end())
    {
        auto* chatWin = findOrCreateChatWindow(fellow);
        auto& list = (*it).second;
        while (!list.empty())
        {
            auto msg = list.front();
            chatWin->handleViewEvent(msg.event);
            list.pop_front();
        }
    }
}

void MainWindow::setBadgeNumber(int number)
{
    PlatformDepend::instance().setBadgeNumber(number);
}

QString MainWindow::simpleTextOf(const Content *content)
{
    switch (content->type()) {
    case ContentType::Text:
        return static_cast<const TextContent*>(content)->text.c_str();
        break;
    case ContentType::File:
        return static_cast<const FileContent*>(content)->filename.c_str();
    case ContentType::Knock:
        return "窗口抖动";
    default:
        return "***";
        break;
    }
}

UnshownMessage &MainWindow::addUnshownMessage(const Fellow *fellow, shared_ptr<ViewEvent> event)
{
    UnshownMessage msg;
    msg.event = event;
    mUnshownEvents[fellow].push_back(msg);
    return mUnshownEvents[fellow].back();
}

UnshownMessage* MainWindow::findUnshownMessage(int id)
{
    auto begin = mUnshownEvents.begin();
    auto end = mUnshownEvents.end();

    for (auto it = begin; it != end; it++)
    {
        for (auto& msg : it->second){
            if (msg.notifyId == id)
                return &msg;
        }
    }

    return nullptr;
}

void MainWindow::userAddFellow(QString ip)
{
    //创建好友
    auto fellow = make_shared<Fellow>();
    fellow->setIp(ip.toStdString());
    fellow->setOnLine(true);
    mFeiq.getModel().addFellow(fellow);

    //添加到列表
    auto& ref = *(fellow.get());
    mFellowList.update(ref);
    mFellowList.top(ref);

    //发送在线
    mFeiq.sendImOnLine(fellow->getIp());
}

void MainWindow::notifyUnshown(UnshownMessage& umsg)
{
    auto event = umsg.event.get();
    if (event->what == ViewEventType::SEND_TIMEO)
    {
        auto e = static_cast<const SendTimeoEvent*>(event);
        auto fellow = e->fellow.get();
        umsg.notifyId = showNotification(fellow, "发送超时:"+simpleTextOf(e->content.get()));
    }
    else if (event->what == ViewEventType::MESSAGE)
    {
        auto e = static_cast<const MessageViewEvent*>(event);
        auto fellow = e->fellow.get();
        QString text="";
        bool first=false;
        for (auto content : e->contents)
        {
            auto t = simpleTextOf(content.get());
            if (first)
                text = t;
            else
                text = text+"\n"+t;
        }
        umsg.notifyId = showNotification(fellow, text);
    }
}

long MainWindow::showNotification(const Fellow *fellow, const QString &text)
{
    QString content(text);
    if (content.length()>100)
        content = content.left(100)+"...";
    return PlatformDepend::instance().showNotify(QString(fellow->getName().c_str())+":", content, fellow->getIp().c_str());
}

void MainWindow::setMyStatus(AbsenceStatus status)
{
    mFeiq.sendAbsence(status);

    // 更新状态栏显示
    QString statusText;
    switch (status) {
    case AbsenceStatus::Online:
        statusText = "在线";
        break;
    case AbsenceStatus::Away:
        statusText = "离开";
        break;
    case AbsenceStatus::Busy:
        statusText = "忙碌";
        break;
    default:
        statusText = "在线";
        break;
    }

    // 更新窗口标题
    if (status == AbsenceStatus::Online) {
        setWindowTitle(mTitle);
    } else {
        setWindowTitle(mTitle + " [" + statusText + "]");
    }

    // 更新状态栏用户信息
    auto name = mSettings->value("user/name").toString();
    auto localIp = getLocalIp();
    if (status == AbsenceStatus::Online) {
        ui->statusUserLabel->setText(name + " (" + localIp + ")");
    } else {
        ui->statusUserLabel->setText(name + " (" + localIp + ") " + statusText);
    }
}

void MainWindow::updateStatusBar()
{
    // 统计在线/总好友数
    int total = 0;
    int online = 0;

    for (int i = 0; i < ui->fellowListWidget->count(); i++)
    {
        auto *item = ui->fellowListWidget->item(i);
        // 跳过分组 header
        if (item->data(Qt::UserRole + 100).isValid()) continue;

        auto *widget = qobject_cast<FellowItemWidget*>(ui->fellowListWidget->itemWidget(item));
        if (widget && widget->fellow())
        {
            total++;
            if (widget->fellow()->isOnLine())
                online++;
        }
    }

    ui->statusOnlineLabel->setText(QString("在线: %1/%2").arg(online).arg(total));
}

QString MainWindow::getLocalIp()
{
    auto allInterfaces = QNetworkInterface::allInterfaces();
    for (auto& iface : allInterfaces)
    {
        if (iface.flags().testFlag(QNetworkInterface::IsUp)
            && iface.flags().testFlag(QNetworkInterface::IsRunning)
            && !iface.flags().testFlag(QNetworkInterface::IsLoopBack))
        {
            auto entries = iface.addressEntries();
            for (auto& entry : entries)
            {
                auto addr = entry.ip();
                if (addr.protocol() == QAbstractSocket::IPv4Protocol
                    && !addr.isLoopback())
                {
                    return addr.toString();
                }
            }
        }
    }
    return "0.0.0.0";
}
