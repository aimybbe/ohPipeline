#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Protocol/ProtocolHls.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Media/Supply.h>
#include <OpenHome/Media/Tests/TestProtocolHls.h>

#include <algorithm>

namespace OpenHome {
namespace Media {

class TimerGeneric : public ITimer
{
public:
    TimerGeneric(Environment& aEnv, const TChar* aId);
    ~TimerGeneric();
private: // from ITimer
    void Start(TUint aDurationMs, ITimerHandler& aHandler) override;
    void Cancel() override;
private:
    void TimerFired();
private:
    Timer* iTimer;
    ITimerHandler* iHandler;
    TBool iPending;
    Mutex iLock;
};

class SemaphoreGeneric : public ISemaphore
{
public:
    SemaphoreGeneric(const TChar* aName, TUint aCount);
public: // from ISemaphore
    void Wait() override;
    TBool Clear() override;
    void Signal() override;
private:
    Semaphore iSem;
};

class HlsReader : public IHlsReader
{
public:
    HlsReader(Environment& aEnv, const Brx& aUserAgent);
private: // from IHlsReader
    IHttpSocket& Socket() override;
    IReader& Reader() override;
public:
    HttpReader iReader;
};

class ProtocolHls : public Protocol
{
public:
    ProtocolHls(Environment& aEnv, IHlsReader* aReaderM3u, IHlsReader* aReaderSegment);
    ~ProtocolHls();
private: // from Protocol
    void Initialise(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstream) override;
    void Interrupt(TBool aInterrupt) override;
    ProtocolStreamResult Stream(const Brx& aUri) override;
    ProtocolGetResult Get(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes) override;
    void Deactivated() override;
public: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryStop(TUint aStreamId) override;
private:
    void Reinitialise();
    void StartStream(const Uri& aUri);
    TBool IsCurrentStream(TUint aStreamId) const;
private:
    IHlsReader* iHlsReaderM3u;
    IHlsReader* iHlsReaderSegment;
    Supply* iSupply;
    TimerGeneric iTimer;
    SemaphoreGeneric iSemReaderM3u;
    HlsM3uReader iM3uReader;
    SegmentStreamer iSegmentStreamer;
    TUint iStreamId;
    TBool iStarted;
    TBool iStopped;
    ContentProcessor* iContentProcessor;
    TUint iNextFlushId;
    Semaphore iSem;
    Mutex iLock;
};

};  // namespace Media
};  // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;


Protocol* ProtocolFactory::NewHls(Environment& aEnv, const Brx& aUserAgent)
{ // static
    HlsReader* readerM3u = new HlsReader(aEnv, aUserAgent);
    HlsReader* readerSegment = new HlsReader(aEnv, aUserAgent);
    return new ProtocolHls(aEnv, readerM3u, readerSegment);
}


// For test purposes.
Protocol* HlsTestFactory::NewTestableHls(Environment& aEnv, IHlsReader* aReaderM3u, IHlsReader* aReaderSegment)
{ // static
    return new ProtocolHls(aEnv, aReaderM3u, aReaderSegment);
};


// TimerGeneric

TimerGeneric::TimerGeneric(Environment& aEnv, const TChar* aId)
    : iHandler(NULL)
    , iPending(false)
    , iLock("TIGL")
{
    iTimer = new Timer(aEnv, MakeFunctor(*this, &TimerGeneric::TimerFired), aId);
}

TimerGeneric::~TimerGeneric()
{
    delete iTimer;
}

void TimerGeneric::Start(TUint aDurationMs, ITimerHandler& aHandler)
{
    AutoMutex a(iLock);
    ASSERT(!iPending);  // Can't set timer when it's already pending.
    iPending = true;
    iHandler = &aHandler;
    iTimer->FireIn(aDurationMs);
}

void TimerGeneric::Cancel()
{
    AutoMutex a(iLock);
    //ASSERT(iPending);
    iPending = false;
    iTimer->Cancel();
}

void TimerGeneric::TimerFired()
{
    AutoMutex a(iLock);
    if (iPending) {
        iPending = false;
        iHandler->TimerFired(); // FIXME - problem if iHandler calls back into ::Start() during TimerFired()
    }
}


// SemaphoreGeneric

SemaphoreGeneric::SemaphoreGeneric(const TChar* aName, TUint aCount)
    : iSem(aName, aCount)
{
}

