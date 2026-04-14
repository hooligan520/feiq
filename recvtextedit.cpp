#include "recvtextedit.h"
#include <QDate>
#include "emoji.h"
#include <QMouseEvent>
#include <QScrollBar>
#include <QFileInfo>
#include <QImage>
#include <QUrl>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QDir>
#include <QRegExp>
#include <QDebug>
#include <QDateTime>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>

RecvTextEdit::RecvTextEdit(QWidget *parent)
    :QTextEdit(parent)
{
    setTextInteractionFlags(Qt::LinksAccessibleByMouse|Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
}

void RecvTextEdit::setMyInfo(const QString& name, const QString& ip)
{
    mMyName = name;
    mMyIp = ip;
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

void RecvTextEdit::scrollContentsBy(int dx, int dy)
{
    QTextEdit::scrollContentsBy(dx, dy);

    // 检查是否滚动到了顶部
    if (!mLoadingHistory && dy > 0) // dy > 0 表示向上滚动
    {
        auto *sb = verticalScrollBar();
        if (sb && sb->value() <= 0)
        {
            emit scrolledToTop();
        }
    }
}

void RecvTextEdit::addFellowContent(const Content *content, long long msSinceEpoch)
{
    addContent(content, msSinceEpoch, false);
}

void RecvTextEdit::addMyContent(const Content *content, long long msSinceEpoch)
{
    addContent(content, msSinceEpoch, true);
}

void RecvTextEdit::prependFellowContent(const Content *content, long long msSinceEpoch)
{
    // TODO: 在顶部插入内容 - 目前先用同样的 addContent
    // 实际滚动加载在 ChatWindow 中通过重新渲染所有历史来实现
    addContent(content, msSinceEpoch, false);
}

void RecvTextEdit::prependMyContent(const Content *content, long long msSinceEpoch)
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

    // 用 QTextCursor + QTextBlockFormat 来实现居中对齐（比 HTML table 可靠）
    moveCursor(QTextCursor::End);
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);

    // 插入新段落
    cursor.insertBlock();

    // 设置居中对齐
    QTextBlockFormat blockFmt;
    blockFmt.setAlignment(Qt::AlignCenter);
    blockFmt.setTopMargin(8);
    blockFmt.setBottomMargin(4);
    cursor.setBlockFormat(blockFmt);

    // 插入时间文本
    QTextCharFormat charFmt;
    charFmt.setForeground(QColor("#B0B0B0"));
    charFmt.setFontPointSize(11);
    cursor.insertText(friendlyTimeStr(msSinceEpoch), charFmt);

    setTextCursor(cursor);
}

