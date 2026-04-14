#include "broadcastdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QMessageBox>
#include <QGroupBox>

BroadcastDialog::BroadcastDialog(FeiqEngine* engine, QWidget *parent)
    : QDialog(parent), mEngine(engine)
{
    setWindowTitle("群发消息");
    resize(500, 400);
    setupUI();
    loadFellows();
}

void BroadcastDialog::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // 上半部分：好友选择
    auto *fellowGroup = new QGroupBox("选择好友", this);
    auto *fellowLayout = new QVBoxLayout(fellowGroup);

    // 全选/取消全选按钮行
    auto *btnRow = new QHBoxLayout();
    auto *selectAllBtn = new QPushButton("全选", this);
    auto *selectNoneBtn = new QPushButton("取消全选", this);
    mSelectedLabel = new QLabel("已选择: 0", this);
    mSelectedLabel->setStyleSheet("color: #666;");

    selectAllBtn->setFixedWidth(60);
    selectNoneBtn->setFixedWidth(80);

    btnRow->addWidget(selectAllBtn);
    btnRow->addWidget(selectNoneBtn);
    btnRow->addStretch();
    btnRow->addWidget(mSelectedLabel);

    connect(selectAllBtn, &QPushButton::clicked, this, &BroadcastDialog::selectAll);
    connect(selectNoneBtn, &QPushButton::clicked, this, &BroadcastDialog::selectNone);

    // 好友列表（多选）
    mFellowList = new QListWidget(this);
    mFellowList->setSelectionMode(QAbstractItemView::NoSelection);

    fellowLayout->addLayout(btnRow);
    fellowLayout->addWidget(mFellowList);
    mainLayout->addWidget(fellowGroup, 1);

    // 下半部分：消息输入
    auto *msgGroup = new QGroupBox("消息内容", this);
    auto *msgLayout = new QVBoxLayout(msgGroup);

    mMessageEdit = new QTextEdit(this);
    mMessageEdit->setPlaceholderText("输入要群发的消息...");
    mMessageEdit->setMaximumHeight(100);

    msgLayout->addWidget(mMessageEdit);
    mainLayout->addWidget(msgGroup);

    // 底部按钮行
    auto *bottomRow = new QHBoxLayout();
    mResultLabel = new QLabel("", this);
    mResultLabel->setStyleSheet("color: #4CAF50;");

    mSendBtn = new QPushButton("发送", this);
    mSendBtn->setFixedSize(100, 32);
    mSendBtn->setStyleSheet(
        "QPushButton { background-color: #4A90D9; color: white; border: none; "
        "border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background-color: #357ABD; }"
        "QPushButton:pressed { background-color: #2A5F9E; }"
    );

    connect(mSendBtn, &QPushButton::clicked, this, &BroadcastDialog::sendBroadcast);

    auto *cancelBtn = new QPushButton("关闭", this);
    cancelBtn->setFixedSize(80, 32);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::close);

    bottomRow->addWidget(mResultLabel);
    bottomRow->addStretch();
    bottomRow->addWidget(cancelBtn);
    bottomRow->addWidget(mSendBtn);
    mainLayout->addLayout(bottomRow);
}

void BroadcastDialog::loadFellows()
{
    mFellowList->clear();
    auto& model = mEngine->getModel();
    auto fellows = model.allFellows();

    for (auto& fellow : fellows)
    {
        if (!fellow->isOnLine())
            continue; // 只显示在线好友

        auto *item = new QListWidgetItem(mFellowList);
        auto *checkBox = new QCheckBox(
            QString("%1  (%2)")
                .arg(QString::fromStdString(fellow->getName()))
                .arg(QString::fromStdString(fellow->getIp())),
            this
        );

        // 存储 fellow IP 作为 data
        item->setData(Qt::UserRole, QString::fromStdString(fellow->getIp()));
        item->setSizeHint(QSize(0, 30));

        connect(checkBox, &QCheckBox::toggled, [item](bool checked){
            item->setData(Qt::UserRole + 1, checked);
        });
        connect(checkBox, &QCheckBox::toggled, this, &BroadcastDialog::updateSelectedCount);

        mFellowList->addItem(item);
        mFellowList->setItemWidget(item, checkBox);
    }

    updateSelectedCount();
}

void BroadcastDialog::sendBroadcast()
{
    auto text = mMessageEdit->toPlainText();
    if (text.isEmpty())
    {
        QMessageBox::warning(this, "提示", "请输入要发送的消息");
        return;
    }

    // 收集选中的好友
    int sentCount = 0;
    int failCount = 0;

    for (int i = 0; i < mFellowList->count(); i++)
    {
        auto *item = mFellowList->item(i);
        bool checked = item->data(Qt::UserRole + 1).toBool();
        if (!checked) continue;

        QString ip = item->data(Qt::UserRole).toString();
        auto fellow = mEngine->getModel().findFirstFellowOf(ip.toStdString());
        if (fellow)
        {
            auto content = make_shared<TextContent>();
            content->text = text.toStdString();
            auto ret = mEngine->send(fellow, content);
            if (ret.first)
                sentCount++;
            else
                failCount++;
        }
    }

    if (sentCount == 0 && failCount == 0)
    {
        QMessageBox::warning(this, "提示", "请至少选择一个好友");
        return;
    }

    QString result = QString("✅ 成功发送给 %1 人").arg(sentCount);
    if (failCount > 0)
        result += QString("，❌ %1 人失败").arg(failCount);

    mResultLabel->setText(result);
}

void BroadcastDialog::selectAll()
{
    for (int i = 0; i < mFellowList->count(); i++)
    {
        auto *item = mFellowList->item(i);
        auto *checkBox = qobject_cast<QCheckBox*>(mFellowList->itemWidget(item));
        if (checkBox) checkBox->setChecked(true);
    }
}

void BroadcastDialog::selectNone()
{
    for (int i = 0; i < mFellowList->count(); i++)
    {
        auto *item = mFellowList->item(i);
        auto *checkBox = qobject_cast<QCheckBox*>(mFellowList->itemWidget(item));
        if (checkBox) checkBox->setChecked(false);
    }
}

void BroadcastDialog::updateSelectedCount()
{
    int count = 0;
    for (int i = 0; i < mFellowList->count(); i++)
    {
        auto *item = mFellowList->item(i);
        if (item->data(Qt::UserRole + 1).toBool())
            count++;
    }
    mSelectedLabel->setText(QString("已选择: %1").arg(count));
}
