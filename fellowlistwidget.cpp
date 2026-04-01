#include "fellowlistwidget.h"
#include <QDebug>
#include <QLabel>
#include <QHBoxLayout>

// 分组标题使用特殊 UserRole 标识
static const int GROUP_HEADER_ROLE = Qt::UserRole + 100;

FellowListWidget::FellowListWidget()
{

}

void FellowListWidget::bindTo(QListWidget *widget)
{
    mWidget = widget;
    connect(mWidget, &QListWidget::itemClicked, this, &FellowListWidget::itemChosen);

    // 初始化时就显示分组 header
    if (mGroupEnabled)
    {
        rebuildGroupHeaders();
    }
}

void FellowListWidget::update(const Fellow &fellow)
{
    auto item = findFirstItem(fellow);
    if (item == nullptr)
    {
        int row = requestRow(fellow);
        item = new QListWidgetItem();
        item->setData(Qt::UserRole, QVariant::fromValue((void*)&fellow));
        item->setSizeHint(QSize(0, 56));
        mWidget->insertItem(row, item);

        auto *widget = createItemWidget(fellow);
        mWidget->setItemWidget(item, widget);
    }
    else
    {
        auto *widget = getItemWidget(item);
        if (widget)
        {
            widget->updateInfo(&fellow);
        }
        item->setData(Qt::UserRole, QVariant::fromValue((void*)&fellow));
    }

    // 更新分组位置
    if (mGroupEnabled)
    {
        rebuildGroupHeaders();
    }
}

void FellowListWidget::top(const Fellow &fellow)
{
    auto item = findFirstItem(fellow);
    if (item != nullptr)
    {
        int row = mWidget->row(item);
        auto *widget = getItemWidget(item);

        // takeItem 会解除 widget 关联，需要重新设置
        mWidget->takeItem(row);

        // 找到在线区域的起始位置（跳过分组 header）
        int insertRow = 0;
        for (int i = 0; i < mWidget->count(); i++)
        {
            if (!isGroupHeader(mWidget->item(i)))
            {
                insertRow = i;
                break;
            }
            insertRow = i + 1;
        }

        mWidget->insertItem(insertRow, item);
        item->setSizeHint(QSize(0, 56));

        if (widget)
        {
            // takeItem 后 widget 被自动删除，需要重建
            auto *newWidget = createItemWidget(*(widget->fellow()));
            mWidget->setItemWidget(item, newWidget);
        }
        else
        {
            auto *fellow_ptr = getFellow(item);
            if (fellow_ptr)
            {
                auto *newWidget = createItemWidget(*fellow_ptr);
                mWidget->setItemWidget(item, newWidget);
            }
        }

        mWidget->scrollToItem(item);
        mWidget->setCurrentItem(item);
    }
}

//TODO:take->insert的方式或导致如果item是当前焦点，则移动后焦点丢失
void FellowListWidget::topSecond(const Fellow &fellow)
{
    auto item = findFirstItem(fellow);
    if (item != nullptr)
    {
        int row = mWidget->row(item);
        const Fellow* fellow_ptr = getFellow(item);

        mWidget->takeItem(row);

        // 找到第二个位置（跳过分组 header）
        int insertRow = 0;
        int normalCount = 0;
        for (int i = 0; i < mWidget->count(); i++)
        {
            if (!isGroupHeader(mWidget->item(i)))
            {
                normalCount++;
                if (normalCount >= 1)
                {
                    insertRow = i + 1;
                    break;
                }
            }
            insertRow = i + 1;
        }

        mWidget->insertItem(insertRow, item);
        item->setSizeHint(QSize(0, 56));

        // 重建 widget
        if (fellow_ptr)
        {
            auto *newWidget = createItemWidget(*fellow_ptr);
            mWidget->setItemWidget(item, newWidget);
        }
    }
}

