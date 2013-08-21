#ifndef HEADER_PIPELINE_CONTAINER
#define HEADER_PIPELINE_CONTAINER

#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Msg.h>

EXCEPTION(CodecStreamCorrupt);

namespace OpenHome {
namespace Media {
namespace Codec {

/*
Element which strips any container data from the start of a file.
Could (should) be extended later to handle more complex containers
...which read a stream, skip some data, read another stream repeatedly
...not just at the start of a file.
*/

class IContainer
{
public:
    virtual void Read(Bwx& aBuf, TUint aOffset, TUint aBytes) = 0;
};


class IRecogniser
{
public:
    virtual TBool Recognise(Brx& aBuf) = 0;
};

class IContainerBase : public IRecogniser, public IPipelineElementUpstream, public IStreamHandler
{
public: // from IRecogniser
    virtual TBool Recognise(Brx& aBuf) = 0;
public: // from IPipelineElementUpstream
    virtual Msg* Pull() = 0;
public: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId) = 0;
    TUint TrySeek(TUint aTrackId, TUint aStreamId, TUint64 aOffset) = 0;
    TUint TryStop(TUint aTrackId, TUint aStreamId) = 0;
};

class ContainerBase : public IContainerBase, private IMsgProcessor, private INonCopyable
{
    friend class Container;
public:
    ContainerBase();
    ~ContainerBase();
protected:
    Msg* PullMsg();
    void AddToAudioEncoded(MsgAudioEncoded* aMsg);
    void ReleaseAudioEncoded();
    void PullAudio(TUint aBytes);
    void Read(Bwx& aBuf, TUint aBytes);
private:
    void Construct(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, IStreamHandler& aStreamHandler);
    TBool ReadFromCachedAudio(Bwx& aBuf, TUint aBytes);
public: // from IRecogniser
    //TBool Recognise(Brx& aBuf) = 0;   // need to reset inner container in this method
    TBool Recognise(Brx& aBuf);
public: // from IPipelineElementUpstream
    Msg* Pull();
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgAudioEncoded* aMsg);
    Msg* ProcessMsg(MsgAudioPcm* aMsg);
    Msg* ProcessMsg(MsgSilence* aMsg);
    Msg* ProcessMsg(MsgPlayable* aMsg);
    Msg* ProcessMsg(MsgDecodedStream* aMsg);
    Msg* ProcessMsg(MsgTrack* aMsg);
    Msg* ProcessMsg(MsgEncodedStream* aMsg);
    Msg* ProcessMsg(MsgMetaText* aMsg);
    Msg* ProcessMsg(MsgHalt* aMsg);
    Msg* ProcessMsg(MsgFlush* aMsg);
    Msg* ProcessMsg(MsgQuit* aMsg);
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId);
    TUint TrySeek(TUint aTrackId, TUint aStreamId, TUint64 aOffset);
    TUint TryStop(TUint aTrackId, TUint aStreamId);
protected:
    MsgAudioEncoded* iAudioEncoded;
    IStreamHandler* iStreamHandler;
    TUint iExpectedFlushId;
private:
    MsgFactory* iMsgFactory;
    IPipelineElementUpstream* iUpstreamElement;
    TByte iReadBuf[EncodedAudio::kMaxBytes];
    MsgQueue iMsgQueue;
    Msg* iPendingMsg;
    TBool iQuit;

protected:
    IContainer* iContainer;
};

class ContainerNull : public ContainerBase
{
public:
    ContainerNull();
public: // from IRecogniser
    TBool Recognise(Brx& aBuf);
};

class Container;

class ContainerFront : public IPipelineElementUpstream, public IStreamHandler, private INonCopyable
{
public:
    ContainerFront(Container& aContainer, IMsgProcessor& aMsgProcessor, IPipelineElementUpstream& aUpstreamElement);
public: // from IPipelineElementUpstream
    Msg* Pull();
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId);
    TUint TrySeek(TUint aTrackId, TUint aStreamId, TUint64 aOffset);
    TUint TryStop(TUint aTrackId, TUint aStreamId);
private:
    Container& iContainer;
    IMsgProcessor& iMsgProcessor;
    IPipelineElementUpstream& iUpstreamElement;
    TUint iAccumulator;
    TUint iExpectedFlushId;
};

class Container : public IPipelineElementUpstream, private IMsgProcessor, private IStreamHandler, private INonCopyable
{
    friend class ContainerFront;
public:
    Container(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement);
    virtual ~Container();
    void AddContainer(ContainerBase* aContainer);
public: // from IPipelineElementUpstream
    Msg* Pull();
private: // IMsgProcessor
    Msg* ProcessMsg(MsgAudioEncoded* aMsg);
    Msg* ProcessMsg(MsgAudioPcm* aMsg);
    Msg* ProcessMsg(MsgSilence* aMsg);
    Msg* ProcessMsg(MsgPlayable* aMsg);
    Msg* ProcessMsg(MsgDecodedStream* aMsg);
    Msg* ProcessMsg(MsgTrack* aMsg);
    Msg* ProcessMsg(MsgEncodedStream* aMsg);
    Msg* ProcessMsg(MsgMetaText* aMsg);
    Msg* ProcessMsg(MsgHalt* aMsg);
    Msg* ProcessMsg(MsgFlush* aMsg);
    Msg* ProcessMsg(MsgQuit* aMsg);
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aTrackId, TUint aStreamId);
    TUint TrySeek(TUint aTrackId, TUint aStreamId, TUint64 aOffset);
    TUint TryStop(TUint aTrackId, TUint aStreamId);
private:
    static const TUint kMaxRecogniseBytes = 6 * 1024;
    MsgFactory& iMsgFactory;
    ContainerFront* iContainerFront;
    std::vector<IContainerBase*> iContainers;
    IContainerBase* iActiveContainer;
    ContainerNull* iContainerNull;
    IStreamHandler* iStreamHandler;
    //Msg* iPendingMsg;
    //TBool iQuit;
    TBool iRecognising;
    MsgAudioEncoded* iAudioEncoded;
    TByte iReadBuf[EncodedAudio::kMaxBytes];
    TUint iExpectedFlushId;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_CONTAINER
