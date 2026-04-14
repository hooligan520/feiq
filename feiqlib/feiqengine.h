#ifndef FEIQENGINE_H
#define FEIQENGINE_H

#include "content.h"
#include "feiqcommu.h"
#include <string>
#include <tuple>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include "feiqmodel.h"
#include "msgqueuethread.h"
#include "ifeiqview.h"
#include "asynwait.h"
#include "history.h"
using namespace std;

class Post;
class ContentSender;

/**
 * @brief The FeiqEngine class
 * feiq以mvc模式架构，FeiqEngine是control部分，负责逻辑控制（以及具体协议来往）
 */
class FeiqEngine
{
public:
    FeiqEngine();

public:
    pair<bool, string> send(shared_ptr<Fellow> fellow, shared_ptr<Content> content);
    pair<bool, string> sendFiles(shared_ptr<Fellow> fellow, list<shared_ptr<FileContent> > &files);
    bool downloadFile(FileTask* task);
    bool downloadDir(FileTask* task);

public:
    pair<bool, string> start();
    void stop();
    void addToBroadcast(const string& ip);
    void setMyHost(string host);
    void setMyName(string name);
    void setView(IFeiqView* view){mView = view;}
    void sendImOnLine(const string& ip = "");
    void sendAbsence(AbsenceStatus status);
    AbsenceStatus myAbsenceStatus() const { return mMyAbsenceStatus; }
    void setAutoReply(bool enable, const string& text);
    /**
     * @brief enableIntervalDetect 当接入路由，被禁止发送广播包时，
     * 启用间隔检测可每隔一段时间发送一次上线通知到指定网段，以实现检测。
     */
    void enableIntervalDetect(int seconds);

public:
    FeiqModel &getModel();
    FeiqCommu &getCommu() { return mCommu; }
    History &getHistory() { return mHistory; }
    void initHistory(const string& dbPath);

private://trigers
    void onAnsEntry(shared_ptr<Post> post);
    void onBrEntry(shared_ptr<Post> post);
    void onBrExit(shared_ptr<Post> post);
    void onBrAbsence(shared_ptr<Post> post);
    void onMsg(shared_ptr<Post> post);
    void onSendCheck(shared_ptr<Post> post);
    void onReadCheck(shared_ptr<Post> post);
    void onSendTimeo(IdType packetId, const string &ip, shared_ptr<Content> content);
    void onReadMessage(shared_ptr<Post> post);

private:
    void fileServerHandler(unique_ptr<TcpSocket> client, int cmdId, int packetNo, int fileId, int offset);

private:
    shared_ptr<Fellow> addOrUpdateFellow(shared_ptr<Fellow> fellow);
    void dispatchMsg(shared_ptr<ViewEvent> msg);
    void broadcastToCurstomGroup(SendProtocol& protocol);

private:
    FeiqCommu mCommu;
    vector<unique_ptr<RecvProtocol>> mRecvProtocols;
    FeiqModel mModel;
    History mHistory;
    string mHost;
    string mName;
    MsgQueueThread<ViewEvent> mMsgThd;
    IFeiqView* mView;
    vector<string> mBroadcast;
    bool mStarted=false;
    AbsenceStatus mMyAbsenceStatus = AbsenceStatus::Online;
    bool mAutoReplyEnabled = false;
    string mAutoReplyText;
    AsynWait mAsyncWait;//异步等待对方回包

    // 图片分片缓存（UDP 内联图片协议）
    struct ImageChunkInfo {
        string imageId;           // 图片 ID（十六进制字符串）
        size_t totalSize = 0;     // 图片总大小（十进制）
        vector<char> data;        // 已收集的图片数据
        shared_ptr<Fellow> from;  // 发送者
        long long lastChunkTime = 0; // 最后一个 chunk 的时间戳（ms）
    };
    mutex mImageChunkMutex;
    unordered_map<string, ImageChunkInfo> mImageChunks; // key = imageId
    unordered_set<string> mCompletedImageIds; // 已完成的 imageId，忽略重传
public:
    // 处理图片分片（由 RecvImage 调用）
    void handleImageChunk(const string& imageId, size_t totalSize, int width, int height,
                         const char* chunkData, size_t chunkLen, shared_ptr<Fellow> from);
private:
    void saveAndNotifyImage(const ImageChunkInfo& info);
    void checkImageChunkTimeout(); // 定时检查超时未完成的图片

    struct EnumClassHash
    {
        template <typename T>
        std::size_t operator()(T t) const
        {
            return static_cast<std::size_t>(t);
        }
    };
    //可以用unique_ptr，但是unique_ptr要求知道具体定义
    unordered_map<ContentType, shared_ptr<ContentSender>, EnumClassHash> mContentSender;
};

#endif // FEIQENGINE_H
