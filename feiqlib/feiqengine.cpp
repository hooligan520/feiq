#include "feiqengine.h"
#include "protocol.h"
#include "ipmsg.h"
#include <memory>
#include "utils.h"
#include <fstream>
#include "defer.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <sys/stat.h>
#include <array>
#include <dirent.h>
#include <functional>
#include <sstream>
#include "logger.h"

class ContentSender : public SendProtocol
{
public:
    void setContent(const Content* content)
    {
        mContent = content;
    }

protected:
    const Content* mContent;
};

class SendTextContent : public ContentSender
{
public:
    int cmdId() override{return IPMSG_SENDMSG|IPMSG_SENDCHECKOPT;}
    void write(ostream& os) override
    {
        auto content = static_cast<const TextContent*>(mContent);
        if (content->format.empty())
        {
            os<<encOut->convert(content->text);
        }
        else
        {
            os<<encOut->convert(content->text)
             <<"{"
            <<encOut->convert(content->format)
            <<"}";
        }
    }
};

class SendKnockContent : public ContentSender
{
public:
    int cmdId() override{return IPMSG_KNOCK;}
    void write(ostream &os) override {(void)os;}
};

class SendAutoReplyContent : public ContentSender
{
public:
    SendAutoReplyContent(const string& text):mText(text){}
    int cmdId() override{return IPMSG_SENDMSG|IPMSG_SENDCHECKOPT|IPMSG_AUTORETOPT;}
    void write(ostream& os) override
    {
        os<<encOut->convert(mText);
    }
private:
    string mText;
};

class SendFileContent : public ContentSender
{
public:
    int cmdId() override {return IPMSG_SENDMSG|IPMSG_FILEATTACHOPT;}
    void write(ostream& os) override
    {
        auto content = static_cast<const FileContent*>(mContent);
        char sep = HLIST_ENTRY_SEPARATOR;
        auto filename = content->filename;
        stringReplace(filename, ":", "::");//估摸着协议不会变，偷懒下
        os<<(char)0
          <<to_string(content->fileId)
         <<sep
        <<encOut->convert(filename)
        <<sep
        <<std::hex<<content->size
        <<sep
        <<content->modifyTime
        <<sep
        <<content->fileType
        <<sep
        <<FILELIST_SEPARATOR;
    }
};

class SendImOnLine : public SendProtocol
{
public:
    SendImOnLine(const string& name):mName(name){}
    int cmdId() override{return IPMSG_BR_ENTRY;}
    void write(ostream &os) override
    {
        os<<encOut->convert(mName);
    }

private:
    string mName;
};

class SendImOffLine : public SendProtocol
{
public:
    SendImOffLine(const string& name):mName(name){}
    int cmdId() override {return IPMSG_BR_EXIT;}
    void write(ostream &os) override
    {
        os<<encOut->convert(mName);
    }
private:
    string mName;
};

class SendAbsence : public SendProtocol
{
public:
    SendAbsence(const string& name, AbsenceStatus status):mName(name), mStatus(status){}
    int cmdId() override {
        int cmd = IPMSG_BR_ABSENCE;
        if (mStatus != AbsenceStatus::Online)
            cmd |= IPMSG_ABSENCEOPT;
        return cmd;
    }
    void write(ostream &os) override
    {
        os<<encOut->convert(mName);
    }
private:
    string mName;
    AbsenceStatus mStatus;
};

/**
 * @brief The AnsSendCheck class 发送消息我收到了
 */
class SendSentCheck : public SendProtocol
{
public:
    SendSentCheck(const string& packetNo)
        :mPacketNo(packetNo){}

    int cmdId() override{return IPMSG_RECVMSG;}

    void write(ostream& os) override
    {
        os<<mPacketNo;
    }
private:
    string mPacketNo;
};

/**
 * @brief The SendReadCheck class 发送消息我已经读了
 */
class SendReadCheck : public SendProtocol
{
public:
    SendReadCheck(const string& packetNo)
        :mPacketNo(packetNo){}
public:
    int cmdId() override {return IPMSG_READMSG;}
    void write(ostream& os) override
    {
        os<<mPacketNo;
    }
private:
    string mPacketNo;
};

/**
 * @brief The AnsBrEntry class 回复好友上线包
 */
class AnsBrEntry : public SendProtocol
{
public:
    AnsBrEntry(const string& myName):mName(myName){}
public:
    int cmdId() override { return IPMSG_ANSENTRY;}
    void write(ostream &os) override {
        os<<encOut->convert(mName);
    }
private:
    const string& mName;
};

//定义触发器
typedef std::function<void (shared_ptr<Post> post)> OnPostReady;
#define DECLARE_TRIGGER(name)\
    public:\
    name(OnPostReady trigger) : mTrigger(trigger){}\
    private:\
    OnPostReady mTrigger;\
    void trigger(shared_ptr<Post> post){mTrigger(post);}

/**
 * @brief The RecvAnsEntry class 好友响应我们的上线消息
 */
class RecvAnsEntry : public RecvProtocol
{
    DECLARE_TRIGGER(RecvAnsEntry)
public:
    bool read(shared_ptr<Post> post)
    {
        if (IS_CMD_SET(post->cmdId, IPMSG_ANSENTRY))
        {
            auto converted = toString(encIn->convert(post->extra));
            post->from->setName(converted);
            post->from->setOnLine(true);
            trigger(post);
            return true;
        }
        return false;
    }
};
/**
 * @brief The RecvBrEntry class 好友上线
 */
class RecvBrEntry : public RecvProtocol
{
    DECLARE_TRIGGER(RecvBrEntry)
public:
    bool read(shared_ptr<Post> post)
    {
        if (IS_CMD_SET(post->cmdId, IPMSG_BR_ENTRY))
        {
            post->from->setName(toString(encIn->convert(post->extra)));
            post->from->setOnLine(true);
            trigger(post);
            return true;
        }
        return false;
    }
};
/**
 * @brief The RecvBrExit class 好友下线
 */
class RecvBrExit : public RecvProtocol
{
    DECLARE_TRIGGER(RecvBrExit)
public:
    bool read(shared_ptr<Post> post)
    {
        if (IS_CMD_SET(post->cmdId, IPMSG_BR_EXIT))
        {
            post->from->setOnLine(false);
            trigger(post);
            return true;
        }
        return false;
    }
};

