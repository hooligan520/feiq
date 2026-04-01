#ifndef RECVTEXTEDIT_H
#define RECVTEXTEDIT_H

#include "feiqlib/content.h"
#include "feiqlib/fellow.h"
#include <QObject>
#include <unordered_map>
#include <QTextEdit>

using namespace std;

class RecvTextEdit: public QTextEdit
{
    Q_OBJECT
public:
    RecvTextEdit(QWidget* parent = 0);

protected:
    virtual void mousePressEvent(QMouseEvent *e) override;
    virtual void mouseReleaseEvent(QMouseEvent *e) override;

public:
    void addFellowContent(const Content* content, long long msSinceEpoch);
    void addMyContent(const Content* content, long long msSinceEpoch);
    void setCurFellow(const Fellow* fellow);
    void addWarning(const QString& warning);
    const Fellow* curFellow();

signals:
    void navigateToFileTask(IdType packetNo, IdType fileId, bool upload);

private:
    QString timeStr(long long msSinceEpoch);
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

private:
    const Fellow* mFellow = nullptr;
    unordered_map<const Fellow*, QTextDocument*> mDocs;
    long long mLastEdit=0;
    QString mPressedAnchor;

};

#endif // RECVTEXTEDIT_H
