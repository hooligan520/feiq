#ifndef FELLOWLISTWIDGET_H
#define FELLOWLISTWIDGET_H

#include "feiqlib/feiqmodel.h"
#include "fellowitemwidget.h"
#include <QListWidget>
#include <unordered_map>

//1.按"会话中"，"有新消息"、"在线"、"离线"优先级罗列好友信息
//2.支持查询好友
//3.支持在线/离线分组显示
class FellowListWidget : public QObject
{
    Q_OBJECT

public:
    typedef std::function<int (const Fellow&, const Fellow&)> RankPredict;

    FellowListWidget();
    void bindTo(QListWidget* widget);

public:
    void update(const Fellow& fellow);
    void top(const Fellow& fellow);
    void topSecond(const Fellow& fellow);
    void mark(const Fellow& fellow, const QString &info);
    void setRankPredict(RankPredict predict);

    // 获取好友项 widget（用于右键菜单等）
    FellowItemWidget* getFellowItemWidget(const Fellow& fellow);

signals:
    void select(const Fellow* fellow);

private slots:
    void itemChosen(QListWidgetItem *item);

private:
    FellowItemWidget* createItemWidget(const Fellow& fellow);
    QListWidgetItem* findFirstItem(const Fellow& fellow);
    int requestRow(const Fellow& fellow);
    const Fellow* getFellow(const QListWidgetItem* item);
    FellowItemWidget* getItemWidget(QListWidgetItem* item);

    // 分组相关
    void rebuildGroupHeaders();
    QListWidgetItem* findGroupHeader(const QString& groupName);
    int getOnlineSectionEnd();
    bool isGroupHeader(QListWidgetItem* item);

private:
    RankPredict mRankPredict;
    QListWidget* mWidget;
    bool mGroupEnabled = true;
};

#endif // FELLOWLISTWIDGET_H
