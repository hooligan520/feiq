#include "history.h"
#include "logger.h"
#include "defer.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "defer.h"

#define FELLOW_TABLE "fellow"
#define MESSAGE_TABLE "message"

#define CHECK_SQLITE_RET(ret, action, result)\
if (ret != SQLITE_OK)\
{\
    FEIQ_LOG("failed to " #action ": " << ret);\
    return result;\
}

#define CHECK_SQLITE_RET2(ret, action)\
if (ret != SQLITE_OK)\
{\
    FEIQ_LOG("failed to " #action ": " << ret);\
    return;\
}

History::History()
{

}

bool History::init(const string &dbPath)
{
    FEIQ_LOG("[History] init: dbPath=" << dbPath);
    unInit();

    //检查数据库文件是否存在，若不存在，需要创建表
    bool needCreateTable=false;
    auto ret = access(dbPath.c_str(), F_OK);
    if (ret == -1)
    {
        FEIQ_LOG("[History] db may be new created: " << strerror(errno));
        needCreateTable=true;
    }

    //打开数据库
    ret = sqlite3_open(dbPath.c_str(), &mDb);
    bool success = false;
    Defer closeDbIfErr{
        [this, &success]()
        {
            if (!success)
            {
                FEIQ_LOG("[History] init failed, close db now");
                sqlite3_close(mDb);//除非内存不够，否则open总是会分配mDb的内存，总是需要close
                mDb = nullptr;
            }
        }
    };

    CHECK_SQLITE_RET(ret, "open sqlite", false);

    //创建表
    if (needCreateTable)
    {
        string createFellowTable = "create table " FELLOW_TABLE "(id integer primary key autoincrement, ip text, name text, mac text);";
        string createMessageTable = "create table " MESSAGE_TABLE " (id integer primary key autoincrement, fellow integer, time integer, is_self integer, content_type integer, content_text text)";

        ret = sqlite3_exec(mDb, createFellowTable.c_str(), nullptr, nullptr, nullptr);
        CHECK_SQLITE_RET(ret, "create fellow table", false);

        ret = sqlite3_exec(mDb, createMessageTable.c_str(), nullptr, nullptr, nullptr);
        CHECK_SQLITE_RET(ret, "create message table", false);
    }
    else
    {
        // 检查是否需要迁移旧表结构
        migrateIfNeeded();
    }

    success=true;
    return true;
}

void History::unInit()
{
    if (mDb != nullptr)
    {
        sqlite3_close(mDb);
        mDb = nullptr;
    }
}

void History::migrateIfNeeded()
{
    // 检查 message 表是否有 is_self 列（新结构）
    string sql = "PRAGMA table_info(" MESSAGE_TABLE ")";
    sqlite3_stmt* stmt = nullptr;
    auto ret = sqlite3_prepare_v2(mDb, sql.c_str(), sql.length(), &stmt, nullptr);
    if (ret != SQLITE_OK) return;

    bool hasIsSelf = false;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* colName = (const char*)sqlite3_column_text(stmt, 1);
        if (colName && string(colName) == "is_self")
        {
            hasIsSelf = true;
            break;
        }
    }
    sqlite3_finalize(stmt);

    if (!hasIsSelf)
    {
        // 旧表结构，需要重建
        // 先删除旧表（旧表数据兼容性差，直接重建）
        sqlite3_exec(mDb, "DROP TABLE IF EXISTS " MESSAGE_TABLE, nullptr, nullptr, nullptr);
        string createMessageTable = "create table " MESSAGE_TABLE " (id integer primary key autoincrement, fellow integer, time integer, is_self integer, content_type integer, content_text text)";
        sqlite3_exec(mDb, createMessageTable.c_str(), nullptr, nullptr, nullptr);
        FEIQ_LOG("[History] migrated message table to new schema");
    }
}

void History::addRecord(const string& fellowIp, const string& fellowName, const string& fellowMac,
                        long long timestamp, bool isSelf, int contentType, const string& contentText)
{
    FEIQ_LOG("[History] addRecord: ip=" << fellowIp << " isSelf=" << isSelf
         << " type=" << contentType << " text=" << contentText.substr(0, 30));
    if (mDb == nullptr) return;

    int fellowId = findFellowId(fellowIp, fellowMac);
    if (fellowId < 0)
    {
        // 插入新好友
        string sql = "INSERT INTO " FELLOW_TABLE " (ip, name, mac) VALUES (?, ?, ?)";
        sqlite3_stmt* stmt = nullptr;
        auto ret = sqlite3_prepare_v2(mDb, sql.c_str(), sql.length(), &stmt, nullptr);
        if (ret == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, fellowIp.c_str(), fellowIp.length(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, fellowName.c_str(), fellowName.length(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, fellowMac.c_str(), fellowMac.length(), SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        fellowId = findFellowId(fellowIp);
    }
    else
    {
        // 更新好友名称
        string sql = "UPDATE " FELLOW_TABLE " SET name = ? WHERE id = ?";
        sqlite3_stmt* stmt = nullptr;
        auto ret = sqlite3_prepare_v2(mDb, sql.c_str(), sql.length(), &stmt, nullptr);
        if (ret == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, fellowName.c_str(), fellowName.length(), SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, fellowId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    if (fellowId < 0) return;

    // 插入消息记录
    string sql = "INSERT INTO " MESSAGE_TABLE " (fellow, time, is_self, content_type, content_text) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    auto ret = sqlite3_prepare_v2(mDb, sql.c_str(), sql.length(), &stmt, nullptr);
    if (ret == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, fellowId);
        sqlite3_bind_int64(stmt, 2, timestamp);
        sqlite3_bind_int(stmt, 3, isSelf ? 1 : 0);
        sqlite3_bind_int(stmt, 4, contentType);
        sqlite3_bind_text(stmt, 5, contentText.c_str(), contentText.length(), SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

vector<SimpleHistoryRecord> History::queryByIp(const string& fellowIp, int limit)
{
    vector<SimpleHistoryRecord> result;
    if (mDb == nullptr) return result;

    int fellowId = findFellowId(fellowIp);
    if (fellowId < 0) return result;

    string sql = "SELECT time, is_self, content_type, content_text FROM " MESSAGE_TABLE
                 " WHERE fellow = ? ORDER BY time DESC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    auto ret = sqlite3_prepare_v2(mDb, sql.c_str(), sql.length(), &stmt, nullptr);
    if (ret != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, fellowId);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        SimpleHistoryRecord record;
        record.timestamp = sqlite3_column_int64(stmt, 0);
        record.isSelf = sqlite3_column_int(stmt, 1) != 0;
        record.contentType = sqlite3_column_int(stmt, 2);
        const char* text = (const char*)sqlite3_column_text(stmt, 3);
        record.contentText = text ? text : "";
        result.push_back(record);
    }
    sqlite3_finalize(stmt);

    // 结果是逆序的（最新在前），反转为正序
    std::reverse(result.begin(), result.end());
    return result;
}

vector<SimpleHistoryRecord> History::queryByIp(const string& fellowIp, int limit, int offset)
{
    vector<SimpleHistoryRecord> result;
    if (mDb == nullptr) return result;

    int fellowId = findFellowId(fellowIp);
    if (fellowId < 0) return result;

    string sql = "SELECT time, is_self, content_type, content_text FROM " MESSAGE_TABLE
                 " WHERE fellow = ? ORDER BY time DESC LIMIT ? OFFSET ?";
    sqlite3_stmt* stmt = nullptr;
    auto ret = sqlite3_prepare_v2(mDb, sql.c_str(), sql.length(), &stmt, nullptr);
    if (ret != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, fellowId);
    sqlite3_bind_int(stmt, 2, limit);
    sqlite3_bind_int(stmt, 3, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        SimpleHistoryRecord record;
        record.timestamp = sqlite3_column_int64(stmt, 0);
        record.isSelf = sqlite3_column_int(stmt, 1) != 0;
        record.contentType = sqlite3_column_int(stmt, 2);
        const char* text = (const char*)sqlite3_column_text(stmt, 3);
        record.contentText = text ? text : "";
        result.push_back(record);
    }
    sqlite3_finalize(stmt);

    // 结果是逆序的（最新在前），反转为正序
    std::reverse(result.begin(), result.end());
    return result;
}

// 旧接口保留兼容
void History::add(const HistoryRecord& record)
{
    if (mDb == nullptr || record.who == nullptr || record.what == nullptr) return;

    string contentText;
    int contentType = static_cast<int>(record.what->type());

    switch (record.what->type())
    {
    case ContentType::Text:
    {
        auto tc = static_cast<TextContent*>(record.what.get());
        contentText = tc->text;
        break;
    }
    case ContentType::File:
    {
        auto fc = static_cast<FileContent*>(record.what.get());
        contentText = fc->filename;
        break;
    }
    case ContentType::Knock:
        contentText = "[窗口抖动]";
        break;
    case ContentType::Image:
    {
        auto ic = static_cast<ImageContent*>(record.what.get());
        contentText = ic->imageId; // 保存 imageId，用于关联本地图片文件
        break;
    }
    default:
        contentText = "[不支持的内容]";
        break;
    }

    addRecord(record.who->getIp(), record.who->getName(), record.who->getMac(),
              record.when.time_since_epoch().count(), false, contentType, contentText);
}

vector<HistoryRecord> History::query(const string& selection, const vector<string>& args)
{
    vector<HistoryRecord> result;
    // 旧接口保留但返回空，新代码使用 queryByIp
    (void)selection;
    (void)args;
    return result;
}

unique_ptr<Fellow> History::getFellow(int id)
{
    if (mDb == nullptr) return nullptr;

    string sql = "SELECT ip, name, mac FROM " FELLOW_TABLE " WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    auto ret = sqlite3_prepare_v2(mDb, sql.c_str(), sql.length(), &stmt, nullptr);
    if (ret != SQLITE_OK) return nullptr;

    sqlite3_bind_int(stmt, 1, id);

    unique_ptr<Fellow> fellow;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        fellow = unique_ptr<Fellow>(new Fellow());
        const char* ip = (const char*)sqlite3_column_text(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        const char* mac = (const char*)sqlite3_column_text(stmt, 2);
        if (ip) fellow->setIp(ip);
        if (name) fellow->setName(name);
        if (mac) fellow->setMac(mac);
    }

    sqlite3_finalize(stmt);
    return fellow;
}

int History::findFellowId(const string &ip, const string &mac)
{
    if (mDb == nullptr) return -1;

    // 先按 MAC 查（MAC 比 IP 更稳定）
    if (!mac.empty())
    {
        string sql = "SELECT id, ip FROM " FELLOW_TABLE " WHERE mac = ?";
        sqlite3_stmt* stmt = nullptr;
        auto ret = sqlite3_prepare_v2(mDb, sql.c_str(), sql.length(), &stmt, nullptr);
        if (ret == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, mac.c_str(), mac.length(), SQLITE_TRANSIENT);
            int fellowId = -1;
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                fellowId = sqlite3_column_int(stmt, 0);
                const char* storedIp = (const char*)sqlite3_column_text(stmt, 1);
                sqlite3_finalize(stmt);

                // 如果 IP 变了，顺手更新（换 IP 的情况）
                if (storedIp && ip != storedIp)
                {
                    string updateSql = "UPDATE " FELLOW_TABLE " SET ip = ? WHERE id = ?";
                    sqlite3_stmt* updStmt = nullptr;
                    if (sqlite3_prepare_v2(mDb, updateSql.c_str(), updateSql.length(), &updStmt, nullptr) == SQLITE_OK)
                    {
                        sqlite3_bind_text(updStmt, 1, ip.c_str(), ip.length(), SQLITE_TRANSIENT);
                        sqlite3_bind_int(updStmt, 2, fellowId);
                        sqlite3_step(updStmt);
                        sqlite3_finalize(updStmt);
                        FEIQ_LOG("[History] fellow IP updated: " << storedIp << " -> " << ip << " (mac=" << mac << ")");
                    }
                }
                return fellowId;
            }
            sqlite3_finalize(stmt);
        }
    }

    // MAC 查不到，按 IP 查（兼容旧数据或无 MAC 的客户端）
    string sql = "SELECT id FROM " FELLOW_TABLE " WHERE ip = ?";
    sqlite3_stmt* stmt = nullptr;
    auto ret = sqlite3_prepare_v2(mDb, sql.c_str(), sql.length(), &stmt, nullptr);
    if (ret != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, ip.c_str(), ip.length(), SQLITE_TRANSIENT);

    int fellowId = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        fellowId = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return fellowId;
}

vector<History::HistoryFellow> History::queryAllFellows()
{
    vector<HistoryFellow> result;
    if (mDb == nullptr) return result;

    // 查询所有好友，以及每个好友最后一条消息的时间戳
    string sql =
        "SELECT f.ip, f.name, f.mac, MAX(m.time) as last_time "
        "FROM " FELLOW_TABLE " f "
        "LEFT JOIN " MESSAGE_TABLE " m ON m.fellow = f.id "
        "GROUP BY f.id "
        "ORDER BY last_time DESC";

    sqlite3_stmt* stmt = nullptr;
    auto ret = sqlite3_prepare_v2(mDb, sql.c_str(), sql.length(), &stmt, nullptr);
    if (ret != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        HistoryFellow hf;
        const char* ip   = (const char*)sqlite3_column_text(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        const char* mac  = (const char*)sqlite3_column_text(stmt, 2);
        hf.ip   = ip   ? ip   : "";
        hf.name = name ? name : "";
        hf.mac  = mac  ? mac  : "";
        hf.lastMsgTime = sqlite3_column_int64(stmt, 3);
        if (!hf.ip.empty())
            result.push_back(hf);
    }
    sqlite3_finalize(stmt);
    return result;
}