void FellowListWidget::mark(const Fellow &fellow, const QString &info)
{
    auto item = findFirstItem(fellow);
    if (item != nullptr)
    {
        auto *widget = getItemWidget(item);
        if (widget)
        {
            if (info.isEmpty())
            {
                widget->setUnreadCount(0);
            }
            else
            {
                bool ok;
                int count = info.toInt(&ok);
                widget->setUnreadCount(ok ? count : 1);
            }
        }

        // 有未读消息时提升位置
        if (!info.isEmpty())
        {
            if (mWidget->row(item) != 0)
            {
                if (mWidget->currentRow() == 0)
                    topSecond(fellow);
                else
                    top(fellow);

                // top/topSecond 后 widget 被重建，需要重新设置角标
                auto newItem = findFirstItem(fellow);
                if (newItem)
                {
                    auto *newWidget = getItemWidget(newItem);
                    if (newWidget)
                    {
                        bool ok;
                        int count = info.toInt(&ok);
                        newWidget->setUnreadCount(ok ? count : 1);
                    }
                }
            }
        }
    }
}

void FellowListWidget::setRankPredict(FellowListWidget::RankPredict predict)
{
    mRankPredict = predict;
}

FellowItemWidget* FellowListWidget::getFellowItemWidget(const Fellow &fellow)
{
    auto item = findFirstItem(fellow);
    if (item)
        return getItemWidget(item);
    return nullptr;
}

void FellowListWidget::itemChosen(QListWidgetItem *item)
{
    if (item == nullptr) return;

    // 忽略分组 header 的点击
    if (isGroupHeader(item)) return;

    auto fellow = getFellow(item);
    emit select(fellow);
}

FellowItemWidget* FellowListWidget::createItemWidget(const Fellow &fellow)
{
    return new FellowItemWidget(&fellow);
}

QListWidgetItem *FellowListWidget::findFirstItem(const Fellow &fellow)
{
    auto count = mWidget->count();
    for (int i = 0; i < count; i++)
    {
        auto widget = mWidget->item(i);
        if (isGroupHeader(widget)) continue;
        auto f = getFellow(widget);
        if (f && f->getIp() == fellow.getIp())
            return widget;
    }

    return nullptr;
}

int FellowListWidget::requestRow(const Fellow &fellow)
{
    if (mGroupEnabled)
    {
        // 分组模式下：在线好友放在"在线好友"header 之后，离线好友放在"离线好友"header 之后
        if (fellow.isOnLine())
        {
            // 找到在线区域的末尾
            return getOnlineSectionEnd();
        }
        else
        {
            // 放在列表末尾
            return mWidget->count();
        }
    }

    auto count = mWidget->count();

    if (!mRankPredict)
        return count;

    int row = count;
    for (; row > 0; row--)
    {
        auto f = getFellow(mWidget->item(row-1));
        if (f)
        {
            auto ret = mRankPredict(*f, fellow);
            if (ret >= 0)
                break;
        }
    }

    return row;
}

const Fellow *FellowListWidget::getFellow(const QListWidgetItem *item)
{
    if (item->data(GROUP_HEADER_ROLE).isValid()) return nullptr;
    return static_cast<const Fellow*>(item->data(Qt::UserRole).value<void*>());
}

FellowItemWidget* FellowListWidget::getItemWidget(QListWidgetItem *item)
{
    return qobject_cast<FellowItemWidget*>(mWidget->itemWidget(item));
}

// === 分组相关 ===

bool FellowListWidget::isGroupHeader(QListWidgetItem *item)
{
    return item && item->data(GROUP_HEADER_ROLE).isValid();
}

QListWidgetItem* FellowListWidget::findGroupHeader(const QString &groupName)
{
    for (int i = 0; i < mWidget->count(); i++)
    {
        auto *item = mWidget->item(i);
        if (isGroupHeader(item) && item->data(GROUP_HEADER_ROLE).toString() == groupName)
            return item;
    }
    return nullptr;
}

int FellowListWidget::getOnlineSectionEnd()
{
    // 在线区域：从"在线好友"header 到"离线好友"header 之间
    auto *offlineHeader = findGroupHeader("offline");
    if (offlineHeader)
    {
        return mWidget->row(offlineHeader);
    }
    return mWidget->count();
}

