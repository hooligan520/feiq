#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "fellowlistwidget.h"
#include "searchfellowdlg.h"
#include "feiqlib/feiqengine.h"
#include "filemanagerdlg.h"
#include "settings.h"
#include <unordered_map>
#include <QFileInfo>
#include <QActionGroup>

using namespace std;

namespace Ui {
class MainWindow;
}

class FeiqWin;
class ChatWindow;

struct UnshownMessage
{
    shared_ptr<ViewEvent> event;
    bool replied = false;
    bool read = false;
    long notifyId = 0;

    bool isUnread()
    {
        return !replied && !read;
    }
};

Q_DECLARE_METATYPE(shared_ptr<ViewEvent>)
class MainWindow : public QMainWindow, IFeiqView
{
    Q_OBJECT

    friend class FeiqWin;
    friend class ChatWindow;
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void setFeiqWin(FeiqWin* feiqWin);

    FeiqEngine& getEngine() { return mFeiq; }
    Settings* getSettings() { return mSettings; }
    FileManagerDlg* getFileManagerDlg() { return mDownloadFileDlg; }

public slots:
    void onNotifyClicked(const QString& fellowIp);
    void onNotifyReplied(long notifyId, const QString& fellowIp, const QString& reply);

signals:
    void showErrorAndQuit(const QString& text);
    void statChanged(FileTask* fileTask);
    void progressChanged(FileTask* fileTask);
    void feiqViewEvent(shared_ptr<ViewEvent> event);

private slots:
    void finishSearch(const Fellow* fellow);
    void openSettings();
    void openSearchDlg();
    void openDownloadDlg();
    void onShowErrorAndQuit(const QString& text);
    void openChartTo(const Fellow* fellow);
    void handleFeiqViewEvent(shared_ptr<ViewEvent> event);
    void refreshFellowList();
    void addFellow();
    void onChatWindowClosed(const Fellow* fellow);
    void setMyStatus(AbsenceStatus status);
    void openPlugins();
    void openBroadcast();

private:
    void userAddFellow(QString ip);
    long showNotification(const Fellow* fellow, const QString& text);
    vector<const Fellow*> fellowSearchDriver(const QString& text);
    void initFeiq();
    void setBadgeNumber(int number);
    QString simpleTextOf(const Content* content);

    ChatWindow* findOrCreateChatWindow(const Fellow* fellow);
    ChatWindow* findChatWindow(const Fellow* fellow);
    void updateStatusBar();
    QString getLocalIp();

    UnshownMessage& addUnshownMessage(const Fellow *fellow, shared_ptr<ViewEvent> event);
    UnshownMessage *findUnshownMessage(int id);
    void notifyUnshown(UnshownMessage &umsg);
    void updateUnshownHint(const Fellow* fellow);
    int getUnreadCount();
    void flushUnshown(const Fellow* fellow);

    // IFileTaskObserver interface
public:
    void onStateChanged(FileTask *fileTask);
    void onProgress(FileTask *fileTask);

    // IFeiqView interface
public:
    void onEvent(shared_ptr<ViewEvent> event);

private:
    Ui::MainWindow *ui;
    FellowListWidget mFellowList;
    SearchFellowDlg* mSearchFellowDlg;
    FileManagerDlg* mDownloadFileDlg;
    Settings* mSettings;
    FeiqEngine mFeiq;
    QString mTitle;
    unordered_map<const Fellow*, list<UnshownMessage>> mUnshownEvents;
    FeiqWin* mFeiqWin = nullptr;

    // 聊天窗口管理
    unordered_map<const Fellow*, ChatWindow*> mChatWindows;

    // 本机 Fellow（本机分组展示）
    shared_ptr<Fellow> mSelfFellow;
};

#endif // MAINWINDOW_H