void SemaphoreGeneric::Wait()
{
    iSem.Wait();
}

TBool SemaphoreGeneric::Clear()
{
    return iSem.Clear();
}

void SemaphoreGeneric::Signal()
{
    return iSem.Signal();
}


// HlsReader

HlsReader::HlsReader(Environment& aEnv, const Brx& aUserAgent)
    : iReader(aEnv, aUserAgent)
{
}

IHttpSocket& HlsReader::Socket()
{
    return iReader;
}

IReader& HlsReader::Reader()
{
    return iReader;
}


// HlsM3uReader

HlsM3uReader::HlsM3uReader(IHttpSocket& aSocket, IReader& aReader, ITimer& aTimer, ISemaphore& aSemaphore)
    : iTimer(aTimer)
    , iSocket(aSocket)
    , iReaderUntil(aReader)
    , iConnected(false)
    , iTotalBytes(0)
    , iVersion(2)
    , iLastSegment(0)
    , iTargetDuration(0)
    , iEndlist(false)
    , iStreamEnded(false)
    , iLock("HMRL")
    , iSem(aSemaphore)
    , iInterrupted(true)
    , iError(false)
{
}

void HlsM3uReader::SetUri(const Uri& aUri)
{
    LOG(kMedia, ">HlsM3uReader::SetUri ");
    LOG(kMedia, aUri.AbsoluteUri());
    LOG(kMedia, "\n");
    AutoMutex a(iLock);
    ASSERT(iInterrupted);   // Interrupt() should be called before re-calling SetUri().
    // iInterrupted is set to true at construction, so will be true on first call to SetUri().
    ASSERT(!iConnected);
    iUri.Replace(aUri.AbsoluteUri());
    iLastSegment = 0;
    iTargetDuration = 0;
    iEndlist = false;
    iStreamEnded = false;
    iNextLine.Set(Brx::Empty());
    iSem.Clear();   // Clear any pending signals from last run.
    iSem.Signal();
    iInterrupted = false;
    iError = false;
    //iReaderUntil.ReadFlush();
}

TUint HlsM3uReader::Version() const
{
    return iVersion;
}

TBool HlsM3uReader::StreamEnded() const
{
    return iStreamEnded;
}

TBool HlsM3uReader::Error() const
{
    return iError;
}

void HlsM3uReader::Close()
{
    // It is responsibility of client of this class to call Interrupt() prior
    // to this (if class is active).
    AutoMutex a(iLock);
    if (iConnected) {
        iReaderUntil.ReadFlush();
        iConnected = false;
        iSocket.Close();
    }
}

