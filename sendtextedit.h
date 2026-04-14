#ifndef SENDTEXTEDIT_H
#define SENDTEXTEDIT_H

#include <QTextEdit>
#include <QList>
#include <QFileInfo>
#include <QPixmap>

class SendTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    SendTextEdit(QWidget* parent = 0);

signals:
    void acceptDropFiles(QList<QFileInfo>);
    void ctrlEnterPressed();
    void enterPressed();
    void pasteImage(QPixmap pixmap);  // 剪贴板粘贴图片

public slots:
    void newLine();

protected:
    virtual void dragEnterEvent(QDragEnterEvent *e) override;
    virtual void dropEvent(QDropEvent *e) override;
    virtual bool eventFilter(QObject *, QEvent *e) override;

private:
    bool mCtrlDown =false;
};

#endif // SENDTEXTEDIT_H
