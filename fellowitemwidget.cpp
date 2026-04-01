#include "fellowitemwidget.h"
#include <QFontMetrics>
#include <QPainter>
#include <QStyleOption>

FellowItemWidget::FellowItemWidget(const Fellow* fellow, QWidget *parent)
    : QWidget(parent), mFellow(fellow)
{
    setupUI();
    updateInfo(fellow);
}

void FellowItemWidget::setupUI()
{
    setObjectName("fellowItemWidget");
    setFixedHeight(56);

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 6, 10, 6);
    mainLayout->setSpacing(10);

    // === 头像（圆形，显示首字母/首字） ===
    mAvatarLabel = new QLabel(this);
    mAvatarLabel->setObjectName("fellowAvatar");
    mAvatarLabel->setFixedSize(40, 40);
    mAvatarLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(mAvatarLabel);

    // === 中间区域（用户名 + IP） ===
    auto *infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(2);

    // 用户名行：名称 + 状态圆点
    auto *nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(6);

    mNameLabel = new QLabel(this);
    mNameLabel->setObjectName("fellowName");
    nameRow->addWidget(mNameLabel);

    mStatusDot = new QLabel(this);
    mStatusDot->setObjectName("fellowStatusDot");
    mStatusDot->setFixedSize(8, 8);
    nameRow->addWidget(mStatusDot);

    nameRow->addStretch();
    infoLayout->addLayout(nameRow);

    // IP 行
    mIpLabel = new QLabel(this);
    mIpLabel->setObjectName("fellowIp");
    infoLayout->addWidget(mIpLabel);

    mainLayout->addLayout(infoLayout, 1);

    // === 右侧：未读角标 ===
    mUnreadBadge = new QLabel(this);
    mUnreadBadge->setObjectName("fellowUnreadBadge");
    mUnreadBadge->setFixedSize(22, 22);
    mUnreadBadge->setAlignment(Qt::AlignCenter);
    mUnreadBadge->hide();
    mainLayout->addWidget(mUnreadBadge);

    setLayout(mainLayout);
}

void FellowItemWidget::updateInfo(const Fellow *fellow)
{
    mFellow = fellow;

    // 用户名
    QString name = QString::fromStdString(fellow->getName());
    mNameLabel->setText(name);

    // IP
    mIpLabel->setText(QString::fromStdString(fellow->getIp()));

    // 头像文字和颜色
    mAvatarLabel->setText(getAvatarText(fellow->getName()));
    QColor avatarColor = getAvatarColor(fellow->getIp());

    // 根据在线状态调整样式
    updateAbsenceStatus(fellow->absenceStatus());

    // 头像背景色
    QString avatarStyle = QString(
        "background-color: %1;"
        "color: white;"
        "border-radius: 20px;"
        "font-size: 16px;"
        "font-weight: bold;"
    ).arg(fellow->isOnLine() ? avatarColor.name() : "#BDBDBD");
    mAvatarLabel->setStyleSheet(avatarStyle);
}

void FellowItemWidget::setUnreadCount(int count)
{
    if (count <= 0)
    {
        mUnreadBadge->hide();
    }
    else
    {
        mUnreadBadge->show();
        if (count > 99)
            mUnreadBadge->setText("99+");
        else
            mUnreadBadge->setText(QString::number(count));

        // 根据数字位数调整宽度
        if (count > 99)
            mUnreadBadge->setFixedWidth(32);
        else if (count > 9)
            mUnreadBadge->setFixedWidth(26);
        else
            mUnreadBadge->setFixedWidth(22);
    }
}

void FellowItemWidget::updateAbsenceStatus(AbsenceStatus status)
{
    switch (status) {
    case AbsenceStatus::Online:
        mStatusDot->setStyleSheet("background-color: #4CAF50; border-radius: 4px;"); // 绿色
        mNameLabel->setStyleSheet("color: #333333; font-size: 14px; font-weight: 500;");
        mIpLabel->setStyleSheet("color: #999999; font-size: 11px;");
        break;
    case AbsenceStatus::Away:
        mStatusDot->setStyleSheet("background-color: #FFC107; border-radius: 4px;"); // 黄色
        mNameLabel->setStyleSheet("color: #666666; font-size: 14px; font-weight: 500;");
        mIpLabel->setStyleSheet("color: #AAAAAA; font-size: 11px;");
        break;
    case AbsenceStatus::Busy:
        mStatusDot->setStyleSheet("background-color: #FF5722; border-radius: 4px;"); // 红色
        mNameLabel->setStyleSheet("color: #666666; font-size: 14px; font-weight: 500;");
        mIpLabel->setStyleSheet("color: #AAAAAA; font-size: 11px;");
        break;
    case AbsenceStatus::Offline:
    default:
        mStatusDot->setStyleSheet("background-color: #BDBDBD; border-radius: 4px;"); // 灰色
        mNameLabel->setStyleSheet("color: #AAAAAA; font-size: 14px; font-weight: 500;");
        mIpLabel->setStyleSheet("color: #CCCCCC; font-size: 11px;");
        break;
    }
}

QString FellowItemWidget::getAvatarText(const std::string &name)
{
    QString qName = QString::fromStdString(name);
    if (qName.isEmpty())
        return "?";

    // 取第一个字符（支持中文）
    return qName.left(1).toUpper();
}

QColor FellowItemWidget::getAvatarColor(const std::string &ip)
{
    // 根据 IP 最后一段生成稳定的颜色
    static const QColor colors[] = {
        QColor("#4A90D9"),  // 蓝
        QColor("#7B68EE"),  // 紫
        QColor("#FF6B6B"),  // 红
        QColor("#4ECDC4"),  // 青
        QColor("#F39C12"),  // 橙
        QColor("#2ECC71"),  // 绿
        QColor("#E74C3C"),  // 深红
        QColor("#9B59B6"),  // 深紫
        QColor("#1ABC9C"),  // 绿松石
        QColor("#E67E22"),  // 深橙
    };

    QString qIp = QString::fromStdString(ip);
    auto parts = qIp.split('.');
    int lastOctet = 0;
    if (parts.size() == 4)
        lastOctet = parts.last().toInt();

    return colors[lastOctet % 10];
}
