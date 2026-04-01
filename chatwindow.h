#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>
#include <QAction>
#include <QSplitter>
#include <QCloseEvent>
#include <memory>
#include "recvtextedit.h"
#include "sendtextedit.h"
#include "chooseemojidlg.h"
#include "feiqlib/feiqengine.h"
#include "feiqlib/ifeiqview.h"
#include "settings.h"

using namespace std;

class MainWindow;

class ChatWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWindow(const Fellow* fellow, FeiqEngine* engine, Settings* settings, MainWindow* mainWin, QWidget *parent = 0);
    ~ChatWindow();

    const Fellow* fellow() const { return mFellow; }
    RecvTextEdit* recvTextEdit() { return mRecvTextEdit; }
    SendTextEdit* sendTextEdit() { return mSendTextEdit; }

    void handleViewEvent(shared_ptr<ViewEvent> event);
    void bringToFront();

public slots:
    void sendFile();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

signals:
    void windowClosed(const Fellow* fellow);

private slots:
    void sendText();
    void sendFile(string filepath);
    void sendFiles(QList<QFileInfo> files);
    void sendKnock();
    void openChooseEmojiDlg();
    void navigateToFileTask(IdType packetNo, IdType fileId, bool upload);

private:
    void setupUI();
    void loadHistory();
    void showResult(pair<bool, string> ret, const Content* content);
    shared_ptr<Fellow> checkCurFellow();
    void readEvent(const ViewEvent* event);
    QString simpleTextOf(const Content* content);

private:
    const Fellow* mFellow;
    FeiqEngine* mEngine;
    Settings* mSettings;
    MainWindow* mMainWin;

    RecvTextEdit* mRecvTextEdit;
    SendTextEdit* mSendTextEdit;
    ChooseEmojiDlg* mChooseEmojiDlg;

    QToolBar* mToolBar;
    QPushButton* mSendBtn;
    QLabel* mTitleLabel;
};

#endif // CHATWINDOW_H