void RecvTextEdit::showBubble(const Content *content, long long msSinceEpoch, bool mySelf)
{
    Q_UNUSED(msSinceEpoch);

    QString name;
    if (mySelf)
    {
        name = mMyName.isEmpty() ? "我" : mMyName;
    }
    else
    {
        name = mFellow == nullptr ? "匿名" : QString::fromStdString(mFellow->getName());
    }

    QString contentHtml = contentToHtml(content, mySelf);

    // 确保头像资源已注册
    QString avatarUrl = ensureAvatarResource(mySelf);

    moveCursor(QTextCursor::End);
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);

    if (mySelf)
    {
        // === 自己的消息：右对齐 ===
        // 第一行：名字 + 头像（右对齐）
        cursor.insertBlock();
        QTextBlockFormat rightFmt;
        rightFmt.setAlignment(Qt::AlignRight);
        rightFmt.setTopMargin(8);
        rightFmt.setBottomMargin(0);
        rightFmt.setRightMargin(16); // 留出滚动条空间
        cursor.setBlockFormat(rightFmt);

        QString nameHtml = QString(
            "<span style=\"color: #999999; font-size: 12px;\">%1</span>"
            " <img src=\"%2\" width=\"36\" height=\"36\" />"
        ).arg(name, avatarUrl);
        cursor.insertHtml(nameHtml);

        // 第二行：气泡内容（右对齐 + 蓝色气泡 span 包裹）
        cursor.insertBlock();
        QTextBlockFormat bubbleFmt;
        bubbleFmt.setAlignment(Qt::AlignRight);
        bubbleFmt.setTopMargin(2);
        bubbleFmt.setBottomMargin(4);
        bubbleFmt.setRightMargin(16); // 留出滚动条空间
        cursor.setBlockFormat(bubbleFmt);

        // 用 span 背景包裹内容，紧凑贴合文字
        QString bubbleHtml = QString(
            "<span style=\"background-color: #0099FF; color: #FFFFFF; "
            "padding: 8px 12px; border-radius: 8px; font-size: 13px; "
            "display: inline-block;\">"
            "%1</span>"
        ).arg(contentHtml);
        cursor.insertHtml(bubbleHtml);
    }
    else
    {
        // === 对方消息：左对齐 ===
        // 第一行：头像 + 名字
        cursor.insertBlock();
        QTextBlockFormat leftFmt;
        leftFmt.setAlignment(Qt::AlignLeft);
        leftFmt.setTopMargin(8);
        leftFmt.setBottomMargin(0);
        cursor.setBlockFormat(leftFmt);

        QString nameHtml = QString(
            "<img src=\"%1\" width=\"36\" height=\"36\" />"
            " <span style=\"color: #999999; font-size: 12px;\">%2</span>"
        ).arg(avatarUrl, name);
        cursor.insertHtml(nameHtml);

        // 第二行：气泡内容（左对齐 + 白色气泡 span 包裹，缩进到头像右侧）
        cursor.insertBlock();
        QTextBlockFormat bubbleFmt;
        bubbleFmt.setAlignment(Qt::AlignLeft);
        bubbleFmt.setTopMargin(2);
        bubbleFmt.setBottomMargin(4);
        bubbleFmt.setLeftMargin(44); // 头像36px + 间距8px
        cursor.setBlockFormat(bubbleFmt);

        // 用 span 背景包裹内容，紧凑贴合文字
        QString bubbleHtml = QString(
            "<span style=\"background-color: #FFFFFF; color: #333333; "
            "padding: 8px 12px; border-radius: 8px; font-size: 13px; "
            "display: inline-block;\">"
            "%1</span>"
        ).arg(contentHtml);
        cursor.insertHtml(bubbleHtml);
    }

    setTextCursor(cursor);
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
            "<div style=\"background-color: #F5F5F5; border: 1px solid #E8E8E8; "
            "border-radius: 8px; padding: 8px 12px; margin: 4px 0;\">"
            "📄 <a href=\"%1\" style=\"color: #576B95; text-decoration: none;\">%2</a>"
            "<br/><span style=\"color: #B0B0B0; font-size: 11px;\">%3</span>"
            "</div>"
        ).arg(link, QString::fromStdString(content->filename), sizeStr);
    }
    else if (content->fileType == IPMSG_FILE_DIR)
    {
        // 文件夹卡片样式
        QString link = QString("%1_%2_%3")
            .arg(content->packetNo)
            .arg(content->fileId)
            .arg(fromMySelf ? "up" : "down");

        return QString(
            "<div style=\"background-color: #FFF8E1; border: 1px solid #FFE082; "
            "border-radius: 8px; padding: 8px 12px; margin: 4px 0;\">"
            "📁 <a href=\"%1\" style=\"color: #576B95; text-decoration: none;\">%2</a>"
            "<br/><span style=\"color: #B0B0B0; font-size: 11px;\">文件夹</span>"
            "</div>"
        ).arg(link, QString::fromStdString(content->filename));
    }
    else
    {
        return unsupportToHtml("对方发来非普通文件（可能是文件夹），收不来……");
    }
}

