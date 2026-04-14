#ifndef FELLOW_H
#define FELLOW_H

#include <string>
#include <memory>
#include <sstream>
using namespace std;

enum class AbsenceStatus {
    Online = 0,     // 在线
    Away = 1,       // 离开
    Busy = 2,       // 忙碌
    Offline = 3     // 离线
};

class Fellow
{
public:
    string getIp() const{return mIp;}
    string getName() const{return mName.empty() ? mPcName : mName;}
    string getHost() const{return mHost;}
    string getMac() const{return mMac;}
    bool isOnLine() const{return mOnLine;}
    string version() const{return mVersion;}
    AbsenceStatus absenceStatus() const{return mAbsenceStatus;}
    bool isSelf() const{return mIsSelf;}
    void setIsSelf(bool v){mIsSelf = v;}

    void setIp(const string& value){
        mIp = value;
    }

    void setName(const string& value){
        mName = value;
    }

    void setHost(const string& value){
        mHost = value;
    }

    void setMac(const string& value){
        mMac = value;
    }

    void setOnLine(bool value){
        mOnLine = value;
        if (!value) mAbsenceStatus = AbsenceStatus::Offline;
        else if (mAbsenceStatus == AbsenceStatus::Offline) mAbsenceStatus = AbsenceStatus::Online;
    }

    void setVersion(const string& value){
        mVersion = value;
    }

    void setPcName(const string& value){
        mPcName = value;
    }

    void setAbsenceStatus(AbsenceStatus status){
        mAbsenceStatus = status;
        if (status == AbsenceStatus::Offline)
            mOnLine = false;
        else
            mOnLine = true;
    }

    bool update(const Fellow& fellow)
    {
        bool changed = false;

        if (!fellow.mName.empty() && mName != fellow.mName){
            mName = fellow.mName;
            changed=true;
        }

        if (!fellow.mMac.empty() && mMac != fellow.mMac){
            mMac = fellow.mMac;
            changed=true;
        }

        if (mOnLine != fellow.mOnLine){
            mOnLine = fellow.mOnLine;
            changed=true;
        }

        if (mAbsenceStatus != fellow.mAbsenceStatus){
            mAbsenceStatus = fellow.mAbsenceStatus;
            changed=true;
        }

        return changed;
    }

    bool operator == (const Fellow& fellow)
    {
        return isSame(fellow);
    }

    bool isSame(const Fellow& fellow)
    {
        return mIp == fellow.mIp || (!mMac.empty() && mMac == fellow.mMac);
    }

    string toString() const
    {
        ostringstream os;
        os<<"["
         <<"ip="<<mIp
        <<",name="<<mName
        <<",host="<<mHost
        <<",pcname="<<mPcName
        <<",mac="<<mMac
        <<",online="<<mOnLine
        <<",absence="<<static_cast<int>(mAbsenceStatus)
        <<",version="<<mVersion
        <<"]";
        return os.str();
    }

    static string absenceStatusStr(AbsenceStatus status)
    {
        switch (status) {
        case AbsenceStatus::Online: return "在线";
        case AbsenceStatus::Away: return "离开";
        case AbsenceStatus::Busy: return "忙碌";
        case AbsenceStatus::Offline: return "离线";
        }
        return "未知";
    }

private:
    string mIp;
    string mPcName;
    string mName;
    string mHost;
    string mMac;
    bool mOnLine = false;
    string mVersion;
    AbsenceStatus mAbsenceStatus = AbsenceStatus::Online;
    bool mIsSelf = false;
};

#endif // FELLOW_H