void FellowListWidget::rebuildGroupHeaders()
{
    // 统计在线和离线好友数量
    int onlineCount = 0;
    int offlineCount = 0;

    for (int i = 0; i < mWidget->count(); i++)
    {
        auto *item = mWidget->item(i);
        if (isGroupHeader(item)) continue;
        auto *widget = getItemWidget(item);
        if (widget && widget->fellow())
        {
            if (widget->fellow()->isOnLine())
                onlineCount++;
            else
                offlineCount++;
        }
    }

    // 先移除旧的分组 header
    for (int i = mWidget->count() - 1; i >= 0; i--)
    {
        auto *item = mWidget->item(i);
        if (isGroupHeader(item))
        {
            delete mWidget->takeItem(i);
        }
    }

    // 收集所有好友项
    struct FellowEntry {
        QListWidgetItem* item;
        FellowItemWidget* widget;
        bool online;
    };
    std::vector<FellowEntry> entries;
    for (int i = 0; i < mWidget->count(); i++)
    {
        auto *item = mWidget->item(i);
        auto *widget = getItemWidget(item);
        bool online = false;
        if (widget && widget->fellow())
            online = widget->fellow()->isOnLine();
        entries.push_back({item, widget, online});
    }

    // 分离在线和离线
    std::vector<FellowEntry> onlineEntries, offlineEntries;
    for (auto& e : entries)
    {
        if (e.online)
            onlineEntries.push_back(e);
        else
            offlineEntries.push_back(e);
    }

    // 清空列表（不删除 widget，因为 takeItem 后 widget 会被清除）
    // 需要重新创建
    while (mWidget->count() > 0)
        mWidget->takeItem(0);

    // 创建在线好友分组 header
    if (true) // 始终显示在线分组
    {
        auto *onlineHeader = new QListWidgetItem();
        onlineHeader->setData(GROUP_HEADER_ROLE, "online");
        onlineHeader->setSizeHint(QSize(0, 28));
        onlineHeader->setFlags(onlineHeader->flags() & ~Qt::ItemIsSelectable);

        auto *headerWidget = new QWidget();
        headerWidget->setObjectName("groupHeaderWidget");
        headerWidget->setFixedHeight(28);
        auto *layout = new QHBoxLayout(headerWidget);
        layout->setContentsMargins(12, 0, 12, 0);
        auto *label = new QLabel(QString("在线好友 (%1)").arg(onlineCount));
        label->setObjectName("groupHeaderLabel");
        layout->addWidget(label);
        layout->addStretch();

        mWidget->addItem(onlineHeader);
        mWidget->setItemWidget(onlineHeader, headerWidget);
    }

    // 添加在线好友
    for (auto& e : onlineEntries)
    {
        auto *newItem = new QListWidgetItem();
        const Fellow* f = e.widget ? e.widget->fellow() : nullptr;
        if (f)
        {
            newItem->setData(Qt::UserRole, QVariant::fromValue((void*)f));
            newItem->setSizeHint(QSize(0, 56));
            mWidget->addItem(newItem);
            auto *newWidget = createItemWidget(*f);
            mWidget->setItemWidget(newItem, newWidget);
        }
    }

    // 创建离线好友分组 header
    {
        auto *offlineHeader = new QListWidgetItem();
        offlineHeader->setData(GROUP_HEADER_ROLE, "offline");
        offlineHeader->setSizeHint(QSize(0, 28));
        offlineHeader->setFlags(offlineHeader->flags() & ~Qt::ItemIsSelectable);

        auto *headerWidget = new QWidget();
        headerWidget->setObjectName("groupHeaderWidget");
        headerWidget->setFixedHeight(28);
        auto *layout = new QHBoxLayout(headerWidget);
        layout->setContentsMargins(12, 0, 12, 0);
        auto *label = new QLabel(QString("离线好友 (%1)").arg(offlineCount));
        label->setObjectName("groupHeaderLabel");
        layout->addWidget(label);
        layout->addStretch();

        mWidget->addItem(offlineHeader);
        mWidget->setItemWidget(offlineHeader, headerWidget);
    }

    // 添加离线好友
    for (auto& e : offlineEntries)
    {
        auto *newItem = new QListWidgetItem();
        const Fellow* f = e.widget ? e.widget->fellow() : nullptr;
        if (f)
        {
            newItem->setData(Qt::UserRole, QVariant::fromValue((void*)f));
            newItem->setSizeHint(QSize(0, 56));
            mWidget->addItem(newItem);
            auto *newWidget = createItemWidget(*f);
            mWidget->setItemWidget(newItem, newWidget);
        }
    }
}