TUint HlsM3uReader::NextSegmentUri(Uri& aUri)
{
    LOG(kMedia, ">HlsM3uReader::NextSegmentUri\n");
    TUint duration = 0;
    Brn segmentUri = Brx::Empty();
    try {
        TBool expectUri = false;
        while (segmentUri == Brx::Empty()) {
            Brn uri;
            if ((iLastSegment == 0 && iTargetDuration == 0) || iOffset >= iTotalBytes) {
                if (!iEndlist) {
                    if (!ReloadVariantPlaylist()) {
                        LOG(kMedia, "HlsM3uReader::NextSegmentUri unable to reload variant playlist\n");
                        iError = true;
                        THROW(HlsVariantPlaylistError);
                    }
                }
                else {
                    iStreamEnded = true;
                    THROW(HlsEndOfStream);
                }
            }

            if (iNextLine == Brx::Empty()) {
                ReadNextLine();
            }
            if (expectUri) {
                segmentUri = Ascii::Trim(iNextLine);
                expectUri = false;
                LOG(kMedia, "HlsM3uReader::NextSegmentUri segmentUri: ");
                LOG(kMedia, segmentUri);
                LOG(kMedia, "\n");
                iLastSegment++;
            }
            else {
                Parser p(iNextLine);
                Brn tag = p.Next(':');
                if (tag == Brn("#EXTINF")) {
                    Brn durationBuf = p.Next(',');
                    Parser durationParser(durationBuf);
                    Brn durationWhole = durationParser.Next('.');
                    duration = Ascii::Uint(durationWhole) * kMillisecondsPerSecond;
                    if (!durationParser.Finished()) {
                        // Looks like duration is a float.
                        // Duration is only guaranteed to be int in version 2 and below
                        Brn durationDecimalBuf = durationParser.Next();
                        if (!durationParser.Finished() && durationDecimalBuf.Bytes()>3) {
                            // Error in M3U8 format.
                            LOG(kMedia, "HlsM3uReader::NextSegmentUri error while parsing duration of next segment. durationDecimalBuf: ");
                            LOG(kMedia, durationDecimalBuf);
                            LOG(kMedia, "\n");
                            iError = true;
                            THROW(HlsVariantPlaylistError);
                        }
                        TUint durationDecimal = Ascii::Uint(durationDecimalBuf);
                        duration += durationDecimal;
                    }
                    LOG(kMedia, "HlsM3uReader::NextSegmentUri duration: %u\n", duration);
                    expectUri = true;
                }
                else if (tag == Brn("#EXT-X-ENDLIST")) {
                    iEndlist = true;
                }
            }
            iNextLine = Brx::Empty();
        }

        try {
            SetSegmentUri(aUri, segmentUri);
        }
        catch (UriError&) {
            LOG(kMedia, "HlsM3uReader::NextSegmentUri UriError\n");
            iError = true;
            THROW(HlsVariantPlaylistError);
        }
    }
    catch (AsciiError&) {
        LOG(kMedia, "HlsM3uReader::NextSegmentUri AsciiError\n");
        iError = true;
        THROW(HlsVariantPlaylistError);
    }
    catch (HttpError&) {
        LOG(kMedia, "HlsM3uReader::NextSegmentUri HttpError\n");
        iError = true;
        THROW(HlsVariantPlaylistError);
    }
    catch (ReaderError&) {
        LOG(kMedia, "HlsM3uReader::NextSegmentUri ReaderError\n");
        iError = true;
        THROW(HlsVariantPlaylistError);
    }

    return duration;
}

void HlsM3uReader::TimerFired()
{
    LOG(kMedia, "HlsM3uReader::TimerFired\n");
    iSem.Signal();
}

void HlsM3uReader::Interrupt()
{
    LOG(kMedia, "HlsM3uReader::Interrupt\n");
    // Must NOT close socket here - undefined behaviour will result.
    AutoMutex a(iLock);
    if (!iInterrupted) {
        iInterrupted = true;
        iTimer.Cancel();
        if (iConnected) {
            iReaderUntil.ReadInterrupt();
        }
        iSem.Signal();
    }
}

void HlsM3uReader::ReadNextLine()
{
    // May throw ReaderError.
    iNextLine = iReaderUntil.ReadUntil('\n');
    iOffset += iNextLine.Bytes()+1;  // Separator has been trimmed.
}

TBool HlsM3uReader::ReloadVariantPlaylist()
{
    LOG(kMedia, "HlsM3uReader::ReloadVariantPlaylist\n");
    // Timer should be started BEFORE refreshing playlist.
    // However, not very useful if we don't yet have target duration, so just
    // start timer after processing part of playlist.

    iSem.Wait();

    {
        AutoMutex a(iLock);
        if (iInterrupted) {
            LOG(kMedia, "HlsM3uReader::ReloadVariantPlaylist interrupted while waiting to poll playlist\n");
            return false;
        }
    }

    Close();
    TBool success = iSocket.Connect(iUri);
    if (success) {
        {
            AutoMutex a(iLock);
            iConnected = true;
        }
        iTotalBytes = iSocket.ContentLength();
        iOffset = 0;
    }
    else {
        LOG(kMedia, "HlsM3uReader::ReloadVariantPlaylist unable to (re-)connect\n");
        return false;
    }

    try {
        if (!PreprocessM3u()) {
            LOG(kMedia, "HlsM3uReader::ReloadVariantPlaylist failed to pre-process M3U8\n");
            return false;
        }
    }
    catch (AsciiError&) {
        LOG(kMedia, "HlsM3uReader::NextSegmentUri AsciiError\n");
        return false;
    }
    catch (HttpError&) {
        LOG(kMedia, "HlsM3uReader::NextSegmentUri HttpError\n");
        return false;
    }
    catch (ReaderError&) {
        LOG(kMedia, "HlsM3uReader::NextSegmentUri ReaderError\n");
        return false;
    }

    if (iOffset >= iTotalBytes) {
        LOG(kMedia, "HlsM3uReader::ReloadVariantPlaylist exhausted file\n");
        return false;
    }

    if (iTargetDuration == 0) { // #EXT-X-TARGETDURATION is a required tag.
        LOG(kMedia, "HlsM3uReader::ReloadVariantPlaylist malformed file\n");
        return false;
    }

    // Hold lock to ensure timer can't be set if Interrupt() is called during this method.
    AutoMutex a(iLock);
    if (iInterrupted) {
        LOG(kMedia, "HlsM3uReader::ReloadVariantPlaylist interrupted while reloading playlist. Not setting timer.\n");
        return false;
    }
    iTimer.Start(iTargetDuration*kMillisecondsPerSecond, *this);
    LOG(kMedia, "<HlsM3uReader::ReloadVariantPlaylist\n");
    return true;
}

