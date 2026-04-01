#include "recvtextedit.h"
#include <QDate>
#include "emoji.h"
#include <QMouseEvent>
#include <QScrollBar>

RecvTextEdit::RecvTextEdit(QWidget *parent)
    :QTextEdit(parent)
{
    setTextInteractionFlags(Qt::LinksAccessibleByMouse|Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
}

void RecvTextEdit::mousePressEvent(QMouseEvent *e)
{
    mPressedAnchor =  (e->button() & Qt::LeftButton) ? anchorAt(e->pos()) : "";
    QTextEdit::mousePressEvent(e);
}

void RecvTextEdit::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() & Qt::LeftButton)
    {
        if (anchorAt(e->pos()) == mPressedAnchor && !mPressedAnchor.isEmpty())
            parseLink(mPressedAnchor);
    }
    QTextEdit::mouseReleaseEvent(e);
}

void RecvTextEdit::addFellowContent(const Content *content, long long msSinceEpoch)
{
    addContent(content, msSinceEpoch, false);
}

void RecvTextEdit::addMyContent(const Content *content, long long msSinceEpoch)
{
    addContent(content, msSinceEpoch, true);
}

void RecvTextEdit::addContent(const Content *content, long long msSinceEpoch, bool mySelf)
{
    drawDaySeperatorIfNewDay(msSinceEpoch);
    showTimestamp(msSinceEpoch);
    showBubble(content, msSinceEpoch, mySelf);
    moveCursor(QTextCursor::End);
    auto *sb = verticalScrollBar();
    if (sb) sb->setValue(sb->maximum());
}

void RecvTextEdit::showTimestamp(long long msSinceEpoch)
{
    // 如果与上条消息间隔超过5分钟，显示时间戳
    if (mLastEdit > 0)
    {
        long long diff = msSinceEpoch - mLastEdit;
        if (diff < 5 * 60 * 1000 && diff > 0)
            return; // 5分钟内不重复显示时间
    }

    moveCursor(QTextCursor::End);
    QString html = QString(
        "<div align=\"center\" style=\"margin: 12px 0 8px 0;\">"
        "<span style=\"color: #999999; font-size: 11px; background-color: #F0F0F0; "
        "padding: 2px 8px; border-radius: 4px;\">"
        "%1"
        "</span></div>"
    ).arg(timeStr(msSinceEpoch));
    insertHtml(html);
    append("");
}

void RecvTextEdit::showBubble(const Content *content, long long msSinceEpoch, bool mySelf)
{
    Q_UNUSED(msSinceEpoch);

    QString name;
    if (mySelf)
    {
        name = "我";
    }
    else
    {
        name = mFellow == nullptr ? "匿名" : QString::fromStdString(mFellow->getName());
    }

    QString contentHtml = contentToHtml(content, mySelf);

    // 气泡样式
    QString bubbleBgColor, bubbleTextColor, align, nameColor;
    if (mySelf)
    {
        bubbleBgColor = "#95EC69";    // 微信绿
        bubbleTextColor = "#000000";
        nameColor = "#4A90D9";
        align = "right";
    }
    else
    {
        bubbleBgColor = "#FFFFFF";
        bubbleTextColor = "#333333";
        nameColor = "#4CAF50";
        align = "left";
    }

    // 构建气泡 HTML
    // 用 table 实现左右对齐（QTextEdit 对 div float 支持有限）
    QString html;
    if (mySelf)
    {
        html = QString(
            "<table width=\"100%%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\">"
            "<tr><td align=\"right\">"
            "<div style=\"display: inline-block; text-align: right; margin: 2px 4px 2px 60px;\">"
            "<div style=\"font-size: 11px; color: %1; margin-bottom: 2px; text-align: right;\">%2</div>"
            "<div style=\"background-color: %3; color: %4; padding: 8px 12px; "
            "border-radius: 12px; border-top-right-radius: 4px; "
            "display: inline-block; text-align: left; font-size: 13px;\">"
            "%5</div>"
            "</div>"
            "</td></tr></table>"
        ).arg(nameColor, name, bubbleBgColor, bubbleTextColor, contentHtml);
    }
    else
    {
        html = QString(
            "<table width=\"100%%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\">"
            "<tr><td align=\"left\">"
            "<div style=\"display: inline-block; text-align: left; margin: 2px 60px 2px 4px;\">"
            "<div style=\"font-size: 11px; color: %1; margin-bottom: 2px;\">%2</div>"
            "<div style=\"background-color: %3; color: %4; padding: 8px 12px; "
            "border-radius: 12px; border-top-left-radius: 4px; "
            "display: inline-block; text-align: left; font-size: 13px;\">"
            "%5</div>"
            "</div>"
            "</td></tr></table>"
        ).arg(nameColor, name, bubbleBgColor, bubbleTextColor, contentHtml);
    }

    moveCursor(QTextCursor::End);
    insertHtml(html);
    append("");

    mLastEdit = QDateTime::currentMSecsSinceEpoch();
}

QString RecvTextEdit::contentToHtml(const Content *content, bool mySelf)
{
    switch (content->type())
    {
    case ContentType::File:
        return fileToHtml(static_cast<const FileContent*>(content), mySelf);
    case ContentType::Knock:
        return knockToHtml(static_cast<const KnockContent*>(content), mySelf);
    case ContentType::Image:
        return imageToHtml(static_cast<const ImageContent*>(content));
    case ContentType::Text:
        return textToHtml(static_cast<const TextContent*>(content));
    default:
        return unsupportToHtml("");
    }
}

