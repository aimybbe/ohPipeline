#include <OpenHome/Av/ProviderInfo.h>

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

ProviderInfo::ProviderInfo(DvDevice& aDevice, PipelineManager& aPipelineManager)
    : DvProviderAvOpenhomeOrgInfo1(aDevice)
    , iPipelineManager(aPipelineManager)
    , iLock("PrIn")
    , iTrackUri(Brx::Empty())
    , iMetaData(Brx::Empty())
    , iCodecName(Brx::Empty())
    , iMetaText(Brx::Empty())
{
    EnablePropertyTrackCount();
    EnablePropertyDetailsCount();
    EnablePropertyMetatextCount();
    EnablePropertyUri();
    EnablePropertyMetadata();
    EnablePropertyDuration();
    EnablePropertyBitRate();
    EnablePropertyBitDepth();
    EnablePropertySampleRate();
    EnablePropertyLossless();
    EnablePropertyCodecName();
    EnablePropertyMetatext();

    SetPropertyTrackCount(0);
    ClearStreamInfo(Brx::Empty(), Brx::Empty());

    EnableActionCounters();
    EnableActionTrack();
    EnableActionDetails();
    EnableActionMetatext();

    iPipelineManager.AddObserver(*this);
}

ProviderInfo::~ProviderInfo()
{
}

void ProviderInfo::ClearStreamInfo(const Brx& aTrackUri, const Brx& aMetaData)
{
    iTrackUri.Replace(aTrackUri);
    SetPropertyUri(iTrackUri);
    iMetaData.Replace(aMetaData);
    SetPropertyMetadata(iMetaData);
    SetPropertyDetailsCount(0);
    SetPropertyDuration(0);
    SetPropertyBitRate(0);
    SetPropertyBitDepth(0);
    SetPropertySampleRate(0);
    SetPropertyLossless(false);
    iCodecName.Replace(Brx::Empty());
    SetPropertyCodecName(iCodecName);
    SetPropertyMetatextCount(0);
    iMetaText.Replace(Brx::Empty());
    SetPropertyMetatext(iMetaText);
}

void ProviderInfo::Counters(IDvInvocation& aInvocation, IDvInvocationResponseUint& aTrackCount, IDvInvocationResponseUint& aDetailsCount, IDvInvocationResponseUint& aMetatextCount)
{
    TUint propTrackCount = 0;
    TUint propDetailsCount = 0;
    TUint propMetaTextCount = 0;

    AutoMutex mutex(iLock);

    GetPropertyTrackCount(propTrackCount);
    GetPropertyDetailsCount(propDetailsCount);
    GetPropertyMetatextCount(propMetaTextCount);

    aInvocation.StartResponse();
    aTrackCount.Write(propTrackCount);
    aDetailsCount.Write(propDetailsCount);
    aMetatextCount.Write(propMetaTextCount);
    aInvocation.EndResponse();
}

void ProviderInfo::Track(IDvInvocation& aInvocation, IDvInvocationResponseString& aUri, IDvInvocationResponseString& aMetadata)
{
    AutoMutex mutex(iLock);

    aInvocation.StartResponse();
    aUri.Write(iTrackUri);
    aUri.WriteFlush();
    aMetadata.Write(iMetaData);
    aMetadata.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderInfo::Details(IDvInvocation& aInvocation, IDvInvocationResponseUint& aDuration, IDvInvocationResponseUint& aBitRate, IDvInvocationResponseUint& aBitDepth, IDvInvocationResponseUint& aSampleRate, IDvInvocationResponseBool& aLossless, IDvInvocationResponseString& aCodecName)
{
    TUint propDuration = 0;
    TUint propBitRate = 0;
    TUint propBitDepth = 0;
    TUint propSampleRate = 0;
    TBool propLossless = false;

    AutoMutex mutex(iLock);

    GetPropertyDuration(propDuration);
    GetPropertyBitRate(propBitRate);
    GetPropertyBitDepth(propBitDepth);
    GetPropertySampleRate(propSampleRate);
    GetPropertyLossless(propLossless);

    aInvocation.StartResponse();
    aDuration.Write(propDuration);
    aBitRate.Write(propBitRate);
    aBitDepth.Write(propBitDepth);
    aSampleRate.Write(propSampleRate);
    aLossless.Write(propLossless);
    aCodecName.Write(iCodecName);
    aCodecName.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderInfo::Metatext(IDvInvocation& aInvocation, IDvInvocationResponseString& aValue)
{
    AutoMutex mutex(iLock);

    aInvocation.StartResponse();
    aValue.Write(iMetaText);
    aValue.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderInfo::NotifyPipelineState(EPipelineState /*aState*/)
{
    // NOP -- playing, paused, stopped, buffering
}

void ProviderInfo::NotifyTrack(Media::Track& aTrack, const Brx& /*aMode*/, TUint /*aIdPipeline*/)
{
    TUint n = 0;
    AutoMutex mutex(iLock);
    PropertiesLock();
    GetPropertyTrackCount(n);
    SetPropertyTrackCount(n + 1);
    ClearStreamInfo(aTrack.Uri(), aTrack.MetaData());
    PropertiesUnlock();
}

void ProviderInfo::NotifyMetaText(const Brx& aText)
{
    TUint n = 0;

    AutoMutex mutex(iLock);

    PropertiesLock();
    GetPropertyMetatextCount(n);
    SetPropertyMetatextCount(n + 1);
    iMetaText.Replace(aText);
    SetPropertyMetatext(iMetaText);
    PropertiesUnlock();
}

void ProviderInfo::NotifyTime(TUint /*aSeconds*/, TUint aTrackDurationSeconds)
{
    AutoMutex mutex(iLock);

    PropertiesLock();
    if (SetPropertyDuration(aTrackDurationSeconds)) {
        // actual change in track duration, not just 1Hz tick
        TUint n = 0;
        GetPropertyDetailsCount(n);
        SetPropertyDetailsCount(n + 1);
    }
    PropertiesUnlock();
}

void ProviderInfo::NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo)
{
    TUint n = 0;

    AutoMutex mutex(iLock);

    PropertiesLock();
    GetPropertyDetailsCount(n);
    SetPropertyDetailsCount(n + 1);
    SetPropertyBitRate(aStreamInfo.BitRate());
    SetPropertyBitDepth(aStreamInfo.BitDepth());
    SetPropertySampleRate(aStreamInfo.SampleRate());
    SetPropertyLossless(aStreamInfo.Lossless());
    iCodecName.Replace(aStreamInfo.CodecName());
    SetPropertyCodecName(iCodecName);
    PropertiesUnlock();
}