TBool HlsM3uReader::PreprocessM3u()
{
    TUint64 skipSegments = 0;
    TBool mediaSeqFound = false;
    try {
        while (iOffset < iTotalBytes) {
            ReadNextLine();
            Parser p(iNextLine);
            Brn tag = p.Next(':');

            if (tag == Brn("#EXT-X-VERSION")) {
                iVersion = Ascii::Uint(p.Next());
                if (iVersion > kMaxM3uVersion) {
                    LOG(kMedia, "Unsupported M3U version. Max supported version: %u, version encountered: %u\n", kMaxM3uVersion, iVersion);
                }
                LOG(kMedia, "HlsM3uReader::PreprocessM3u iVersion: %u\n", iVersion);
            }
            if (tag == Brn("#EXT-X-MEDIA-SEQUENCE")) {
                mediaSeqFound = true;
                // If this isn't found, it must be assumed that first segment in playlist is 0.
                TUint64 mediaSeq = Ascii::Uint64(p.Next());
                if (iLastSegment == 0) {
                    iLastSegment = mediaSeq;
                    skipSegments = 0;
                }
                else if (mediaSeq <= iLastSegment) {
                    skipSegments = (iLastSegment-mediaSeq);
                }
                LOG(kMedia, "HlsM3uReader::PreprocessM3u mediaSeq: %llu\n", mediaSeq);
            }
            else if (tag == Brn("#EXT-X-TARGETDURATION")) {
                iTargetDuration = Ascii::Uint(p.Next());
                LOG(kMedia, "HlsM3uReader::PreprocessM3u iTargetDuration: %u\n", iTargetDuration);
            }
            else if (tag == Brn("#EXT-X-ENDLIST")) {
                iEndlist = true;
                LOG(kMedia, "HlsM3uReader::PreprocessM3u found #EXT-X-ENDLIST\n");
            }
            else if (tag == Brn("#EXTINF")) {
                if (!mediaSeqFound) {
                    // EXT-X-MEDIA-SEQUENCE MUST appear before EXTINF, so must
                    // have seen it by now if present.
                    skipSegments = iLastSegment;
                    mediaSeqFound = true;   // Don't want to enter this block again.
                }

                if (skipSegments > 0) {
                    skipSegments--;
                }
                else {
                    // Found start/continuation of audio.
                    // iNextLine will remain populated with this "#EXTINF" line
                    // for starting parsing of outstanding segments elsewhere.
                    LOG(kMedia, "HlsM3uReader::PreprocessM3u found start/continuation of audio segments\n");
                    return true;
                }
            }
        }
    }
    catch (AsciiError&) {
        LOG(kMedia, "HlsM3uReader::PreprocessM3u AsciiError\n");
        return false;
    }
    catch (HttpError&) {
        LOG(kMedia, "HlsM3uReader::PreprocessM3u HttpError\n");
        return false;
    }
    catch (ReaderError&) {
        LOG(kMedia, "HlsM3uReader::PreprocessM3u ReaderError\n");
        return false;
    }
    LOG(kMedia, "HlsM3uReader::PreprocessM3u exhausted file without finding new segments. iEndlist: %u\n", iEndlist);
    return false;
}