QString RecvTextEdit::imageToHtml(const ImageContent *content)
{
    // 检查是否有本地图片文件可以显示
    if (!content->localPath.empty())
    {
        QString filePath = QString::fromStdString(content->localPath);
        QFileInfo fi(filePath);
        if (fi.exists() && fi.size() > 0)
        {
            QImage image(filePath);
            if (!image.isNull())
            {
                int maxWidth = 240;
                int maxHeight = 240;
                if (image.width() > maxWidth || image.height() > maxHeight)
                    image = image.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

                QUrl imgUrl = QUrl::fromLocalFile(filePath);
                document()->addResource(QTextDocument::ImageResource, imgUrl, image);

                return QString("<img src=\"%1\" width=\"%2\" height=\"%3\" />")
                    .arg(imgUrl.toString()).arg(image.width()).arg(image.height());
            }
        }
        // 文件存在但无法读取
        return "<span style=\"color: #B0B0B0;\">🖼 图片文件已丢失</span>";
    }

    // 没有本地路径，显示下载中
    return "<span style=\"color: #B0B0B0;\">🖼 图片下载中...</span>";
}

QString RecvTextEdit::textToHtml(const TextContent *content)
{
    auto str = QString::fromStdString(content->text);

    // 先提取内联图片标记 \x01IMG:imageId\x01，避免被 toHtmlEscaped 破坏
    // 用临时占位符替换，HTML 转义后再还原为 <img> 标签
    QStringList imageIds;
    QRegExp imgRx("\\x01IMG:([0-9a-fA-F]+)\\x01");
    int offset = 0;
    while ((offset = imgRx.indexIn(str, offset)) != -1)
    {
        QString imageId = imgRx.cap(1);
        QString placeholder = QString("__FEIQ_IMG_%1__").arg(imageIds.size());
        imageIds.append(imageId);
        str.replace(offset, imgRx.matchedLength(), placeholder);
        offset += placeholder.length();
    }

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

    // 还原图片占位符为 <img> 标签
    QString imgDir = QDir::homePath() + "/.feiq/images";
    for (int i = 0; i < imageIds.size(); i++)
    {
        QString placeholder = QString("__FEIQ_IMG_%1__").arg(i);
        QString imageId = imageIds[i];

        // 查找 *_imageId.jpg（取最新的一个），兼容旧格式 imageId.jpg
        QDir dir(imgDir);
        QStringList filters;
        filters << ("*_" + imageId + ".jpg");
        auto matches = dir.entryList(filters, QDir::Files, QDir::Time); // Time=最新在前
        // 兼容旧格式
        if (matches.isEmpty()) filters << (imageId + ".jpg");
        matches = dir.entryList(filters, QDir::Files, QDir::Time);
        QString filePath = matches.isEmpty() ? "" : (imgDir + "/" + matches.first());

        QString imgHtml;
        if (!filePath.isEmpty()) {
            QImage image(filePath);
            if (!image.isNull())
            {
                int maxWidth = 240;
                int maxHeight = 240;
                if (image.width() > maxWidth || image.height() > maxHeight)
                    image = image.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

                QUrl imgUrl = QUrl::fromLocalFile(filePath);
                document()->addResource(QTextDocument::ImageResource, imgUrl, image);

                imgHtml = QString("<img src=\"%1\" width=\"%2\" height=\"%3\" />")
                    .arg(imgUrl.toString()).arg(image.width()).arg(image.height());
            }
            else
            {
                imgHtml = "<span style=\"color: #B0B0B0;\">🖼 图片加载失败</span>";
            }
        }
        else
        {
            // 图片文件尚未下载完成，显示占位提示
            imgHtml = QString("<span style=\"color: #B0B0B0;\">🖼 图片加载中... (%1)</span>").arg(imageId);
        }

        htmlStr.replace(placeholder, imgHtml);
    }

    return htmlStr;
}

