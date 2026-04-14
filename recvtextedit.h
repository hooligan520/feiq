#ifndef RECVTEXTEDIT_H
#define RECVTEXTEDIT_H

#include "feiqlib/content.h"
#include "feiqlib/fellow.h"
#include <QObject>
#include <unordered_map>
#include <QTextEdit>
#include <QColor>

using namespace std;

class RecvTextEdit: public QTextEdit
{
    Q_OBJECT
public:
    RecvTextEdit(QWidget* parent = 0);

    // 设置"我"的信息，用于生成自己的头像
    void setMyInfo(const QString& name, const QString& ip);

protected:
    virtual void mousePressEvent(QMouseEvent *e) override;
    virtual void mouseReleaseEvent(QMouseEvent *e) override;
    virtual void scrollContentsBy(int dx, int dy) override;

public:
    void addFellowContent(const Content* content, long long msSinceEpoch);
    void addMyContent(const Content* content, long long msSinceEpoch);
    void setCurFellow(const Fellow* fellow);
    void addWarning(const QString& warning);
    const Fellow* curFellow();

    // 在顶部插入历史消息（保持滚动位置）
    void prependFellowContent(const Content* content, long long msSinceEpoch);
    void prependMyContent(const Content* content, long long msSinceEpoch);

    // 重置时间戳状态（加载历史时用）
    void resetTimestamp() { mLastEdit = 0; }

signals:
    void navigateToFileTask(IdType packetNo, IdType fileId, bool upload);
    void scrolledToTop(); // 滚动到顶部时触发，用于加载更多历史

private:
    QString friendlyTimeStr(long long msSinceEpoch);
    void addContent(const Content* content, long long msSinceEpoch, bool mySelf);
    void showTimestamp(long long msSinceEpoch);
    void showBubble(const Content* content, long long msSinceEpoch, bool mySelf);

    // 内容渲染为 HTML
    QString contentToHtml(const Content* content, bool mySelf);
    QString fileToHtml(const FileContent* content, bool fromMySelf);
    QString imageToHtml(const ImageContent* content);
    QString textToHtml(const TextContent* content);
    QString knockToHtml(const KnockContent* content, bool mySelf);
    QString unsupportToHtml(const QString &text = "");

    void drawDaySeperatorIfNewDay(long long sinceEpoch);
    void parseLink(const QString& link);

    // 头像生成
    QImage generateAvatar(const QString& text, const QColor& bgColor, int size = 36);
    QString getAvatarText(const QString& name);
    QColor getAvatarColor(const QString& ip);
    QString ensureAvatarResource(bool mySelf);

private:
    const Fellow* mFellow = nullptr;
    unordered_map<const Fellow*, QTextDocument*> mDocs;
    long long mLastEdit=0;
    QString mPressedAnchor;

    // "我"的信息
    QString mMyName;
    QString mMyIp;

    // 防止滚动加载重复触发
    bool mLoadingHistory = false;
public:
    void setLoadingHistory(bool loading) { mLoadingHistory = loading; }
};

#endif // RECVTEXTEDIT_H