void HlsM3uReader::SetSegmentUri(Uri& aUri, const Brx& aSegmentUri)
{
    // Segment URI MAY be relative.
    // If it is relative, it is relative to URI of playlist that contains it.
    static const Brn kSchemeHttp("http");

    if (aSegmentUri.Bytes() > kSchemeHttp.Bytes()
            && Brn(aSegmentUri.Ptr(), kSchemeHttp.Bytes()) == kSchemeHttp) {
        // Segment URI is absolute.

        // May throw UriError.
        aUri.Replace(aSegmentUri);
    }
    else {
        // Segment URI is relative.
        Bws<Uri::kMaxUriBytes> uriBuf;
        uriBuf.Replace(iUri.Scheme());
        uriBuf.Append("://");
        uriBuf.Append(iUri.Host());
        TInt port = iUri.Port();
        if (port > 0) {
            uriBuf.Append(":");
            Ascii::AppendDec(uriBuf, iUri.Port());
        }

        // Get URI path minus file.
        Parser uriParser(iUri.Path());
        while (!uriParser.Finished()) {
            Brn fragment = uriParser.Next('/');
            if (!uriParser.Finished()) {
                uriBuf.Append(fragment);
                uriBuf.Append("/");
            }
        }

        // May throw UriError.
        aUri.Replace(uriBuf, aSegmentUri);
    }
}


// SegmentStreamer

SegmentStreamer::SegmentStreamer(IHttpSocket& aSocket, IReader& aReader)
    : iSocket(aSocket)
    , iReader(aReader)
    , iSegmentUriProvider(NULL)
    , iConnected(false)
    , iTotalBytes(0)
    , iOffset(0)
    , iInterrupted(true)
    , iError(false)
    , iLock("SEGL")
{
}

void SegmentStreamer::Stream(ISegmentUriProvider& aSegmentUriProvider)
{
    LOG(kMedia, "SegmentStreamer::Stream\n");
    AutoMutex a(iLock);
    ASSERT(iInterrupted);
    ASSERT(!iConnected);
    iInterrupted = false;
    iError = false;
    iSegmentUriProvider = &aSegmentUriProvider;
    iTotalBytes = 0;
    iOffset = 0;
}

TBool SegmentStreamer::Error() const
{
    return iError;
}

Brn SegmentStreamer::Read(TUint aBytes)
{
    try {
        EnsureSegmentIsReady();
    }
    catch (HlsSegmentError&) {
        LOG(kMedia, "SegmentStreamer::Read HlsSegmentError\n");
        THROW(ReaderError);
    }
    catch (HlsEndOfStream&) {
        LOG(kMedia, "SegmentStreamer::Read HlsEndOfStream\n");
        THROW(ReaderError);
    }
    Brn buf = iReader.Read(aBytes);
    iOffset += buf.Bytes();
    return buf;
}

void SegmentStreamer::ReadFlush()
{
    iReader.ReadFlush();
}

void SegmentStreamer::ReadInterrupt()
{
    LOG(kMedia, "SegmentStreamer::ReadInterrupt\n");
    AutoMutex a(iLock);
    if (!iInterrupted) {
        iInterrupted = true;
        if (iConnected) {
            iReader.ReadInterrupt();
        }
    }
}

void SegmentStreamer::Close()
{
    LOG(kMedia, "SegmentStreamer::Close\n");
    AutoMutex a(iLock);
    if (iConnected) {
        iConnected = false;
        iSocket.Close();
    }
}

void SegmentStreamer::GetNextSegment()
{
    LOG(kMedia, ">SegmentStreamer::GetNextSegment\n");
    Uri segment;
    try {
        (void)iSegmentUriProvider->NextSegmentUri(segment);
    }
    catch (HlsVariantPlaylistError&) {
        LOG(kMedia, "SegmentStreamer::GetNextSegment HlsVariantPlaylistError\n");
        iError = true;
        THROW(HlsSegmentError);
    }
    catch (HlsEndOfStream&) {
        LOG(kMedia, "SegmentStreamer::GetNextSegment HlsEndOfStream\n");
        throw;
    }

    iUri.Replace(segment.AbsoluteUri());

    Close();
    TBool success = iSocket.Connect(iUri);
    if (!success) {
        iError = true;
        THROW(HlsSegmentError);
    }
    {
        AutoMutex a(iLock);
        iConnected = true;
    }
    iTotalBytes = iSocket.ContentLength();
    iOffset = 0;
}

void SegmentStreamer::EnsureSegmentIsReady()
{
    // FIXME - what if iTotalBytes == 0?
    if (iOffset == iTotalBytes) {
        GetNextSegment();
    }
}


// ProtocolHls

