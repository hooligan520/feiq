#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>

/**
 * @brief 全局日志系统
 *
 * 用法：
 *   Logger::instance().init("/path/to/feiq.log", true);  // 启用
 *   Logger::instance().init("", false);                   // 禁用
 *   FEIQ_LOG("xxx " << var);
 */
class Logger
{
public:
    static Logger& instance()
    {
        static Logger inst;
        return inst;
    }

    // enabled=false 时不创建/写入任何文件
    void init(const std::string& logPath, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mEnabled = enabled;
        if (!enabled) return;

        mLogPath = logPath;
        mFile.open(logPath, std::ios::app);
    }

    bool isEnabled() const { return mEnabled; }

    void log(const std::string& msg)
    {
        if (!mEnabled) return;
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mFile.is_open()) return;

        // 时间戳
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()).count() % 1000;
        struct tm tm_info;
        localtime_r(&t, &tm_info);

        char timeBuf[32];
        strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm_info);

        mFile << timeBuf << "." << std::setfill('0') << std::setw(3) << ms
              << " " << msg << "\n";
        mFile.flush();
    }

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool mEnabled = false;
    std::string mLogPath;
    std::ofstream mFile;
    std::mutex mMutex;
};

// 日志宏：Logger 禁用时几乎零开销（条件编译不够，用 if 短路）
#define FEIQ_LOG(expr) \
    do { \
        if (Logger::instance().isEnabled()) { \
            std::ostringstream _oss; \
            _oss << expr; \
            Logger::instance().log(_oss.str()); \
        } \
    } while(0)

#endif // LOGGER_H
