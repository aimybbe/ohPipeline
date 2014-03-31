#ifndef HEADER_PROTOCOL_OHBASE
#define HEADER_PROTOCOL_OHBASE

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Msg.h>
#include <OpenHome/Av/Songcast/OhmMsg.h>
#include <OpenHome/Av/Songcast/OhmSocket.h>
#include <OpenHome/Av/Songcast/OhmTimestamp.h>
#include <OpenHome/Private/Stream.h>

#include <vector>

namespace OpenHome {
    class Environment;
    class Timer;
namespace Av {

class OhmMsgAudioBlob;
class OhmMsg;

class ProtocolOhBase : public Media::Protocol, private IOhmMsgProcessor
{
    static const TUint kMaxRepairBacklogFrames = 200;
    static const TUint kMaxRepairMissedFrames = 20;
    static const TUint kInitialRepairTimeoutMs = 10;
    static const TUint kSubsequentRepairTimeoutMs = 30;
    static const TUint kTimerJoinTimeoutMs = 300;
    static const TUint kTtl = 2;
protected:
    ProtocolOhBase(Environment& aEnv, IOhmMsgFactory& aFactory, Media::TrackFactory& aTrackFactory, IOhmTimestamper& aTimestamper, const TChar* aSupportedScheme, const Brx& aMode);
    ~ProtocolOhBase();
    void Add(OhmMsg* aMsg);
    void ResendSeen();
    void RequestResend(const Brx& aFrames);
    void SendJoin();
    void SendListen();
    void Send(TUint aType);
private:
    virtual Media::ProtocolStreamResult Play(TIpAddress aInterface, TUint aTtl, const Endpoint& aEndpoint) = 0;
protected: // from Media::Protocol
    void Interrupt(TBool aInterrupt);
private: // from Media::Protocol
    Media::ProtocolStreamResult Stream(const Brx& aUri);
private: // from IStreamHandler
    Media::EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId);
private:
    void CurrentSubnetChanged();
    void RepairReset();
    void TimerRepairExpired();
    TBool RepairBegin(OhmMsgAudioBlob& aMsg);
    TBool Repair(OhmMsgAudioBlob& aMsg);
    void OutputAudio(OhmMsgAudioBlob& aMsg);
private: // from IOhmMsgProcessor
    void Process(OhmMsgAudio& aMsg);
    void Process(OhmMsgAudioBlob& aMsg);
    void Process(OhmMsgTrack& aMsg);
    void Process(OhmMsgMetatext& aMsg);
protected:
    static const TUint kMaxFrameBytes = 16*1024;
    static const TUint kTimerListenTimeoutMs = 10000;
protected:
    Environment& iEnv;
    IOhmMsgFactory& iMsgFactory;
    OhmSocket iSocket;
    Srs<kMaxFrameBytes> iReadBuffer;
    Endpoint iEndpoint;
    Timer* iTimerJoin;
    Timer* iTimerListen;
private:
    Mutex iMutexTransport;
    Media::TrackFactory& iTrackFactory;
    IOhmTimestamper& iTimestamper;
    Brn iSupportedScheme;
    Media::BwsMode iMode;
    TUint iNacnId;
    Uri iUri; // only used inside Stream() but too large to put on the stack
    TUint iFrame;
    TBool iRunning;
    TBool iRepairing;
    TBool iStreamMsgDue;
    OhmMsgAudioBlob* iRepairFirst;
    std::vector<OhmMsgAudioBlob*> iRepairFrames;
    Timer* iTimerRepair;
    Bws<Media::EncodedAudio::kMaxBytes> iFrameBuf;
    TUint iAddr; // FIXME - should listen for subnet changes and update this value
    Media::BwsTrackUri iTrackUri;
};

} // namespace Av
} // namespace OpenHome

#endif // HEADER_PROTOCOL_OHBASE
