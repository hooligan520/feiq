#ifndef FELLOWITEMWIDGET_H
#define FELLOWITEMWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include "feiqlib/fellow.h"

/**
 * @brief 好友列表项自定义 Widget
 * 布局：
 * ┌────────────────────────┐
 * │ ┌────┐                 │
 * │ │头像│ 用户名     🟢/⚫ │
 * │ │图标│ 192.168.1.x      │
 * │ └────┘          [3]    │
 * └────────────────────────┘
 */
class FellowItemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FellowItemWidget(const Fellow* fellow, QWidget *parent = nullptr);

    void updateInfo(const Fellow* fellow);
    void setUnreadCount(int count);
    const Fellow* fellow() const { return mFellow; }

private:
    void setupUI();
    void updateAbsenceStatus(AbsenceStatus status);
    QString getAvatarText(const std::string& name);
    QColor getAvatarColor(const std::string& ip);

private:
    const Fellow* mFellow;

    QLabel* mAvatarLabel;      // 头像（文字首字母）
    QLabel* mNameLabel;        // 用户名
    QLabel* mIpLabel;          // IP 地址
    QLabel* mStatusDot;        // 在线状态圆点
    QLabel* mUnreadBadge;      // 未读消息角标
};

#endif // FELLOWITEMWIDGET_H