ProtocolHls::ProtocolHls(Environment& aEnv, IHlsReader* aReaderM3u, IHlsReader* aReaderSegment)
    : Protocol(aEnv)
    , iHlsReaderM3u(aReaderM3u)
    , iHlsReaderSegment(aReaderSegment)
    , iSupply(NULL)
    , iTimer(iEnv, "PHLS")
    , iSemReaderM3u("HMRS", 0)
    , iM3uReader(iHlsReaderM3u->Socket(), iHlsReaderM3u->Reader(), iTimer, iSemReaderM3u)
    , iSegmentStreamer(iHlsReaderSegment->Socket(), iHlsReaderSegment->Reader())
    , iSem("PRTH", 0)
    , iLock("PRHL")
{
}

ProtocolHls::~ProtocolHls()
{
    delete iSupply;
    delete iHlsReaderSegment;
    delete iHlsReaderM3u;
}

void ProtocolHls::Initialise(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstream)
{
    iSupply = new Supply(aMsgFactory, aDownstream);
}

void ProtocolHls::Interrupt(TBool aInterrupt)
{
    iLock.Wait();
    if (iActive) {
        LOG(kMedia, "ProtocolHls::Interrupt(%u)\n", aInterrupt);
        if (aInterrupt) {
            iStopped = true;
        }
        iSegmentStreamer.ReadInterrupt();
        iM3uReader.Interrupt();
    }
    iLock.Signal();
}

ProtocolStreamResult ProtocolHls::Stream(const Brx& aUri)
{
    // There is no notion of a live or seekable stream in HLS!
    //
    // By default, all streams are live.
    //
    // It is legal to perform a seek, as long as it is within the current
    // stream segments available within the variant playlist.
    // (i.e., if the first available segment was some_stream-002.ts, and the
    // user wished to seek to a position that would be in some_stream-001.ts,
    // that seek would be invalid.)
    //
    // It is also legal to attempt to pause an HLS stream (albeit that the
    // position at which it can resume is bounded by the segments available in
    // the variant playlist).
    // (i.e., if paused during some_stream-002.ts and when unpaused first
    // segment now available was some_stream-004.ts, there would be a forced
    // discontinuity in the audio.)
    //
    // Given the limited usefulness of this behaviour (because it is bound by
    // the limits of the periodically changing variant playlist), the use case
    // (why would a client wish to seek during a live radio stream?), and the
    // increased complexity of the code required, just don't allow
    // seeking/pausing.



    Reinitialise();
    Uri uriHls(aUri);
    LOG(kMedia, "ProtocolHls::Stream ");
    LOG(kMedia, uriHls.AbsoluteUri());
    LOG(kMedia, "\n");

    if (uriHls.Scheme() != Brn("hls")) {
        LOG(kMedia, "ProtocolHls::Stream scheme not recognised\n");
        return EProtocolErrorNotSupported;
    }

    if (!iStarted) {
        StartStream(uriHls);
    }

    // Don't want to buffer content from a live stream
    // ...so need to wait on pipeline signalling it is ready to play
    LOG(kMedia, "ProtocolHls::Stream live stream waiting to be (re-)started\n");
    iSegmentStreamer.Close();
    iM3uReader.Close();
    iSem.Wait();
    LOG(kMedia, "ProtocolHls::Stream live stream restart\n");

    // Convert hls:// scheme to http:// scheme
    const Brx& uriHlsBuf = uriHls.AbsoluteUri();
    Parser p(uriHlsBuf);
    p.Next(':');    // skip "hls" scheme
    Bws<Uri::kMaxUriBytes> uriHttpBuf("http:");
    uriHttpBuf.Append(p.NextToEnd());
    Uri uriHttp(uriHttpBuf);    // may throw UriError

    //iSegmentStreamer.ReadInterrupt();
    //iM3uReader.Interrupt();
    iM3uReader.SetUri(uriHttp);
    iSegmentStreamer.Stream(iM3uReader);

    if (iContentProcessor == NULL) {
        iContentProcessor = iProtocolManager->GetAudioProcessor();
    }

    ProtocolStreamResult res = EProtocolStreamErrorRecoverable;
    while (res == EProtocolStreamErrorRecoverable) {
        if (iStopped) {
            res = EProtocolStreamStopped;
            break;
        }

        // This will only return EProtocolStreamErrorRecoverable for live streams!
        res = iContentProcessor->Stream(iSegmentStreamer, 0);

        // Check for context of above method returning.
        // i.e., identify whether it was actually caused by:
        //  - TryStop() being called                    (EProtocolStreamStopped)
        //  - end of stream indicated in M3U8           (EProtocolStreamSuccess)
        //  - unrecoverable error (e.g. malformed M3U8) (EProtocolStreamErrorUnrecoverable)
        //  - recoverable interruption in stream        (EProtocolStreamErrorRecoverable)

        if (iStopped) {
            res = EProtocolStreamStopped;
            break;
        }
        else if (iM3uReader.StreamEnded()) {
            res = EProtocolStreamSuccess;
            break;
        }
        else if (iM3uReader.Error() || iSegmentStreamer.Error()) {
            res = EProtocolStreamErrorUnrecoverable;
            break;
        }
    }

    // Streaming helpers MUST be interrupted before being Close()d/restarted.
    iSegmentStreamer.ReadInterrupt();
    iM3uReader.Interrupt();
    iSegmentStreamer.Close();
    iM3uReader.Close();

    {
        AutoMutex a(iLock);
        if (iStopped && iNextFlushId != MsgFlush::kIdInvalid) {
            iSupply->OutputFlush(iNextFlushId);
        }
        // Clear iStreamId to prevent TrySeek or TryStop returning a valid flush id.
        iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    }

    LOG(kMedia, "<ProtocolHls::Stream res: %d\n", res);
    return res;
}