/**
 * @brief The RecvBrAbsence class 好友状态变更（离开/忙碌/恢复）
 */
class RecvBrAbsence : public RecvProtocol
{
    DECLARE_TRIGGER(RecvBrAbsence)
public:
    bool read(shared_ptr<Post> post)
    {
        if (IS_CMD_SET(post->cmdId, IPMSG_BR_ABSENCE))
        {
            post->from->setName(toString(encIn->convert(post->extra)));
            if (IS_OPT_SET(post->cmdId, IPMSG_ABSENCEOPT))
            {
                // 有 ABSENCEOPT 标志表示离线状态（离开或忙碌）
                // 飞秋扩展中离开和忙碌通过附加内容区分
                post->from->setAbsenceStatus(AbsenceStatus::Away);
            }
            else
            {
                // 没有 ABSENCEOPT 标志表示恢复在线
                post->from->setAbsenceStatus(AbsenceStatus::Online);
            }
            post->from->setOnLine(true);
            trigger(post);
            return true;
        }
        return false;
    }
};
/**
 * @brief The RecvKnock class  窗口抖动
 */
class RecvKnock : public RecvProtocol
{
public:
    bool read(shared_ptr<Post> post)
    {
        if (IS_CMD_SET(post->cmdId, IPMSG_KNOCK))
        {
            post->contents.push_back(make_shared<KnockContent>());
        }
        return false;
    }
};
/**
 * @brief The AnsSendCheck class
 */
class RecvSendCheck : public RecvProtocol
{
    DECLARE_TRIGGER(RecvSendCheck)
public:
    bool read(shared_ptr<Post> post)
    {
        if (IS_OPT_SET(post->cmdId, IPMSG_SENDCHECKOPT))
            trigger(post);
        return false;
    }
};

/**
 * @brief The RecvReadCheck class 接收到请求阅后通知
 */
class RecvReadCheck : public RecvProtocol
{
    DECLARE_TRIGGER(RecvReadCheck)
public:
    bool read(shared_ptr<Post> post)
    {
        if (IS_OPT_SET(post->cmdId, IPMSG_READCHECKOPT))
            trigger(post);
        return false;
    }
};

/**
 * @brief The RecvText class 接收文本消息（同时支持含内联图片的文本 0x78）
 */
class RecvText : public RecvProtocol
{
public:
    bool read(shared_ptr<Post> post)
    {
        if (!IS_CMD_SET(post->cmdId, IPMSG_SENDMSG))
            return false;

        auto& extra = post->extra;

        auto end = extra.end();
        auto begin = extra.begin();
        auto found = std::find(begin, end, 0);
        if (found != begin)//有找到0，且不是第一个字符
        {
            string rawText;
            rawText.assign(begin, found);
            string converted = encIn->convert(rawText);

            // 检测文本中是否包含图片占位符 /~#>imageId<B~
            // 将占位符替换为 \x01IMG:imageId\x01 标记，保持为单个 TextContent
            string processed = replaceImgPlaceholders(converted);
            auto content = createTextContent(processed);
            post->contents.push_back(shared_ptr<Content>(std::move(content)));
        }

        return false;
    }
private:
    // 将图片占位符 /~#>imageId<B~ 替换为特殊标记 \x01IMG:imageId\x01
    // 保持消息为单个字符串，在 UI 层渲染时再转为 <img> 标签
    string replaceImgPlaceholders(const string& text)
    {
        string result;
        size_t pos = 0;
        size_t len = text.size();

        while (pos < len)
        {
            size_t imgStart = text.find("/~#>", pos);
            if (imgStart == string::npos)
            {
                result += text.substr(pos);
                break;
            }

            // 占位符前面的文字
            result += text.substr(pos, imgStart - pos);

            // 解析 imageId
            size_t idStart = imgStart + 4;
            size_t imgEnd = text.find("<B~", idStart);
            if (imgEnd == string::npos)
            {
                // 格式不完整，保留原文
                result += text.substr(imgStart);
                break;
            }

            string imageId = text.substr(idStart, imgEnd - idStart);
            pos = imgEnd + 3;

            // 替换为特殊标记
            result += "\x01IMG:" + imageId + "\x01";

            FEIQ_LOG("[RecvText] 图片占位符替换: imageId=" << imageId);
        }

        return result;
    }

    unique_ptr<TextContent> createTextContent(const string& raw)
    {
        auto content = unique_ptr<TextContent>(new TextContent());
        auto begin = raw.find('{');
        auto end = raw.find("}", begin+1);

        if (begin != raw.npos && end != raw.npos)
        {
            content->text = raw.substr(0, begin);
            content->format = raw.substr(begin+1, end-begin-1);
        }
        else
        {
            content->text = raw;
        }
        return content;
    }
};

class RecvFile : public RecvProtocol
{
public:
    bool read(shared_ptr<Post> post)
    {
        if (!IS_OPT_SET(post->cmdId, IPMSG_FILEATTACHOPT) || !IS_CMD_SET(post->cmdId, IPMSG_SENDMSG))
            return false;

        //文件任务信息紧随文本消息之后，中间相隔一个ascii 0
        //一个文件任务信息格式为fileId:filename:fileSize:modifyTime:fileType:其他扩展属性
        //多个文件任务以ascii 7分割
        //文件名含:，以::表示
        auto& extra = post->extra;
        auto end = extra.end();
        auto found = find(extra.begin(), end, 0)+1;

        while (found != end)
        {
            auto endTask = find(found, end, FILELIST_SEPARATOR);
            if (endTask == end)
                break;

            auto content = createFileContent(found, endTask);
            if (content != nullptr)
            {
                content->packetNo = stoul(post->packetNo);
                post->contents.push_back(shared_ptr<Content>(std::move(content)));
            }

            found = ++endTask;
        }

        return false;
    }
private:
    unique_ptr<FileContent> createFileContent(vector<char>::iterator from,
                                          vector<char>::iterator to)
    {
        unique_ptr<FileContent> content(new FileContent());

        auto values = splitAllowSeperator(from, to, HLIST_ENTRY_SEPARATOR);
        const int fieldCount = 5;
        if (values.size() < fieldCount)
            return nullptr;

        content->fileId = stoi(values[0]);
        content->filename = encIn->convert(values[1]);
        content->size = stoi(values[2],0,16);
        content->modifyTime = stoi(values[3],0,16);
        content->fileType = stoi(values[4],0,16);

        return content;
    }
};

