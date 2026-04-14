#ifndef HISTORY_H
#define HISTORY_H

#include "fellow.h"
#include <memory>
#include <vector>
#include <algorithm>
#include "content.h"
#include "post.h"
#include <sqlite3.h>
#include <unordered_map>

using namespace std;

struct HistoryRecord{
    time_point<steady_clock, milliseconds> when;
    shared_ptr<Fellow> who;
    shared_ptr<Content> what;
};

// 简化的历史记录（用于 UI 显示）
struct SimpleHistoryRecord {
    long long timestamp;    // 毫秒级时间戳
    bool isSelf;           // 是否是自己发的
    int contentType;       // ContentType 枚举值
    string contentText;    // 文本内容或文件名
};

/**
 * @brief The History class 以Content为单位，记录和查询聊天记录
 */
class History
{
public:
    History();

public:
    bool init(const string& dbPath);
    void unInit();

public:
    // 新的简化接口
    void addRecord(const string& fellowIp, const string& fellowName, const string& fellowMac,
                   long long timestamp, bool isSelf, int contentType, const string& contentText);
    vector<SimpleHistoryRecord> queryByIp(const string& fellowIp, int limit = 50);
    vector<SimpleHistoryRecord> queryByIp(const string& fellowIp, int limit, int offset);

    // 查询所有历史好友（ip, name），用于启动时展示离线好友
    struct HistoryFellow {
        string ip;
        string name;
        string mac;
        long long lastMsgTime = 0; // 最后一条消息时间戳（毫秒）
    };
    vector<HistoryFellow> queryAllFellows();

    // 旧接口保留兼容
    void add(const HistoryRecord &record);
    vector<HistoryRecord> query(const string& selection, const vector<string> &args);

private:
    unique_ptr<Fellow> getFellow(int id);
    int findFellowId(const string& ip, const string& mac = "");
    void migrateIfNeeded();

private:
    sqlite3* mDb = nullptr;
};

#endif // HISTORY_H
