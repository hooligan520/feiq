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
        // 先把 Fellow 指针存好，takeItem 之后 widget 会被 Qt 自动删除，不能再访问
        const Fellow* fellowPtr = getFellow(item);
        if (!fellowPtr) fellowPtr = &fellow;

        int row = mWidget->row(item);
        mWidget->takeItem(row);   // widget 在这里被 Qt 自动 delete

        mWidget->insertItem(0, item);  // 先临时插到头部（防止 requestRow 计算偏移错误）
        item->setSizeHint(QSize(0, 56));
        auto *newWidget = createItemWidget(*fellowPtr);
        mWidget->setItemWidget(item, newWidget);

        mWidget->scrollToItem(item);
        mWidget->setCurrentItem(item);
    }

    // 重建分组 header（保证分组归属和计数正确）
    if (mGroupEnabled)
    {
        rebuildGroupHeaders();
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
        // 本机 Fellow 放在 self 分组末尾
        if (fellow.isSelf())
        {
            auto *onlineHeader = findGroupHeader("online");
            if (onlineHeader)
                return mWidget->row(onlineHeader);
            auto *offlineHeader = findGroupHeader("offline");
            if (offlineHeader)
                return mWidget->row(offlineHeader);
            return mWidget->count();
        }
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
    // 第一步：从 item->data(Qt::UserRole) 收集所有 Fellow 指针（不依赖 widget）
    std::vector<const Fellow*> selfFellows;   // 本机
    std::vector<const Fellow*> onlineFellows;
    std::vector<const Fellow*> offlineFellows;

    for (int i = 0; i < mWidget->count(); i++)
    {
        auto *item = mWidget->item(i);
        if (isGroupHeader(item)) continue;
        const Fellow* f = getFellow(item);
        if (!f) continue;
        if (f->isSelf())
            selfFellows.push_back(f);
        else if (f->isOnLine())
            onlineFellows.push_back(f);
        else
            offlineFellows.push_back(f);
    }

    // 第二步：清空列表（takeItem 会销毁关联的 widget，但 Fellow 指针来自模型，仍然有效）
    while (mWidget->count() > 0)
    {
        auto *item = mWidget->takeItem(0);
        delete item;
    }

    // 第三步：本机分组 header（只有本机时才显示）
    if (!selfFellows.empty())
    {
        auto *selfHeader = new QListWidgetItem();
        selfHeader->setData(GROUP_HEADER_ROLE, "self");
        selfHeader->setSizeHint(QSize(0, 28));
        selfHeader->setFlags(selfHeader->flags() & ~Qt::ItemIsSelectable);

        auto *headerWidget = new QWidget();
        headerWidget->setObjectName("groupHeaderWidget");
        headerWidget->setFixedHeight(28);
        auto *layout = new QHBoxLayout(headerWidget);
        layout->setContentsMargins(12, 0, 12, 0);
        auto *label = new QLabel("本机");
        label->setObjectName("groupHeaderLabel");
        layout->addWidget(label);
        layout->addStretch();

        mWidget->addItem(selfHeader);
        mWidget->setItemWidget(selfHeader, headerWidget);

        for (const Fellow* f : selfFellows)
        {
            auto *newItem = new QListWidgetItem();
            newItem->setData(Qt::UserRole, QVariant::fromValue((void*)f));
            newItem->setSizeHint(QSize(0, 56));
            mWidget->addItem(newItem);
            auto *newWidget = createItemWidget(*f);
            mWidget->setItemWidget(newItem, newWidget);
        }
    }

    // 第四步：创建在线好友分组 header
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
        auto *label = new QLabel(QString("在线好友 (%1)").arg(onlineFellows.size()));
        label->setObjectName("groupHeaderLabel");
        layout->addWidget(label);
        layout->addStretch();

        mWidget->addItem(onlineHeader);
        mWidget->setItemWidget(onlineHeader, headerWidget);
    }

    // 第四步：添加在线好友
    for (const Fellow* f : onlineFellows)
    {
        auto *newItem = new QListWidgetItem();
        newItem->setData(Qt::UserRole, QVariant::fromValue((void*)f));
        newItem->setSizeHint(QSize(0, 56));
        mWidget->addItem(newItem);
        auto *newWidget = createItemWidget(*f);
        mWidget->setItemWidget(newItem, newWidget);
    }

    // 第五步：创建离线好友分组 header
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
        auto *label = new QLabel(QString("离线好友 (%1)").arg(offlineFellows.size()));
        label->setObjectName("groupHeaderLabel");
        layout->addWidget(label);
        layout->addStretch();

        mWidget->addItem(offlineHeader);
        mWidget->setItemWidget(offlineHeader, headerWidget);
    }

    // 第六步：添加离线好友
    for (const Fellow* f : offlineFellows)
    {
        auto *newItem = new QListWidgetItem();
        newItem->setData(Qt::UserRole, QVariant::fromValue((void*)f));
        newItem->setSizeHint(QSize(0, 56));
        mWidget->addItem(newItem);
        auto *newWidget = createItemWidget(*f);
        mWidget->setItemWidget(newItem, newWidget);
    }
}