class Debuger : public RecvProtocol
{
public:
    bool read(shared_ptr<Post> post)
    {
        if (!Logger::instance().isEnabled()) return false;
        std::ostringstream oss;
        oss << "==========================="
            << "\ncmd id : " << std::hex << post->cmdId
            << "\nfrom: " << post->from->toString()
            << "\n";
        int count = 0;
        for (unsigned char ch : post->extra){
            oss << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)ch << " ";
            if (++count >= 8){ oss << "\n"; count=0; }
        }
        FEIQ_LOG(oss.str());
        return false;
    }
};

class RecvReadMessage : public RecvProtocol
{
    DECLARE_TRIGGER(RecvReadMessage)
public:
    bool read(shared_ptr<Post> post)
    {
        if (post->cmdId == IPMSG_RECVMSG)
        {
            IdType id = static_cast<IdType>(stoll(toString(post->extra)));
            auto content = make_shared<IdContent>();
            content->id = id;
            post->addContent(content);
            trigger(post);
            return true;
        }
        return false;
    }
};

// RecvImage: 处理 UDP 内联图片分片
// 飞秋图片协议: Windows 飞秋将图片数据拆分成多个 UDP 包发送
// extra 格式: imageId|seqNum|totalSize|width|height|chunkSize|...|flags#\0<图片二进制数据>
// 每个 UDP 包都是独立的 IPMSG 包（cmdId = IPMSG_SENDIMAGE | IPMSG_FILEATTACHOPT）
typedef std::function<void (shared_ptr<Post> post, const string& imageId,
                           size_t totalSize, int width, int height,
                           const char* chunkData, size_t chunkLen)> OnImageChunk;

class RecvImage : public RecvProtocol
{
public:
    RecvImage(OnImageChunk handler) : mHandler(handler) {}

    bool read(shared_ptr<Post> post)
    {
        if (IS_CMD_SET(post->cmdId, IPMSG_SENDIMAGE)
            && IS_OPT_SET(post->cmdId, IPMSG_FILEATTACHOPT))
        {
            auto& extra = post->extra;
            if (extra.empty()) return true; // 空 extra，丢弃

            // 解析 extra: 用 '|' (HLIST_ENTRY_SEPARATOR) 分隔的元信息头，
            // 后跟 '#' + '\0' + 二进制图片数据
            // 格式: imageId|seqNum|totalSize|width|height|chunkSize|0|2|0|00000000#\0<data>

            // 找到元信息头的结束位置（'#' 后跟 '\0'）
            // 先找 \0 位置——它分隔元信息和二进制数据
            auto zeroPos = find(extra.begin(), extra.end(), '\0');
            if (zeroPos == extra.end()) return true; // 没有二进制数据

            // 解析元信息头（\0 之前的部分）
            string header(extra.begin(), zeroPos);

            // 用 '|' 分割头部字段
            vector<string> fields;
            {
                string field;
                for (char ch : header) {
                    if (ch == '|') {
                        fields.push_back(field);
                        field.clear();
                    } else if (ch == '#') {
                        fields.push_back(field);
                        break;
                    } else {
                        field += ch;
                    }
                }
                if (!field.empty()) fields.push_back(field);
            }

            // fields[0]=imageId, fields[1]=totalSize(十进制), fields[2]=offset
            // fields[3]=totalChunks, fields[4]=chunkSeq, fields[5]=chunkSize
            if (fields.size() < 2) return true;

            string imageId = fields[0];

            // 总大小（十进制）
            size_t totalSize = 0;
            try { totalSize = stoull(fields[1]); } catch (...) {}

            // 二进制数据在 \0 后面
            auto dataStart = zeroPos + 1;
            size_t chunkLen = distance(dataStart, extra.end());

            if (chunkLen > 0 && mHandler)
            {
                mHandler(post, imageId, totalSize, 0, 0,
                        &(*dataStart), chunkLen);
            }

            return true; // 拦截！不让后续协议处理器（特别是 EndRecv/onMsg）处理
        }
        return false;
    }
private:
    OnImageChunk mHandler;
};

/**
 * @brief The EndRecv class 终止解析链
 */
class EndRecv : public RecvProtocol
{
    DECLARE_TRIGGER(EndRecv)
public:
    bool read(shared_ptr<Post> post)
    {
        if (!post->contents.empty())
            trigger(post);
        return true;
    }
};

//添加一条接收协议，触发时更新好友信息，并调用func
#define ADD_RECV_PROTOCOL(protocol, func)\
{\
    RecvProtocol* p = new protocol([this](shared_ptr<Post> post){\
        post->from = this->addOrUpdateFellow(post->from);\
        this->func(post);});\
    mRecvProtocols.push_back(unique_ptr<RecvProtocol>(p));\
    mCommu.addRecvProtocol(p);\
 }

//添加一条接收协议，无触发
#define ADD_RECV_PROTOCOL2(protocol)\
{\
    RecvProtocol* p = new protocol();\
    mRecvProtocols.push_back(unique_ptr<RecvProtocol>(p));\
    mCommu.addRecvProtocol(p);\
}

//添加一条接收协议，触发时更新好友信息
#define ADD_RECV_PROTOCOL3(protocol)\
{\
    RecvProtocol* p = new protocol([this](shared_ptr<Post> post){\
        post->from = this->addOrUpdateFellow(post->from);});\
    mRecvProtocols.push_back(unique_ptr<RecvProtocol>(p));\
    mCommu.addRecvProtocol(p);\
}

