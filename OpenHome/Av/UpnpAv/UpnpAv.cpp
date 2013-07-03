#include <OpenHome/Av/UpnpAv/UpnpAv.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Av/UpnpAv/ProviderAvTransport.h>
#include <OpenHome/Av/UpnpAv/ProviderConnectionManager.h>
#include <OpenHome/Av/UpnpAv/ProviderRenderingControl.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/UriProviderSingleTrack.h>

#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

ISource* SourceFactory::NewUpnpAv(IMediaPlayer& aMediaPlayer, Net::DvDevice& aDevice, const TChar* aSupportedProtocols)
{ // static
    UriProviderSingleTrack* uriProvider = new UriProviderSingleTrack("UpnpAv", aMediaPlayer.TrackFactory());
    aMediaPlayer.Add(uriProvider);
    return new SourceUpnpAv(aMediaPlayer.Env(), aDevice, aMediaPlayer.Pipeline(), *uriProvider, aSupportedProtocols);
}

// UpnpAv

SourceUpnpAv::SourceUpnpAv(Environment& aEnv, Net::DvDevice& aDevice, PipelineManager& aPipeline, UriProviderSingleTrack& aUriProvider, const TChar* aSupportedProtocols)
    : Source("UpnpAv", "UpnpAv")
    , iLock("UPAV")
    , iDevice(aDevice)
    , iPipeline(aPipeline)
    , iUriProvider(aUriProvider)
    , iTrack(NULL)
    , iPipelineTrackId(UINT_MAX)
    , iStreamId(UINT_MAX)
    , iTransportState(Media::EPipelineStopped)
{
    iActive = true; /* FIXME - Kinsky doesn't cope with this source so we don't register it with the Product.
                       Fool IsActive() checks below by saying this is always active.  See #169 */
    iProviderAvTransport = new ProviderAvTransport(iDevice, aEnv, *this);
    iProviderConnectionManager = new ProviderConnectionManager(iDevice, aSupportedProtocols);
    iProviderRenderingControl = new ProviderRenderingControl(iDevice);
    iDownstreamObserver = iProviderAvTransport;
}

SourceUpnpAv::~SourceUpnpAv()
{
    ASSERT(!iDevice.Enabled());
    delete iProviderAvTransport;
    delete iProviderConnectionManager;
    delete iProviderRenderingControl;
}

void SourceUpnpAv::Activate()
{
    iActive = true;
}

void SourceUpnpAv::Deactivate()
{
    iLock.Wait();
    iTransportState = Media::EPipelineStopped;
    if (iTrack != NULL) {
        iTrack->RemoveRef();
        iTrack = NULL;
    }
    iLock.Signal();
    Source::Deactivate();
}

void SourceUpnpAv::SetTrack(const Brx& aUri, const Brx& aMetaData)
{
    if (!IsActive()) {
        DoActivate();
    }
    if (iTrack == NULL || iTrack->Uri() != aUri) {
        iPipeline.RemoveAll();
        if (iTrack != NULL) {
            iTrack->RemoveRef();
        }
        iTrack = iUriProvider.SetTrack(aUri, aMetaData);
        iPipeline.Begin(iUriProvider.Mode(), iTrack->Id());
        if (iTransportState == Media::EPipelinePlaying) {
            iPipeline.Play();
        }
    }
}

void SourceUpnpAv::Play()
{
    if (!IsActive()) {
        DoActivate();
    }
    iLock.Wait();
    iTransportState = Media::EPipelinePlaying;
    iLock.Signal();
    iPipeline.Play();
}

void SourceUpnpAv::Pause()
{
    if (IsActive()) {
        iLock.Wait();
        iTransportState = Media::EPipelinePaused;
        iLock.Signal();
        iPipeline.Pause();
    }
}

void SourceUpnpAv::Stop()
{
    if (IsActive()) {
        iLock.Wait();
        iTransportState = Media::EPipelineStopped;
        iLock.Signal();
        iPipeline.Stop();
    }
}

void SourceUpnpAv::Next()
{
    if (IsActive()) {
        iPipeline.Stop(); // we only store a single track so have nothing to move forward to
    }
}

void SourceUpnpAv::Prev()
{
    if (IsActive()) {
        iPipeline.Stop(); // we only store a single track so have nothing to move back to
    }
}

void SourceUpnpAv::Seek(TUint aSecondsAbsolute)
{
    if (IsActive()) {
        (void)iPipeline.Seek(iPipelineTrackId, iStreamId, aSecondsAbsolute);
    }
}

void SourceUpnpAv::NotifyPipelineState(EPipelineState aState)
{
    if (IsActive()) {
        iDownstreamObserver->NotifyPipelineState(aState);
    }
}

void SourceUpnpAv::NotifyTrack(Track& aTrack, const Brx& aMode, TUint aIdPipeline)
{
    iLock.Wait();
    iPipelineTrackId = aIdPipeline;
    iStreamId = UINT_MAX;
    if (iTrack != NULL) {
        iTrack->RemoveRef();
    }
    iTrack = &aTrack;
    iLock.Signal();
    if (IsActive()) {
        iDownstreamObserver->NotifyTrack(aTrack, aMode, aIdPipeline);
    }
}

void SourceUpnpAv::NotifyMetaText(const Brx& aText)
{
    if (IsActive()) {
        iDownstreamObserver->NotifyMetaText(aText);
    }
}

void SourceUpnpAv::NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds)
{
    if (IsActive()) {
        iDownstreamObserver->NotifyTime(aSeconds, aTrackDurationSeconds);
    }
}

void SourceUpnpAv::NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo)
{
    iLock.Wait();
    iStreamId = aStreamInfo.StreamId();
    iLock.Signal();
    if (IsActive()) {
        iDownstreamObserver->NotifyStreamInfo(aStreamInfo);
    }
}
