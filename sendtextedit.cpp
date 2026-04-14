#include "sendtextedit.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QDebug>
#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QPixmap>

SendTextEdit::SendTextEdit(QWidget *parent)
    :QTextEdit(parent)
{
    setAcceptDrops(true);
    installEventFilter(this);
}

void SendTextEdit::newLine()
{
    append("");
}

void SendTextEdit::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
    {
        auto urls = e->mimeData()->urls();
        for (auto url : urls)
        {
            if (QFileInfo(url.toLocalFile()).isFile())
            {
                e->accept();
                return;
            }
        }
    }
    else
    {
        QTextEdit::dragEnterEvent(e);
    }
}

void SendTextEdit::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasUrls())
    {
        auto urls = e->mimeData()->urls();
        QList<QFileInfo> files;
        for (auto url : urls)
        {
            files.append(QFileInfo(url.toLocalFile()));
            e->accept();
        }
        emit acceptDropFiles(files);
    }   
    else
    {
        QTextEdit::dropEvent(e);
    }
}

bool SendTextEdit::eventFilter(QObject *, QEvent * e)
{
    if (e->type() == QEvent::KeyPress)
    {
        auto keyEvent = static_cast<QKeyEvent*>(e);
        auto enter = keyEvent->key() == Qt::Key_Return;
        auto ctrl  = keyEvent->modifiers() == Qt::ControlModifier;

        // Cmd+V（macOS 上 ControlModifier = Command）且剪贴板有图片
        if (keyEvent->key() == Qt::Key_V && ctrl)
        {
            auto *clipboard = QApplication::clipboard();
            auto *mime = clipboard->mimeData();
            if (mime && mime->hasImage())
            {
                QPixmap pix = qvariant_cast<QPixmap>(mime->imageData());
                if (!pix.isNull())
                {
                    emit pasteImage(pix);
                    return true;  // 拦截，不让 QTextEdit 把图片插入文本框
                }
            }
            // 没有图片，走默认粘贴（粘贴文字）
            return false;
        }

        if (enter && ctrl)
        {
            emit ctrlEnterPressed();
            return true;
        }
        else if (enter)
        {
            emit enterPressed();
            return true;
        }
    }
    return false;
}