QString RecvTextEdit::fileToHtml(const FileContent *content, bool fromMySelf)
{
    if (content->fileType == IPMSG_FILE_REGULAR)
    {
        // 文件卡片样式
        QString link = QString("%1_%2_%3")
            .arg(content->packetNo)
            .arg(content->fileId)
            .arg(fromMySelf ? "up" : "down");

        QString sizeStr;
        if (content->size > 1024 * 1024)
            sizeStr = QString::number(content->size / (1024.0 * 1024.0), 'f', 1) + " MB";
        else if (content->size > 1024)
            sizeStr = QString::number(content->size / 1024.0, 'f', 1) + " KB";
        else
            sizeStr = QString::number(content->size) + " B";

        return QString(
            "<div style=\"background-color: #F8F8F8; border: 1px solid #E0E0E0; "
            "border-radius: 8px; padding: 8px 12px; margin: 4px 0;\">"
            "📄 <a href=\"%1\" style=\"color: #4A90D9; text-decoration: none;\">%2</a>"
            "<br/><span style=\"color: #999999; font-size: 11px;\">%3</span>"
            "</div>"
        ).arg(link, QString::fromStdString(content->filename), sizeStr);
    }
    else
    {
        return unsupportToHtml("对方发来非普通文件（可能是文件夹），收不来……");
    }
}

QString RecvTextEdit::imageToHtml(const ImageContent *content)
{
    Q_UNUSED(content);
    return unsupportToHtml("对方发来图片，来图片，图片，片……额~还不支持!");
}

QString RecvTextEdit::textToHtml(const TextContent *content)
{
    auto str = QString::fromStdString(content->text);
    auto htmlStr = str.toHtmlEscaped();
    htmlStr.replace("\r\n", "<br>");
    htmlStr.replace("\r", "<br>");
    htmlStr.replace("\n", "<br>");

    for (auto i = 0; i < EMOJI_LEN; i++)
    {
         auto resName = QString(":/default/res/face/")+QString::number(i+1)+".gif";
         auto emojiStr = QString(g_emojis[i]).toHtmlEscaped();
         QString imgTag = "<img src=\""+resName+"\" width=\"24\" height=\"24\"/>";
         htmlStr.replace(emojiStr, imgTag);
    }

    return htmlStr;
}

QString RecvTextEdit::knockToHtml(const KnockContent *content, bool mySelf)
{
    Q_UNUSED(content);
    if (mySelf)
        return "<span style=\"color: #999999;\">👋 发送了一个窗口抖动</span>";
    else
        return "<span style=\"color: #999999;\">👋 发来窗口抖动</span>";
}

QString RecvTextEdit::unsupportToHtml(const QString &text)
{
    QString t = text;
    if (t.isEmpty())
        t = "对方发来尚未支持的内容，无法显示";
    return "<span style=\"color: #FF6B6B;\">" + t + "</span>";
}

void RecvTextEdit::setCurFellow(const Fellow *fellow)
{
    if (mFellow)
        mDocs[mFellow] = document()->clone();//document将被清除或删除了，需clone

    auto it = mDocs.find(fellow);
    if (it != mDocs.end())
    {
        setDocument((*it).second);
        moveCursor(QTextCursor::End);
    }
    else
    {
        clear();
    }

    mFellow = fellow;
}

void RecvTextEdit::addWarning(const QString &warning)
{
    moveCursor(QTextCursor::End);
    QString html = QString(
        "<div align=\"center\" style=\"margin: 8px 0;\">"
        "<span style=\"color: #999999; font-size: 12px;\">"
        "%1</span></div>"
    ).arg(warning);
    insertHtml(html);
    append("");
}

const Fellow *RecvTextEdit::curFellow()
{
    return mFellow;
}

void RecvTextEdit::parseLink(const QString &link)
{
    QStringList parts = link.split("_");
    if (parts.count()<3)
        return;

    auto packetNo = parts.at(0).toLongLong();
    auto fileId = parts.at(1).toLongLong();
    bool upload = parts.at(2) == "up";

    emit navigateToFileTask(packetNo, fileId, upload);
}

QString RecvTextEdit::timeStr(long long msSinceEpoch)
{
    QDateTime time;
    time.setMSecsSinceEpoch(msSinceEpoch);

    QDateTime now = QDateTime::currentDateTime();
    if (time.date() == now.date())
        return time.toString("HH:mm");
    else if (time.date().year() == now.date().year())
        return time.toString("MM-dd HH:mm");
    else
        return time.toString("yyyy-MM-dd HH:mm");
}

void RecvTextEdit::drawDaySeperatorIfNewDay(long long sinceEpoch)
{
    QDateTime cur;
    cur.setMSecsSinceEpoch(sinceEpoch);

    if (mLastEdit > 0)
    {
        QDateTime last;
        last.setMSecsSinceEpoch(mLastEdit);

        if (last.daysTo(cur)>0)
        {
            moveCursor(QTextCursor::End);
            QString dateStr = cur.toString("yyyy年MM月dd日");
            QString html = QString(
                "<div align=\"center\" style=\"margin: 16px 0 8px 0;\">"
                "<span style=\"color: #BBBBBB; font-size: 11px;\">—— %1 ——</span>"
                "</div>"
            ).arg(dateStr);
            insertHtml(html);
            append("");
        }
    }

    mLastEdit = sinceEpoch;
}
