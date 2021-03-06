#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/NtpClient.h>

EXCEPTION(UnixTimestampUnavailable);

namespace OpenHome {

class Environment;
class Timer;

class IUnixTimestamp
{
public:
    virtual ~IUnixTimestamp() {}
    virtual TUint Now() = 0;
    virtual void Reset() = 0;
};

class UnixTimestamp : public IUnixTimestamp
{
    static const TUint kSecsBetweenNtpAndUnixEpoch = 2208988800; // secs between 1900 and 1970
public:
    UnixTimestamp(Environment& aEnv);
    ~UnixTimestamp();
public: // from IUnixTimestamp
    TUint Now() override;
    void Reset() override;
private:
    void TimestampExpired();
private:
    Environment& iEnv;
    Mutex iLock;
    NtpClient iNtpClient;
    Timer* iTimer;
    TBool iTimestampValid;
    TUint iStartSeconds;
    TUint iStartMs;
};

} // namespace OpenHome