//添加一条发送协议
#define ADD_SEND_PROTOCOL(protocol, sender, args...)\
{\
    mContentSender[protocol]=make_shared<sender>(##args);\
}

FeiqEngine::FeiqEngine()
{
    ADD_RECV_PROTOCOL2(Debuger);//仅用于开发中的调试

    ADD_RECV_PROTOCOL3(RecvAnsEntry);
    ADD_RECV_PROTOCOL(RecvBrEntry, onBrEntry);
    ADD_RECV_PROTOCOL3(RecvBrExit);
    ADD_RECV_PROTOCOL(RecvBrAbsence, onBrAbsence);
    ADD_RECV_PROTOCOL(RecvSendCheck, onSendCheck);
    ADD_RECV_PROTOCOL(RecvReadCheck, onReadCheck);
    ADD_RECV_PROTOCOL(RecvReadMessage, onReadMessage);//好友回复消息已经阅读
    ADD_RECV_PROTOCOL2(RecvText);
    // RecvImage 手动注册，传入图片分片处理回调
    {
        RecvProtocol* p = new RecvImage([this](shared_ptr<Post> post,
                const string& imageId, size_t totalSize, int width, int height,
                const char* chunkData, size_t chunkLen){
            post->from = this->addOrUpdateFellow(post->from);
            this->handleImageChunk(imageId, totalSize, width, height,
                                   chunkData, chunkLen, post->from);
        });
        mRecvProtocols.push_back(unique_ptr<RecvProtocol>(p));
        mCommu.addRecvProtocol(p);
    }
    ADD_RECV_PROTOCOL2(RecvKnock);
    ADD_RECV_PROTOCOL2(RecvFile);
    ADD_RECV_PROTOCOL(EndRecv, onMsg);

    ADD_SEND_PROTOCOL(ContentType::Text, SendTextContent);
    ADD_SEND_PROTOCOL(ContentType::Knock, SendKnockContent);
    ADD_SEND_PROTOCOL(ContentType::File, SendFileContent);

    mCommu.setFileServerHandler(std::bind(&FeiqEngine::fileServerHandler,
                                          this,
                                          placeholders::_1,
                                          placeholders::_2,
                                          placeholders::_3,
                                          placeholders::_4,
                                          placeholders::_5));
}

pair<bool, string> FeiqEngine::send(shared_ptr<Fellow> fellow, shared_ptr<Content> content)
{
    if (content == nullptr)
        return {false, "要发送的内容无效"};

    auto& sender = mContentSender[content->type()];
    if (sender == nullptr)
        return {false, "no send protocol can send"};

    sender->setContent(content.get());
    auto ip = fellow->getIp();
    auto ret = mCommu.send(ip, *sender);
    if (ret.first == 0)
    {
        return {false, ret.second};
    }

    content->setPacketNo(ret.first);

    // 记录发送的消息到聊天记录
    {
        string contentText;
        int contentType = static_cast<int>(content->type());
        switch (content->type())
        {
        case ContentType::Text:
            contentText = static_cast<TextContent*>(content.get())->text;
            break;
        case ContentType::File:
            contentText = static_cast<FileContent*>(content.get())->filename;
            break;
        case ContentType::Knock:
            contentText = "[窗口抖动]";
            break;
        default:
            contentText = "";
            break;
        }
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        mHistory.addRecord(fellow->getIp(), fellow->getName(), fellow->getMac(),
                          now, true, contentType, contentText);
    }

    if (content->type() == ContentType::File){
        auto ptr = dynamic_pointer_cast<FileContent>(content);
        mModel.addUploadTask(fellow, ptr)->setObserver(mView);
    }
    else if (content->type() == ContentType::Text){
        auto handler = std::bind(&FeiqEngine::onSendTimeo, this, placeholders::_1, ip, content);
        mAsyncWait.addWaitPack(content->packetNo, handler, 5000);
    }
    return {true, ""};
}

pair<bool, string> FeiqEngine::sendFiles(shared_ptr<Fellow> fellow, list<shared_ptr<FileContent>> &files)
{
    for (auto file : files) {
        auto ret = send(fellow, file);
        if (!ret.first)
            return ret;
    }
    return {true,""};
}

bool FeiqEngine::downloadFile(FileTask* task)
{
    if (task==nullptr)
        return false;

    task->setObserver(mView);

    if (task->getContent()->fileType == IPMSG_FILE_DIR)
        return downloadDir(task);

    auto func = [task, this](){
        auto fellow = task->fellow();
        auto content = task->getContent();

        auto client = mCommu.requestFileData(fellow->getIp(), *content, 0);
        if (client == nullptr)
        {
            task->setState(FileTaskState::Error, "请求下载文件失败，可能好友已经取消");
            return;
        }

        FILE* of = fopen(content->path.c_str(), "w+");
        if (of == nullptr){
            task->setState(FileTaskState::Error, "无法打开文件进行保存");
            return;
        }

//        Defer{//TODO:工作异常
//            [of](){
//                cout<<"close file now"<<endl;
//                fclose(of);
//            }
//        };

        const int unitSize = 2048;//一次请求2k
        const int maxTimeoCnt = 3;//最多允许超时3次
        const int timeo = 2000;//允许超时2s

        int recv = 0;
        auto total = content->size;
        std::array<char, unitSize> buf;
        int timeoCnt = 0;
        task->setState(FileTaskState::Running);
        while (recv < total)
        {
            if (task->hasCancelPending())
            {
                task->setState(FileTaskState::Canceled);
                fclose(of);
                return;
            }

            auto left = total - recv;
            auto request = unitSize > left ? left : unitSize;
            auto got = client->recv(buf.data(), request, timeo);
            if (got == -1 && ++timeoCnt >= maxTimeoCnt)
            {
                task->setState(FileTaskState::Error, "下载文件超时，好友可能掉线");
                fclose(of);
                return;
            }
            else if (got < 0)
            {
                task->setState(FileTaskState::Error, "接收数据出错，可能网络错误");
                fclose(of);
                return;
            }
            else
            {
                fwrite(buf.data(), 1, got, of);
                recv+=got;
                task->setProcess(recv);
            }
        }

        fclose(of);
        task->setProcess(total);
        task->setState(FileTaskState::Finish);
    };

    thread thd(func);
    thd.detach();

    return task;
}

bool FeiqEngine::downloadDir(FileTask* task)
{
    task->setObserver(mView);

    auto func = [task, this](){
        auto fellow = task->fellow();
        auto content = task->getContent();

        auto client = mCommu.requestFileData(fellow->getIp(), *content, 0);
        if (client == nullptr)
        {
            task->setState(FileTaskState::Error, "请求下载目录失败，可能好友已经取消");
            return;
        }

        task->setState(FileTaskState::Running);

        // 目录传输协议：循环读取 header，解析出文件/子目录/返回上级
        // header格式: header-size:filename:file-size:fileattr:\a
        // 其中 fileattr 的低位标识类型（IPMSG_FILE_REGULAR=1, IPMSG_FILE_DIR=2, IPMSG_FILE_RETPARENT=3）

        string currentPath = content->path;
        // 创建根目录
        mkdir(currentPath.c_str(), 0755);

        const int bufSize = 4096;
        std::vector<char> buf(bufSize);
        string headerBuf;
        int totalRecv = 0;
        bool done = false;
        int depthCount = 1; // 进入根目录算1层

        while (!done && depthCount > 0)
        {
            if (task->hasCancelPending())
            {
                task->setState(FileTaskState::Canceled);
                return;
            }

            // 读取 header（以 \a 即 0x07 结尾）
            headerBuf.clear();
            bool gotHeader = false;
            while (!gotHeader)
            {
                char c;
                auto got = client->recv(&c, 1, 5000);
                if (got <= 0)
                {
                    // 超时或错误，可能传输完成
                    done = true;
                    break;
                }
                if (c == FILELIST_SEPARATOR)
                {
                    gotHeader = true;
                }
                else
                {
                    headerBuf += c;
                }
            }

            if (!gotHeader) break;

            // 解析 header: headerSize:filename:fileSize:fileAttr[:extAttr...]
            // 字段间以 ':' (0x3a) 分隔
            std::vector<string> fields;
            string field;
            for (char c : headerBuf)
            {
                if (c == HLIST_ENTRY_SEPARATOR)
                {
                    fields.push_back(field);
                    field.clear();
                }
                else
                {
                    field += c;
                }
            }
            if (!field.empty())
                fields.push_back(field);

            if (fields.size() < 4) {
                done = true;
                break;
            }

            // string headerSizeStr = fields[0]; // 不需要
            string filename = fields[1];
            long long fileSize = 0;
            try { fileSize = stoll(fields[2], nullptr, 16); } catch (...) {}
            int fileAttr = 0;
            try { fileAttr = stoi(fields[3], nullptr, 16); } catch (...) {}

            int fileType = fileAttr & 0xFF; // 低8位是文件类型

            if (fileType == IPMSG_FILE_DIR)
            {
                // 进入子目录
                currentPath += "/" + filename;
                mkdir(currentPath.c_str(), 0755);
                depthCount++;
            }
            else if (fileType == IPMSG_FILE_RETPARENT)
            {
                // 返回上级目录
                auto pos = currentPath.rfind('/');
                if (pos != string::npos)
                    currentPath = currentPath.substr(0, pos);
                depthCount--;
            }
            else if (fileType == IPMSG_FILE_REGULAR)
            {
                // 普通文件，读取 fileSize 字节的数据
                string filePath = currentPath + "/" + filename;
                FILE* of = fopen(filePath.c_str(), "w+");
                if (of == nullptr) continue;

                long long recv = 0;
                const int unitSize = 2048;
                std::array<char, 2048> fileBuf;

                while (recv < fileSize)
                {
                    if (task->hasCancelPending())
                    {
                        fclose(of);
                        task->setState(FileTaskState::Canceled);
                        return;
                    }

                    auto left = fileSize - recv;
                    auto request = (long long)unitSize > left ? (int)left : unitSize;
                    auto got = client->recv(fileBuf.data(), request, 5000);
                    if (got <= 0)
                    {
                        fclose(of);
                        task->setState(FileTaskState::Error, "下载目录中的文件超时");
                        return;
                    }
                    fwrite(fileBuf.data(), 1, got, of);
                    recv += got;
                    totalRecv += got;
                    task->setProcess(totalRecv);
                }

                fclose(of);
            }
            // 其他类型（symlink 等）跳过
        }

        task->setProcess(totalRecv);
        task->setState(FileTaskState::Finish);
    };

    thread thd(func);
    thd.detach();

    return task;
}

class GetPubKey : public SendProtocol
{
public:
    int cmdId() {return IPMSG_GETPUBKEY;}
    void write(ostream& os){
        (void)os;
    }
};

pair<bool, string> FeiqEngine::start()
{
    if (mStarted)
    {
        return {true, "已经启动过"};
    }

    mCommu.setMyHost(encOut->convert(mHost));
    mCommu.setMyName(encOut->convert(mName));
    auto result = mCommu.start();

    if(result.first)
    {
        mAsyncWait.start();

        mMsgThd.start();
        mMsgThd.setHandler(std::bind(&FeiqEngine::dispatchMsg, this, placeholders::_1));

        mStarted = true;
        sendImOnLine();
    }

    return result;
}

void FeiqEngine::stop()
{
    if (mStarted)
    {
        mStarted=false;
        SendImOffLine imOffLine(mName);
        mCommu.send("255.255.255.255", imOffLine);
        broadcastToCurstomGroup(imOffLine);
        mCommu.stop();
        mAsyncWait.stop();
        mMsgThd.stop();
    }
}

void FeiqEngine::addToBroadcast(const string &ip)
{
    mBroadcast.push_back(ip);
}

void FeiqEngine::setMyHost(string host)
{
    mHost=host;
    if (mName.empty())
        mName = mHost;
}

void FeiqEngine::setMyName(string name){
    mName=name;
    if (mName.empty())
        mName = mHost;
}

void FeiqEngine::setAutoReply(bool enable, const string& text)
{
    mAutoReplyEnabled = enable;
    mAutoReplyText = text;
}

void FeiqEngine::sendImOnLine(const string &ip)
{
    SendImOnLine imOnLine(mName);

    if (ip.empty())
    {
        mCommu.send("255.255.255.255", imOnLine);
        broadcastToCurstomGroup(imOnLine);
    }
    else
    {
        mCommu.send(ip, imOnLine);
    }
}

void FeiqEngine::sendAbsence(AbsenceStatus status)
{
    mMyAbsenceStatus = status;
    SendAbsence absence(mName, status);
    mCommu.send("255.255.255.255", absence);
    broadcastToCurstomGroup(absence);
}

void FeiqEngine::enableIntervalDetect(int seconds)
{
    thread thd([this, seconds](){
        while(mStarted)
        {
            sleep(seconds);
            if (!mStarted)  break;

            SendImOnLine imOnLine(mName);
            broadcastToCurstomGroup(imOnLine);
        }
    });
    thd.detach();
}


FeiqModel &FeiqEngine::getModel()
{
    return mModel;
}

void FeiqEngine::initHistory(const string& dbPath)
{
    if (!mHistory.init(dbPath))
    {
        cerr<<"Failed to init history at: "<<dbPath<<endl;
    }
}

void FeiqEngine::onBrEntry(shared_ptr<Post> post)
{
    AnsBrEntry ans(mName);
    mCommu.send(post->from->getIp(), ans);
}

void FeiqEngine::onBrAbsence(shared_ptr<Post> post)
{
    // 好友状态变更，更新好友信息并通知 UI
    // addOrUpdateFellow 已经在宏中调用了
    (void)post;
}

void FeiqEngine::handleImageChunk(const string& imageId, size_t totalSize,
                                  int /*width*/, int /*height*/,
                                  const char* chunkData, size_t chunkLen,
                                  shared_ptr<Fellow> from)
{
    ImageChunkInfo completedInfo;
    bool completed = false;

    {
        lock_guard<mutex> lock(mImageChunkMutex);

        // 如果这个 imageId 已经完成过，忽略重传包
        if (mCompletedImageIds.count(imageId)) {
            return;
        }

        auto& info = mImageChunks[imageId];
        if (info.imageId.empty()) {
            // 第一个分片
            info.imageId = imageId;
            info.totalSize = totalSize;
            info.from = from;
            if (totalSize > 0) info.data.reserve(totalSize);
            FEIQ_LOG("[ImageRecv] 新图片开始接收: id=" << imageId
                 << " totalSize=" << totalSize);
        }

        // 更新最后收到 chunk 的时间
        info.lastChunkTime = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count();

        // 追加分片数据
        info.data.insert(info.data.end(), chunkData, chunkData + chunkLen);

        FEIQ_LOG("[ImageRecv] chunk: id=" << imageId
             << " chunkLen=" << chunkLen
             << " accumulated=" << info.data.size()
             << " totalSize=" << totalSize);

        // 用 totalSize 精确判断是否收齐
        if (totalSize > 0 && info.data.size() >= totalSize)
        {
            // 截断到 totalSize，最后一个 chunk 可能包含多余数据
            if (info.data.size() > totalSize) {
                FEIQ_LOG("[ImageRecv] 截断多余数据: id=" << imageId
                     << " actual=" << info.data.size()
                     << " totalSize=" << totalSize);
                info.data.resize(totalSize);
            }
            completedInfo = std::move(info);
            mImageChunks.erase(imageId);
            mCompletedImageIds.insert(imageId); // 记录已完成，防止重传
            completed = true;
        }
    }

    if (completed) {
        FEIQ_LOG("[ImageRecv] 图片接收完成: id=" << imageId
             << " totalBytes=" << completedInfo.data.size()
             << " expected=" << completedInfo.totalSize);
        saveAndNotifyImage(completedInfo);

        // 检查是否有其他图片超时
        checkImageChunkTimeout();
    }
}

void FeiqEngine::saveAndNotifyImage(const ImageChunkInfo& info)
{
    // 保存到 ~/.feiq/images/
    // 文件名格式: <receiveTimestampMs>_<imageId>.jpg
    // 不同时刻收到的同一张图片（同 imageId）不会相互覆盖
    string homeDir = getenv("HOME") ? getenv("HOME") : "/tmp";
    string imgDir = homeDir + "/.feiq/images";

    // 创建目录（mkdir -p，后台线程调用）
    mkdir((homeDir + "/.feiq").c_str(), 0755);
    mkdir(imgDir.c_str(), 0755);

    // 生成唯一文件名：毫秒时间戳 + imageId
    auto nowMs = chrono::duration_cast<chrono::milliseconds>(
                     chrono::system_clock::now().time_since_epoch()).count();
    string fileName = to_string(nowMs) + "_" + info.imageId + ".jpg";
    string savePath = imgDir + "/" + fileName;

    const char* dataPtr = info.data.data();
    size_t dataSize = info.data.size();

    // 轻量校验：检查 JPEG 头尾标记（仅用于日志）
    bool validJpeg = false;
    if (dataSize >= 4) {
        unsigned char h0 = (unsigned char)dataPtr[0];
        unsigned char h1 = (unsigned char)dataPtr[1];
        unsigned char t0 = (unsigned char)dataPtr[dataSize - 2];
        unsigned char t1 = (unsigned char)dataPtr[dataSize - 1];
        validJpeg = (h0 == 0xFF && h1 == 0xD8 && t0 == 0xFF && t1 == 0xD9);
        FEIQ_LOG("[ImageRecv] JPEG校验: header=" << std::hex << (int)h0 << (int)h1
             << " tail=" << (int)t0 << (int)t1
             << " valid=" << std::dec << validJpeg
             << " size=" << dataSize);
    }

    FILE* fp = fopen(savePath.c_str(), "wb");
    if (!fp) {
        FEIQ_LOG("[ImageRecv] 无法保存图片: " << savePath);
        return;
    }
    size_t written = fwrite(dataPtr, 1, dataSize, fp);
    fclose(fp);

    FEIQ_LOG("[ImageRecv] 图片已保存: " << savePath
         << " size=" << dataSize
         << " written=" << written
         << " validJpeg=" << validJpeg);

    // 通过 ViewEvent 通知 UI 显示图片
    auto event = make_shared<MessageViewEvent>();
    event->when = Post::now();
    event->fellow = info.from;

    auto content = make_shared<ImageContent>();
    content->imageId = info.imageId;
    content->localPath = savePath;
    event->contents.push_back(content);

    // 记录到历史：contentText 存 fileName（含时间戳前缀），保证唯一
    mHistory.addRecord(info.from->getIp(), info.from->getName(), info.from->getMac(),
                       nowMs, false, static_cast<int>(ContentType::Image), fileName);

    dispatchMsg(event);
}

void FeiqEngine::checkImageChunkTimeout()
{
    // 检查所有正在接收的图片，如果超过 5 秒没有收到新 chunk，
    // 则认为传输中断，保存已接收的部分数据
    auto now = chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()).count();

    vector<ImageChunkInfo> timedOut;

    {
        lock_guard<mutex> lock(mImageChunkMutex);
        vector<string> toRemove;

        for (auto& kv : mImageChunks) {
            auto& info = kv.second;
            if (info.lastChunkTime > 0 && (now - info.lastChunkTime) > 5000) {
                FEIQ_LOG("[ImageRecv] 图片接收超时: id=" << info.imageId
                     << " received=" << info.data.size()
                     << " expected=" << info.totalSize
                     << " progress=" << (info.totalSize > 0 ? (int)(info.data.size() * 100 / info.totalSize) : 0) << "%");
                timedOut.push_back(std::move(info));
                toRemove.push_back(kv.first);
                mCompletedImageIds.insert(kv.first);
            }
        }

        for (auto& id : toRemove) {
            mImageChunks.erase(id);
        }
    }

    // 对超时的图片，保存已接收的部分数据（部分图片总比没有好）
    for (auto& info : timedOut) {
        if (!info.data.empty()) {
            saveAndNotifyImage(info);
        }
    }
}