QString RecvTextEdit::knockToHtml(const KnockContent *content, bool mySelf)
{
    Q_UNUSED(content);
    if (mySelf)
        return "<span style=\"color: #B0B0B0;\">👋 发送了一个窗口抖动</span>";
    else
        return "<span style=\"color: #B0B0B0;\">👋 发来窗口抖动</span>";
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
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertBlock();

    QTextBlockFormat blockFmt;
    blockFmt.setAlignment(Qt::AlignCenter);
    blockFmt.setTopMargin(6);
    blockFmt.setBottomMargin(6);
    cursor.setBlockFormat(blockFmt);

    QTextCharFormat charFmt;
    charFmt.setForeground(QColor("#B0B0B0"));
    charFmt.setFontPointSize(10);
    cursor.insertText(warning, charFmt);

    setTextCursor(cursor);
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

QString RecvTextEdit::friendlyTimeStr(long long msSinceEpoch)
{
    QDateTime time;
    time.setMSecsSinceEpoch(msSinceEpoch);

    QDateTime now = QDateTime::currentDateTime();
    QDate today = now.date();
    QDate msgDate = time.date();

    if (msgDate == today)
    {
        // 当天：只显示时间
        return time.toString("HH:mm");
    }

    QDate yesterday = today.addDays(-1);
    if (msgDate == yesterday)
    {
        // 昨天
        return "昨天 " + time.toString("HH:mm");
    }

    // 一周内（7天内）
    int daysAgo = msgDate.daysTo(today);
    if (daysAgo >= 2 && daysAgo <= 6)
    {
        static const char* weekdays[] = {
            "星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"
        };
        int dayOfWeek = msgDate.dayOfWeek(); // 1=Monday, 7=Sunday
        return QString::fromUtf8(weekdays[dayOfWeek - 1]) + " " + time.toString("HH:mm");
    }

    // 更早：显示日期 + 时间
    if (msgDate.year() == today.year())
        return time.toString("M月d日 HH:mm");
    else
        return time.toString("yyyy年M月d日 HH:mm");
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
            QTextCursor cursor = textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.insertBlock();

            QTextBlockFormat blockFmt;
            blockFmt.setAlignment(Qt::AlignCenter);
            blockFmt.setTopMargin(12);
            blockFmt.setBottomMargin(6);
            cursor.setBlockFormat(blockFmt);

            QTextCharFormat charFmt;
            charFmt.setForeground(QColor("#C0C0C0"));
            charFmt.setFontPointSize(11);
            QString dateStr = cur.toString("yyyy年MM月dd日");
            cursor.insertText("—— " + dateStr + " ——", charFmt);

            setTextCursor(cursor);
        }
    }

    mLastEdit = sinceEpoch;
}

// === 头像生成 ===

QImage RecvTextEdit::generateAvatar(const QString& text, const QColor& bgColor, int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // 画圆角矩形背景（QQ 风格）
    QPainterPath path;
    path.addRoundedRect(0, 0, size, size, 6, 6);
    painter.fillPath(path, bgColor);

    // 画文字
    painter.setPen(Qt::white);
    QFont font;
    font.setPixelSize(size * 0.45);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, text);

    painter.end();
    return image;
}

QString RecvTextEdit::getAvatarText(const QString& name)
{
    if (name.isEmpty())
        return "?";
    return name.left(1).toUpper();
}

QColor RecvTextEdit::getAvatarColor(const QString& ip)
{
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

    auto parts = ip.split('.');
    int lastOctet = 0;
    if (parts.size() == 4)
        lastOctet = parts.last().toInt();

    return colors[lastOctet % 10];
}

QString RecvTextEdit::ensureAvatarResource(bool mySelf)
{
    QString avatarKey;
    QString name;
    QString ip;

    if (mySelf)
    {
        avatarKey = "avatar://my";
        name = mMyName.isEmpty() ? "我" : mMyName;
        ip = mMyIp;
    }
    else
    {
        QString fellowIp = mFellow ? QString::fromStdString(mFellow->getIp()) : "0.0.0.0";
        avatarKey = "avatar://" + fellowIp;
        name = mFellow ? QString::fromStdString(mFellow->getName()) : "匿名";
        ip = fellowIp;
    }

    QUrl url(avatarKey);

    // 检查是否已经注册过
    QVariant existing = document()->resource(QTextDocument::ImageResource, url);
    if (existing.isNull())
    {
        // 生成头像并注册
        QString text = getAvatarText(name);
        QColor color = getAvatarColor(ip);
        QImage avatar = generateAvatar(text, color, 36);
        document()->addResource(QTextDocument::ImageResource, url, avatar);
    }

    return avatarKey;
}