ProtocolGetResult ProtocolHls::Get(IWriter& /*aWriter*/, const Brx& /*aUri*/, TUint64 /*aOffset*/, TUint /*aBytes*/)
{
    return EProtocolGetErrorNotSupported;
}

void ProtocolHls::Deactivated()
{
    if (iContentProcessor != NULL) {
        iContentProcessor->Reset();
        iContentProcessor = NULL;
    }
    iSegmentStreamer.Close();
    iM3uReader.Close();
}

EStreamPlay ProtocolHls::OkToPlay(TUint aStreamId)
{
    LOG(kMedia, "> ProtocolHls::OkToPlay(%u)\n", aStreamId);
    const EStreamPlay canPlay = iIdProvider->OkToPlay(aStreamId);
    if (canPlay != ePlayNo && iStreamId == aStreamId) {
        iSem.Signal();
    }
    LOG(kMedia, "< ProtocolHls::OkToPlay(%u) == %s\n", aStreamId, kStreamPlayNames[canPlay]);
    return canPlay;
}

TUint ProtocolHls::TrySeek(TUint /*aStreamId*/, TUint64 /*aOffset*/)
{
    LOG(kMedia, "ProtocolHls::TrySeek\n");
    return MsgFlush::kIdInvalid;
}

TUint ProtocolHls::TryStop(TUint aStreamId)
{
    iLock.Wait();
    const TBool stop = IsCurrentStream(aStreamId);
    if (stop) {
        if (iNextFlushId == MsgFlush::kIdInvalid) {
            /* If a valid flushId is set then We've previously promised to send a Flush but haven't
            got round to it yet.  Re-use the same id for any other requests that come in before
            our main thread gets a chance to issue a Flush */
            iNextFlushId = iFlushIdProvider->NextFlushId();
        }
        iStopped = true;
        iSegmentStreamer.ReadInterrupt();
        iM3uReader.Interrupt();
        iSem.Signal();
    }
    iLock.Signal();
    return (stop? iNextFlushId : MsgFlush::kIdInvalid);
}

void ProtocolHls::Reinitialise()
{
    LOG(kMedia, "ProtocolHls::Reinitialise\n");
    iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    iStarted = iStopped = false;
    iContentProcessor = NULL;
    iNextFlushId = MsgFlush::kIdInvalid;
    (void)iSem.Clear();
}

void ProtocolHls::StartStream(const Uri& aUri)
{
    LOG(kMedia, "ProtocolHls::StartStream\n");
    TBool totalBytes = 0;
    TBool seekable = false;
    TBool live = true;
    iStreamId = iIdProvider->NextStreamId();
    iSupply->OutputStream(aUri.AbsoluteUri(), totalBytes, seekable, live, *this, iStreamId);
    iStarted = true;
}

TBool ProtocolHls::IsCurrentStream(TUint aStreamId) const
{
    if (iStreamId != aStreamId || aStreamId == IPipelineIdProvider::kStreamIdInvalid) {
        return false;
    }
    return true;
}