void FeiqEngine::onMsg(shared_ptr<Post> post)
{
    static vector<string> rejectedImages;

    auto event = make_shared<MessageViewEvent>();
    event->when = post->when;
    event->fellow = post->from;

    auto it = post->contents.begin();
    auto end = post->contents.end();

    string reply;
    while (it != end)//过滤消息内容：删除不支持的包，并回复好友
    {
        bool rejected = false;
        if ((*it)->type() == ContentType::File)
        {
            auto fc = static_pointer_cast<FileContent>(*it);

            if (fc->fileType == IPMSG_FILE_REGULAR || fc->fileType == IPMSG_FILE_DIR)
                mModel.addDownloadTask(event->fellow, fc);
        }
        else if ((*it)->type() == ContentType::Image)
        {
            // 图片消息暂不支持内联显示，但保留消息让 UI 展示提示
            // 不再自动回复拒绝消息
        }

        if (!rejected)
        {
            event->contents.push_back(*it);
        }
        ++it;
    }

    if (!reply.empty())
    {
        SendTextContent send;
        TextContent content;
        content.text = reply;
        send.setContent(&content);
        mCommu.send(post->from->getIp(), send);
    }

    if (!event->contents.empty())
    {
        // 记录收到的消息到聊天记录
        for (auto& c : event->contents)
        {
            string contentText;
            int contentType = static_cast<int>(c->type());
            switch (c->type())
            {
            case ContentType::Text:
                contentText = static_cast<TextContent*>(c.get())->text;
                break;
            case ContentType::File:
                contentText = static_cast<FileContent*>(c.get())->filename;
                break;
            case ContentType::Knock:
                contentText = "[窗口抖动]";
                break;
            case ContentType::Image:
            {
                // 保存 packetNo_imageId 用于关联本地图片文件
                auto ic = static_cast<ImageContent*>(c.get());
                contentText = to_string(ic->packetNo) + "_" + ic->imageId;
                break;
            }
            default:
                contentText = "";
                break;
            }
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            mHistory.addRecord(post->from->getIp(), post->from->getName(), post->from->getMac(),
                              now, false, contentType, contentText);
        }
        mMsgThd.sendMessage(event);
    }

    // 自动应答：收到普通文本消息时自动回复
    // 检查消息不是自动回复或广播（防死循环）
    if (mAutoReplyEnabled && !mAutoReplyText.empty()
        && !IS_OPT_SET(post->cmdId, IPMSG_NO_REPLY_OPTS))
    {
        SendAutoReplyContent autoReply(mAutoReplyText);
        mCommu.send(post->from->getIp(), autoReply);
    }
}

