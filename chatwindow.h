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
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QSet>
#include <QTimer>
#include <QMenu>
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
    void sendImage();
    void captureScreen();
    void openChooseEmojiDlg();
    void navigateToFileTask(IdType packetNo, IdType fileId, bool upload);
    void loadMoreHistory();

private:
    void setupUI();
    void loadHistory();
    void shakeWindow();
    void showResult(pair<bool, string> ret, const Content* content);
    shared_ptr<Fellow> checkCurFellow();
    void readEvent(const ViewEvent* event);
    QString simpleTextOf(const Content* content);
    void downloadAndShowImage(shared_ptr<ImageContent> ic, shared_ptr<Fellow> fellow);
    void showInlineImage(const QString& filePath);
    void displayContentWithRetry(shared_ptr<Content> content, long long time,
                                  bool isFellow, int retryCount);
    void showQuickReplyMenu();
    void setScreenshotPreview(const QString& filePath);
    void clearScreenshotPreview();

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
    QSet<QString> mInlineImageIds; // 文本消息中引用的内联图片 imageId

    // 截图预览
    QWidget* mScreenshotPreviewBar = nullptr;  // 预览条（图片缩略图 + 关闭按钮）
    QString  mPendingScreenshotPath;           // 待发送的截图路径
    QVBoxLayout* mInputOuterLayout = nullptr;  // 包含 previewBar + inputContainer 的外层布局

    // 历史消息加载
    int mHistoryLoadedCount = 0;  // 已加载的历史消息数
    bool mHasMoreHistory = true;  // 是否还有更多历史
    static const int HISTORY_PAGE_SIZE = 40;
};

#endif // CHATWINDOW_H