void FeiqEngine::onSendCheck(shared_ptr<Post> post)
{
    SendSentCheck reply(post->packetNo);
    mCommu.send(post->from->getIp(), reply);
}

void FeiqEngine::onReadCheck(shared_ptr<Post> post)
{
    SendReadCheck reply(post->packetNo);
    mCommu.send(post->from->getIp(), reply);
}

void FeiqEngine::onSendTimeo(IdType packetId, const string& ip, shared_ptr<Content> content)
{
    auto event = make_shared<SendTimeoEvent>();
    event->fellow = mModel.findFirstFellowOf(ip);
    if (event->fellow == nullptr)
        return;

    event->content = content;
    mMsgThd.sendMessage(event);
}

void FeiqEngine::onReadMessage(shared_ptr<Post> post)
{
    if (post->contents.empty())
        return;
    auto content = dynamic_pointer_cast<IdContent>(post->contents[0]);
    mAsyncWait.clearWaitPack(content->id);
}

void FeiqEngine::fileServerHandler(unique_ptr<TcpSocket> client, int cmdId, int packetNo, int fileId, int offset)
{
    auto task = mModel.findTask(packetNo, fileId);
    if (task == nullptr)
        return;

    // 如果是目录请求 (IPMSG_GETDIRFILES = 0x62)，走目录上传逻辑
    if (cmdId == IPMSG_GETDIRFILES)
    {
        auto func = [task](unique_ptr<TcpSocket> client){
            task->setState(FileTaskState::Running);
            string rootPath = task->getContent()->path;

            // 递归发送目录结构的辅助 lambda
            // 目录协议格式: header-size:filename:file-size:fileattr:\a
            // 其中 \a = 0x07 = FILELIST_SEPARATOR
            std::function<bool(const string&, TcpSocket*, FileTask*, int&)> sendDirRecursive;
            sendDirRecursive = [&sendDirRecursive](const string& dirPath, TcpSocket* sock, FileTask* task, int& totalSent) -> bool
            {
                DIR* dir = opendir(dirPath.c_str());
                if (dir == nullptr) return false;

                char sep = HLIST_ENTRY_SEPARATOR;
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr)
                {
                    if (task->hasCancelPending())
                    {
                        closedir(dir);
                        return false;
                    }

                    string name = entry->d_name;
                    if (name == "." || name == "..") continue;

                    string fullPath = dirPath + "/" + name;
                    struct stat st;
                    if (stat(fullPath.c_str(), &st) != 0) continue;

                    if (S_ISDIR(st.st_mode))
                    {
                        // 发送目录 header: headerSize:filename:fileSize:fileAttr:\a
                        // fileSize 对目录来说是 0
                        stringstream ss;
                        ss << std::hex;
                        // 先计算 header 内容（不含 headerSize 本身）
                        stringstream innerSs;
                        innerSs << std::hex;
                        innerSs << name << sep << 0 << sep << IPMSG_FILE_DIR << sep;
                        string inner = innerSs.str();
                        // headerSize = inner.size() + headerSize 字段本身的长度 + 1(:)
                        // 先用一个估计值
                        int headerLen = inner.size();
                        stringstream headerSizeSs;
                        headerSizeSs << std::hex << (headerLen + 3); // 粗略估计 headerSize 字段约 2-3 字符
                        string headerSizeStr = headerSizeSs.str();
                        int actualHeaderLen = headerSizeStr.size() + 1 + inner.size(); // +1 for ':'
                        // 重新计算
                        stringstream finalSs;
                        finalSs << std::hex << actualHeaderLen << sep << inner;
                        string header = finalSs.str();
                        header += FILELIST_SEPARATOR;

                        int ret = sock->send(header.data(), header.size());
                        if (ret < 0)
                        {
                            closedir(dir);
                            return false;
                        }

                        // 递归进入子目录
                        if (!sendDirRecursive(fullPath, sock, task, totalSent))
                        {
                            closedir(dir);
                            return false;
                        }

                        // 发送返回上级 header: headerSize:.:0:IPMSG_FILE_RETPARENT:\a
                        stringstream retSs;
                        retSs << std::hex;
                        stringstream retInner;
                        retInner << std::hex;
                        retInner << "." << sep << 0 << sep << IPMSG_FILE_RETPARENT << sep;
                        string retInnerStr = retInner.str();
                        int retHeaderLen = retInnerStr.size();
                        stringstream retHdrSizeSs;
                        retHdrSizeSs << std::hex << (retHeaderLen + 3);
                        string retHdrSizeStr = retHdrSizeSs.str();
                        int actualRetLen = retHdrSizeStr.size() + 1 + retInnerStr.size();
                        stringstream retFinalSs;
                        retFinalSs << std::hex << actualRetLen << sep << retInnerStr;
                        string retHeader = retFinalSs.str();
                        retHeader += FILELIST_SEPARATOR;

                        ret = sock->send(retHeader.data(), retHeader.size());
                        if (ret < 0)
                        {
                            closedir(dir);
                            return false;
                        }
                    }
                    else if (S_ISREG(st.st_mode))
                    {
                        // 发送文件 header: headerSize:filename:fileSize:fileAttr:\a
                        long long fileSize = st.st_size;
                        stringstream innerSs;
                        innerSs << std::hex;
                        innerSs << name << sep << fileSize << sep << IPMSG_FILE_REGULAR << sep;
                        string inner = innerSs.str();
                        int headerLen = inner.size();
                        stringstream headerSizeSs;
                        headerSizeSs << std::hex << (headerLen + 3);
                        string headerSizeStr = headerSizeSs.str();
                        int actualHeaderLen = headerSizeStr.size() + 1 + inner.size();
                        stringstream finalSs;
                        finalSs << std::hex << actualHeaderLen << sep << inner;
                        string header = finalSs.str();
                        header += FILELIST_SEPARATOR;

                        int ret = sock->send(header.data(), header.size());
                        if (ret < 0)
                        {
                            closedir(dir);
                            return false;
                        }

                        // 发送文件数据
                        FILE* fp = fopen(fullPath.c_str(), "r");
                        if (fp == nullptr) continue;

                        const int unitSize = 2048;
                        std::array<char, 2048> buf;
                        long long sent = 0;
                        while (sent < fileSize && !feof(fp))
                        {
                            auto left = fileSize - sent;
                            auto request = (long long)unitSize > left ? (int)left : unitSize;
                            int got = fread(buf.data(), 1, request, fp);
                            if (got <= 0) break;
                            got = sock->send(buf.data(), got);
                            if (got < 0)
                            {
                                fclose(fp);
                                closedir(dir);
                                return false;
                            }
                            sent += got;
                            totalSent += got;
                            task->setProcess(totalSent);
                        }
                        fclose(fp);
                    }
                    // 其他类型（symlink等）跳过
                }

                closedir(dir);
                return true;
            };

            int totalSent = 0;
            bool success = sendDirRecursive(rootPath, client.get(), task.get(), totalSent);

            if (task->hasCancelPending())
            {
                task->setState(FileTaskState::Canceled);
            }
            else if (success)
            {
                task->setProcess(totalSent);
                task->setState(FileTaskState::Finish);
            }
            else
            {
                task->setState(FileTaskState::Error, "目录发送失败，可能是网络问题");
            }
        };

        thread thd(func, std::move(client));
        thd.detach();
        return;
    }

    // 普通文件上传
    auto func = [task, offset](unique_ptr<TcpSocket> client){
        FILE* is = fopen(task->getContent()->path.c_str(), "r");
        if (is == nullptr)
        {
            task->setState(FileTaskState::Error, "无法读取文件");
        }

//        Defer{
//            [is](){
//                fclose(is);
//            }
//        };

        if (offset > 0)
            fseek(is, offset, SEEK_SET);

        const int unitSize = 2048;//一次发送2k
        std::array<char, unitSize> buf;
        auto total = task->getContent()->size;
        int sent = 0;

        task->setState(FileTaskState::Running);
        while (sent < total && !feof(is))
        {
            auto left = total - sent;
            auto request = unitSize > left ? left : unitSize;
            int got = fread(buf.data(), 1, request, is);
            got = client->send(buf.data(), got);
            if (got < 0)
            {
                task->setState(FileTaskState::Error, "无法发送数据，可能是网络问题");
                fclose(is);
                return;
            }

            sent+=got;
            task->setProcess(sent);
        }

        if (sent != total)
        {
            task->setState(FileTaskState::Error, "文件未完整发送，可能是发送期间文件被改动");
        }
        else
        {
            task->setProcess(total);
            task->setState(FileTaskState::Finish);
        }

        fclose(is);
    };

    thread thd(func, std::move(client));
    thd.detach();
}

shared_ptr<Fellow> FeiqEngine::addOrUpdateFellow(shared_ptr<Fellow> fellow)
{
    auto f = mModel.getFullInfoOf(fellow);
    bool shouldApdate = false;

    if (f == nullptr)
    {
        mModel.addFellow(fellow);
        f = fellow;
        shouldApdate = true;
    }
    else
    {
        if (f->update(*fellow))
            shouldApdate = true;
    }

    if (shouldApdate){
        auto event = make_shared<FellowViewEvent>();
        event->what = ViewEventType::FELLOW_UPDATE;
        event->fellow = f;
        event->when = Post::now();
        mMsgThd.sendMessage(event);
    }

    return f;
}

void FeiqEngine::dispatchMsg(shared_ptr<ViewEvent> msg)
{
    mView->onEvent(msg);
}

void FeiqEngine::broadcastToCurstomGroup(SendProtocol &protocol)
{
    for (auto ip : mBroadcast)
    {
        if (!mStarted)
            break;//发送过程是一个耗时网络操作，如果已经stop，则中断

        mCommu.send(ip, protocol);
    }
}
