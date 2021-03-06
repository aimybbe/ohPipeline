#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/ElementObserver.h>
#include <OpenHome/Media/Pipeline/AudioDumper.h>
#include <OpenHome/Media/Pipeline/EncodedAudioReservoir.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/Id3v2.h>
#include <OpenHome/Media/Codec/Mpeg4.h>
#include <OpenHome/Media/Codec/MpegTs.h>
#include <OpenHome/Media/Pipeline/DecodedAudioValidator.h>
#include <OpenHome/Media/Pipeline/DecodedAudioAggregator.h>
#include <OpenHome/Media/Pipeline/SampleRateValidator.h>
#include <OpenHome/Media/Pipeline/DecodedAudioReservoir.h>
#include <OpenHome/Media/Pipeline/Ramper.h>
#include <OpenHome/Media/Pipeline/RampValidator.h>
#include <OpenHome/Media/Pipeline/Seeker.h>
#include <OpenHome/Media/Pipeline/VariableDelay.h>
#include <OpenHome/Media/Pipeline/TrackInspector.h>
#include <OpenHome/Media/Pipeline/Skipper.h>
#include <OpenHome/Media/Pipeline/Stopper.h>
#include <OpenHome/Media/Pipeline/Reporter.h>
#include <OpenHome/Media/Pipeline/SpotifyReporter.h>
#include <OpenHome/Media/Pipeline/Router.h>
#include <OpenHome/Media/Pipeline/Drainer.h>
#include <OpenHome/Media/Pipeline/Pruner.h>
#include <OpenHome/Media/Pipeline/Attenuator.h>
#include <OpenHome/Media/Pipeline/Logger.h>
#include <OpenHome/Media/Pipeline/StarvationRamper.h>
#include <OpenHome/Media/Pipeline/Muter.h>
#include <OpenHome/Media/Pipeline/AnalogBypassRamper.h>
#include <OpenHome/Media/Pipeline/PreDriver.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Debug.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Media;

// PipelineInitParams

PipelineInitParams* PipelineInitParams::New()
{ // static
    return new PipelineInitParams();
}

PipelineInitParams::PipelineInitParams()
    : iEncodedReservoirBytes(kEncodedReservoirSizeBytes)
    , iDecodedReservoirJiffies(kDecodedReservoirSize)
    , iGorgeDurationJiffies(kGorgerSizeDefault)
    , iStarvationRamperMinJiffies(kStarvationRamperSizeDefault)
    , iMaxStreamsPerReservoir(kMaxReservoirStreamsDefault)
    , iRampLongJiffies(kLongRampDurationDefault)
    , iRampShortJiffies(kShortRampDurationDefault)
    , iRampEmergencyJiffies(kEmergencyRampDurationDefault)
    , iMaxLatencyJiffies(kMaxLatencyDefault)
    , iSupportElements(EPipelineSupportElementsAll)
    , iMuter(kMuterDefault)
{
    SetThreadPriorityMax(kThreadPriorityMax);
}

PipelineInitParams::~PipelineInitParams()
{
}

void PipelineInitParams::SetEncodedReservoirSize(TUint aBytes)
{
    iEncodedReservoirBytes = aBytes;
}

void PipelineInitParams::SetDecodedReservoirSize(TUint aJiffies)
{
    iDecodedReservoirJiffies = aJiffies;
}

void PipelineInitParams::SetGorgerDuration(TUint aJiffies)
{
    iGorgeDurationJiffies = aJiffies;
}

void PipelineInitParams::SetStarvationRamperMinSize(TUint aJiffies)
{
    iStarvationRamperMinJiffies = aJiffies;
}

void PipelineInitParams::SetMaxStreamsPerReservoir(TUint aCount)
{
    iMaxStreamsPerReservoir = aCount;
}

void PipelineInitParams::SetLongRamp(TUint aJiffies)
{
    iRampLongJiffies = aJiffies;
}

void PipelineInitParams::SetShortRamp(TUint aJiffies)
{
    iRampShortJiffies = aJiffies;
}

void PipelineInitParams::SetEmergencyRamp(TUint aJiffies)
{
    iRampEmergencyJiffies = aJiffies;
}

void PipelineInitParams::SetThreadPriorityMax(TUint aPriority)
{
    iThreadPriorityStarvationRamper = aPriority;
    iThreadPriorityCodec            = iThreadPriorityStarvationRamper - 1;
    iThreadPriorityEvent            = iThreadPriorityCodec - 1;
}

void PipelineInitParams::SetThreadPriorities(TUint aStarvationRamper, TUint aCodec, TUint aEvent)
{
    iThreadPriorityStarvationRamper = aStarvationRamper;
    iThreadPriorityCodec            = aCodec;
    iThreadPriorityEvent            = aEvent;
}

void PipelineInitParams::SetMaxLatency(TUint aJiffies)
{
    iMaxLatencyJiffies = aJiffies;
}

void PipelineInitParams::SetSupportElements(TUint aElements)
{
    iSupportElements = aElements;
}

void PipelineInitParams::SetMuter(MuterImpl aMuter)
{
    iMuter = aMuter;
}

TUint PipelineInitParams::EncodedReservoirBytes() const
{
    return iEncodedReservoirBytes;
}

TUint PipelineInitParams::DecodedReservoirJiffies() const
{
    return iDecodedReservoirJiffies;
}

TUint PipelineInitParams::GorgeDurationJiffies() const
{
   return iGorgeDurationJiffies;
}

TUint PipelineInitParams::StarvationRamperMinJiffies() const
{
    return iStarvationRamperMinJiffies;
}

TUint PipelineInitParams::MaxStreamsPerReservoir() const
{
    return iMaxStreamsPerReservoir;
}

TUint PipelineInitParams::RampLongJiffies() const
{
    return iRampLongJiffies;
}

TUint PipelineInitParams::RampShortJiffies() const
{
    return iRampShortJiffies;
}

TUint PipelineInitParams::RampEmergencyJiffies() const
{
    return iRampEmergencyJiffies;
}

TUint PipelineInitParams::ThreadPriorityStarvationRamper() const
{
    return iThreadPriorityStarvationRamper;
}

TUint PipelineInitParams::ThreadPriorityCodec() const
{
    return iThreadPriorityCodec;
}

TUint PipelineInitParams::ThreadPriorityEvent() const
{
    return iThreadPriorityEvent;
}

TUint PipelineInitParams::MaxLatencyJiffies() const
{
    return iMaxLatencyJiffies;
}

TUint PipelineInitParams::SupportElements() const
{
    return iSupportElements;
}

PipelineInitParams::MuterImpl PipelineInitParams::Muter() const
{
    return iMuter;
}


// Pipeline

#define ATTACH_ELEMENT(elem, ctor, prev_elem, supported, type)  \
    do {                                                        \
        if ((supported & (type)) ||                             \
            (type) == EPipelineSupportElementsMandatory) {      \
            elem = ctor;                                        \
            prev_elem = elem;                                   \
        }                                                       \
        else {                                                  \
            elem = nullptr;                                     \
        }                                                       \
    } while (0)

static Pipeline* gPipeline = nullptr;
Pipeline::Pipeline(PipelineInitParams* aInitParams, IInfoAggregator& aInfoAggregator, TrackFactory& aTrackFactory, IPipelineObserver& aObserver,
                   IStreamPlayObserver& aStreamPlayObserver, ISeekRestreamer& aSeekRestreamer, IUrlBlockWriter& aUrlBlockWriter)
    : iInitParams(aInitParams)
    , iObserver(aObserver)
    , iLock("PLMG")
    , iState(EStopped)
    , iLastReportedState(EPipelineStateCount)
    , iBuffering(false)
    , iWaiting(false)
    , iQuitting(false)
    , iNextFlushId(MsgFlush::kIdInvalid + 1)
{
    const TUint perStreamMsgCount = aInitParams->MaxStreamsPerReservoir() * kReservoirCount;
    TUint encodedAudioCount = ((aInitParams->EncodedReservoirBytes() + EncodedAudio::kMaxBytes - 1) / EncodedAudio::kMaxBytes); // this may only be required on platforms that don't guarantee priority based thread scheduling
    encodedAudioCount = std::max(encodedAudioCount, // songcast and some hardware inputs won't use the full capacity of each encodedAudio
                                 (kReceiverMaxLatency + kSongcastFrameJiffies - 1) / kSongcastFrameJiffies);
    const TUint maxEncodedReservoirMsgs = encodedAudioCount;
    encodedAudioCount += kRewinderMaxMsgs; // this may only be required on platforms that don't guarantee priority based thread scheduling
    const TUint msgEncodedAudioCount = encodedAudioCount + 100; // +100 allows for Split()ing by Container and CodecController
    const TUint decodedReservoirSize = aInitParams->DecodedReservoirJiffies() + aInitParams->StarvationRamperMinJiffies();
    const TUint decodedAudioCount = ((decodedReservoirSize + kSenderMinLatency) / DecodedAudioAggregator::kMaxJiffies) + 200; // +200 allows for songcast sender, some smaller msgs and some buffering in non-reservoir elements
    const TUint msgAudioPcmCount = decodedAudioCount + 100; // +100 allows for Split()ing in various elements
    const TUint msgHaltCount = perStreamMsgCount * 2; // worst case is tiny Vorbis track with embedded metatext in a single-track playlist with repeat
    MsgFactoryInitParams msgInit;
    msgInit.SetMsgModeCount(kMsgCountMode);
    msgInit.SetMsgTrackCount(perStreamMsgCount);
    msgInit.SetMsgDrainCount(kMsgCountDrain);
    msgInit.SetMsgDelayCount(perStreamMsgCount);
    msgInit.SetMsgEncodedStreamCount(perStreamMsgCount);
    msgInit.SetMsgAudioEncodedCount(msgEncodedAudioCount, encodedAudioCount);
    msgInit.SetMsgMetaTextCount(perStreamMsgCount);
    msgInit.SetMsgStreamInterruptedCount(perStreamMsgCount);
    msgInit.SetMsgHaltCount(msgHaltCount);
    msgInit.SetMsgFlushCount(kMsgCountFlush);
    msgInit.SetMsgWaitCount(perStreamMsgCount);
    msgInit.SetMsgDecodedStreamCount(perStreamMsgCount);
    msgInit.SetMsgAudioPcmCount(msgAudioPcmCount, decodedAudioCount);
    msgInit.SetMsgSilenceCount(kMsgCountSilence);
    msgInit.SetMsgPlayableCount(kMsgCountPlayablePcm, kMsgCountPlayableSilence);
    msgInit.SetMsgQuitCount(kMsgCountQuit);
    iMsgFactory = new MsgFactory(aInfoAggregator, msgInit);

    iEventThread = new PipelineElementObserverThread(aInitParams->ThreadPriorityEvent());
    IPipelineElementDownstream* downstream = nullptr;
    IPipelineElementUpstream* upstream = nullptr;
    const auto elementsSupported = aInitParams->SupportElements();

    // disable "conditional expression is constant" warnings from ATTACH_ELEMENT
#ifdef _WIN32
# pragma warning( push )
# pragma warning( disable : 4127)
#endif // _WIN32

    // Construct encoded reservoir out of sequence.  It doesn't pull from the left so doesn't need to know its preceding element
    iEncodedAudioReservoir = new EncodedAudioReservoir(*iMsgFactory, *this, maxEncodedReservoirMsgs, aInitParams->MaxStreamsPerReservoir());
    upstream = iEncodedAudioReservoir;
    ATTACH_ELEMENT(iLoggerEncodedAudioReservoir, new Logger(*upstream, "Encoded Audio Reservoir"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);

    // Construct audio dumper out of sequence. It doesn't pull from left so doesn't need to know it's preceding element (but it does need to know the element it's pushing to).
    downstream = iEncodedAudioReservoir;
    ATTACH_ELEMENT(iAudioDumper, new AudioDumper(*iEncodedAudioReservoir),
                   downstream, elementsSupported, EPipelineSupportElementsAudioDumper);
    iPipelineStart = iAudioDumper;
    if (iPipelineStart == nullptr) {
        iPipelineStart = iEncodedAudioReservoir;
    }

    const TBool createLoggers = (elementsSupported & EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iContainer, new Codec::ContainerController(*iMsgFactory, *upstream, aUrlBlockWriter, createLoggers),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerContainer, new Logger(*iContainer, "Codec Container"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);

    // Construct decoded reservoir out of sequence.  It doesn't pull from the left so doesn't need to know its preceding element
    iDecodedAudioReservoir = new DecodedAudioReservoir(*iMsgFactory, *this,
                                                       aInitParams->DecodedReservoirJiffies(),
                                                       aInitParams->MaxStreamsPerReservoir(),
                                                       aInitParams->GorgeDurationJiffies());
    downstream = iDecodedAudioReservoir;

    ATTACH_ELEMENT(iLoggerDecodedAudioAggregator,
                   new Logger("Decoded Audio Aggregator", *iDecodedAudioReservoir),
                   downstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iDecodedAudioAggregator, new DecodedAudioAggregator(*downstream),
                   downstream, elementsSupported, EPipelineSupportElementsMandatory);

    ATTACH_ELEMENT(iLoggerSampleRateValidator, new Logger("Sample Rate Validator", *iDecodedAudioAggregator),
                   downstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iSampleRateValidator, new SampleRateValidator(*iMsgFactory, *downstream),
                   downstream, elementsSupported, EPipelineSupportElementsMandatory);

    // construct push logger slightly out of sequence
    ATTACH_ELEMENT(iRampValidatorCodec, new RampValidator("Codec Controller", *iSampleRateValidator),
                   downstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iLoggerCodecController, new Logger("Codec Controller", *downstream),
                   downstream, elementsSupported, EPipelineSupportElementsLogger);
    iCodecController = new Codec::CodecController(*iMsgFactory, *upstream, *downstream, aUrlBlockWriter,
                                                  kSongcastFrameJiffies, aInitParams->ThreadPriorityCodec(),
                                                  createLoggers);

    upstream = iDecodedAudioReservoir;
    ATTACH_ELEMENT(iLoggerDecodedAudioReservoir,
                   new Logger(*iDecodedAudioReservoir, "Decoded Audio Reservoir"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRamper, new Ramper(*upstream, aInitParams->RampLongJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerRamper, new Logger(*iRamper, "Ramper"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorRamper, new RampValidator(*upstream, "Ramper"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iSeeker, new Seeker(*iMsgFactory, *upstream, *iCodecController, aSeekRestreamer, aInitParams->RampShortJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerSeeker, new Logger(*iSeeker, "Seeker"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorSeeker, new RampValidator(*upstream, "Seeker"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorSeeker, new DecodedAudioValidator(*upstream, "Seeker"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iVariableDelay1,
                   new VariableDelayLeft(*iMsgFactory, *upstream,
                                         aInitParams->RampEmergencyJiffies(), kSenderMinLatency),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerVariableDelay1, new Logger(*iVariableDelay1, "VariableDelay1"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorDelay1, new RampValidator(*upstream, "VariableDelay1"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorDelay1, new DecodedAudioValidator(*upstream, "VariableDelay1"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iSkipper, new Skipper(*iMsgFactory, *upstream, aInitParams->RampShortJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerSkipper, new Logger(*iSkipper, "Skipper"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorSkipper, new RampValidator(*upstream, "Skipper"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorSkipper, new DecodedAudioValidator(*upstream, "Skipper"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iTrackInspector, new TrackInspector(*upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerTrackInspector, new Logger(*iTrackInspector, "TrackInspector"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iWaiter, new Waiter(*iMsgFactory, *upstream, *this, *iEventThread, aInitParams->RampShortJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerWaiter, new Logger(*iWaiter, "Waiter"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorWaiter, new RampValidator(*upstream, "Waiter"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorWaiter, new DecodedAudioValidator(*upstream, "Waiter"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iStopper, new Stopper(*iMsgFactory, *upstream, *this, *iEventThread, aInitParams->RampLongJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    iStopper->SetStreamPlayObserver(aStreamPlayObserver);
    ATTACH_ELEMENT(iLoggerStopper, new Logger(*iStopper, "Stopper"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorStopper, new RampValidator(*upstream, "Stopper"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorStopper, new DecodedAudioValidator(*upstream, "Stopper"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iSpotifyReporter, new Media::SpotifyReporter(*upstream, *iMsgFactory, aTrackFactory),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerSpotifyReporter, new Logger(*iSpotifyReporter, "SpotifyReporter"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iReporter, new Reporter(*upstream, *this, *iEventThread),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerReporter, new Logger(*iReporter, "Reporter"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRouter, new Router(*upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerRouter, new Logger(*iRouter, "Router"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iAttenuator, new Attenuator(*upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerAttenuator, new Logger(*iAttenuator, "Attenuator"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iDecodedAudioValidatorRouter, new DecodedAudioValidator(*upstream, "Router"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iDrainer, new Drainer(*iMsgFactory, *upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerDrainer, new Logger(*iDrainer, "Drainer"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iVariableDelay2,
                   new VariableDelayRight(*iMsgFactory, *upstream,
                                          aInitParams->RampEmergencyJiffies(),
                                          aInitParams->StarvationRamperMinJiffies()),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    iVariableDelay1->SetObserver(*iVariableDelay2);
    ATTACH_ELEMENT(iLoggerVariableDelay2, new Logger(*iVariableDelay2, "VariableDelay2"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorDelay2, new RampValidator(*upstream, "VariableDelay2"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator);
    ATTACH_ELEMENT(iDecodedAudioValidatorDelay2, new DecodedAudioValidator(*upstream, "VariableDelay2"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iPruner, new Pruner(*upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerPruner, new Logger(*iPruner, "Pruner"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iDecodedAudioValidatorPruner, new DecodedAudioValidator(*upstream, "Pruner"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    ATTACH_ELEMENT(iStarvationRamper,
                   new StarvationRamper(*iMsgFactory, *upstream, *this, *iEventThread,
                                        aInitParams->StarvationRamperMinJiffies(),
                                        aInitParams->ThreadPriorityStarvationRamper(),
                                        aInitParams->RampShortJiffies(), aInitParams->MaxStreamsPerReservoir()),
                                        upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerStarvationRamper, new Logger(*iStarvationRamper, "StarvationRamper"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iRampValidatorStarvationRamper, new RampValidator(*upstream, "StarvationRamper"),
                   upstream, elementsSupported, EPipelineSupportElementsRampValidator | EPipelineSupportElementsValidatorMinimal);
    ATTACH_ELEMENT(iDecodedAudioValidatorStarvationRamper,
                   new DecodedAudioValidator(*upstream, "StarvationRamper"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator);
    IMute* muter = nullptr;
    if (aInitParams->Muter() == PipelineInitParams::MuterImpl::eRampSamples) {
        ATTACH_ELEMENT(iMuterSamples, new Muter(*iMsgFactory, *upstream, aInitParams->RampLongJiffies()),
                       upstream, elementsSupported, EPipelineSupportElementsMandatory);
        muter = iMuterSamples;
        iMuterVolume = nullptr;
        ATTACH_ELEMENT(iLoggerMuter, new Logger(*iMuterSamples, "Muter"),
                       upstream, elementsSupported, EPipelineSupportElementsLogger);
    }
    else {
        ATTACH_ELEMENT(iMuterVolume, new MuterVolume(*iMsgFactory, *upstream),
                       upstream, elementsSupported, EPipelineSupportElementsMandatory);
        muter = iMuterVolume;
        iMuterSamples = nullptr;
        ATTACH_ELEMENT(iLoggerMuter, new Logger(*iMuterVolume, "Muter"),
                       upstream, elementsSupported, EPipelineSupportElementsLogger);
    }
    ATTACH_ELEMENT(iDecodedAudioValidatorMuter, new DecodedAudioValidator(*upstream, "Muter"),
                   upstream, elementsSupported, EPipelineSupportElementsDecodedAudioValidator | EPipelineSupportElementsValidatorMinimal);
    ATTACH_ELEMENT(iAnalogBypassRamper, new AnalogBypassRamper(*iMsgFactory, *upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    ATTACH_ELEMENT(iLoggerAnalogBypassRamper, new Logger(*iAnalogBypassRamper, "AnalogBypassRamper"),
                   upstream, elementsSupported, EPipelineSupportElementsLogger);
    ATTACH_ELEMENT(iPreDriver, new PreDriver(*upstream),
                   upstream, elementsSupported, EPipelineSupportElementsMandatory);
    iLoggerPreDriver = new Logger(*iPreDriver, "PreDriver");

#ifdef _WIN32
# pragma warning( pop )
#endif // _WIN32

    iPipelineEnd = iLoggerPreDriver;
    if (iPipelineEnd == nullptr) {
        iPipelineEnd = iPreDriver;
    }
    iMuteCounted = new MuteCounted(*muter);

    gPipeline = this;

    //iAudioDumper->SetEnabled(true);

    //iLoggerEncodedAudioReservoir->SetEnabled(true);
    //iLoggerContainer->SetEnabled(true);
    //iLoggerCodecController->SetEnabled(true);
    //iLoggerSampleRateValidator->SetEnabled(true);
    //iLoggerDecodedAudioAggregator->SetEnabled(true);
    //iLoggerDecodedAudioReservoir->SetEnabled(true);
    //iLoggerRamper->SetEnabled(true);
    //iLoggerSeeker->SetEnabled(true);
    //iLoggerVariableDelay1->SetEnabled(true);
    //iLoggerSkipper->SetEnabled(true);
    //iLoggerTrackInspector->SetEnabled(true);
    //iLoggerWaiter->SetEnabled(true);
    //iLoggerStopper->SetEnabled(true);
    //iLoggerSpotifyReporter->SetEnabled(true);
    //iLoggerReporter->SetEnabled(true);
    //iLoggerRouter->SetEnabled(true);
    //iLoggerAttenuator->SetEnabled(true);
    //iLoggerDrainer->SetEnabled(true);
    //iLoggerDelayReservoir->SetEnabled(true);
    //iLoggerVariableDelay2->SetEnabled(true);
    //iLoggerPruner->SetEnabled(true);
    //iLoggerStarvationRamper->SetEnabled(true);
    //iLoggerMuter->SetEnabled(true);
    //iLoggerAnalogBypassRamper->SetEnabled(true);

    // A logger that is enabled will block waiting for MsgQuit in its dtor
    // ~Pipeline (below) relies on this to synchronise its destruction
    // i.e. NEVER DISABLE THIS LOGGER
    iLoggerPreDriver->SetEnabled(true);

    //iLoggerEncodedAudioReservoir->SetFilter(Logger::EMsgAll);
    //iLoggerContainer->SetFilter(Logger::EMsgAll);
    //iLoggerCodecController->SetFilter(Logger::EMsgAll);
    //iLoggerSampleRateValidator->SetFilter(Logger::EMsgAll);
    //iLoggerDecodedAudioAggregator->SetFilter(Logger::EMsgAll);
    //iLoggerDecodedAudioReservoir->SetFilter(Logger::EMsgAll);
    //iLoggerRamper->SetFilter(Logger::EMsgAll);
    //iLoggerSeeker->SetFilter(Logger::EMsgAll);
    //iLoggerVariableDelay1->SetFilter(Logger::EMsgAll);
    //iLoggerSkipper->SetFilter(Logger::EMsgAll);
    //iLoggerTrackInspector->SetFilter(Logger::EMsgAll);
    //iLoggerWaiter->SetFilter(Logger::EMsgAll);
    //iLoggerStopper->SetFilter(Logger::EMsgAll);
    //iLoggerSpotifyReporter->SetFilter(Logger::EMsgAll);
    //iLoggerReporter->SetFilter(Logger::EMsgAll);
    //iLoggerRouter->SetFilter(Logger::EMsgAll);
    //iLoggerAttenuator->SetFilter(Logger::EMsgAll);
    //iLoggerDrainer->SetFilter(Logger::EMsgAll);
    //iLoggerDelayReservoir->SetFilter(Logger::EMsgAll);
    //iLoggerVariableDelay2->SetFilter(Logger::EMsgAll);
    //iLoggerPruner->SetFilter(Logger::EMsgAll);
    //iLoggerStarvationRamper->SetFilter(Logger::EMsgAll);
    //iLoggerMuter->SetFilter(Logger::EMsgAll);
    //iLoggerAnalogBypassRamper->SetFilter(Logger::EMsgAll);
    //iLoggerPreDriver->SetFilter(Logger::EMsgAll);
}

Pipeline::~Pipeline()
{
    // FIXME - should we wait for the pipeline to be halted before issuing a Quit?
    //         ...otherwise, MsgQuit goes down the pipeline ahead of final audio
    Quit();
    iEventThread->Stop();

    // loggers (if non-null) and iPreDriver will block until they receive the Quit msg
    delete iMuteCounted;
    delete iLoggerPreDriver;
    delete iPreDriver;
    delete iLoggerAnalogBypassRamper;
    delete iAnalogBypassRamper;
    delete iDecodedAudioValidatorMuter;
    delete iLoggerMuter;
    delete iMuterVolume;
    delete iMuterSamples;
    delete iDecodedAudioValidatorStarvationRamper;
    delete iRampValidatorStarvationRamper;
    delete iLoggerStarvationRamper;
    delete iStarvationRamper;
    delete iDecodedAudioValidatorPruner;
    delete iLoggerPruner;
    delete iPruner;
    delete iLoggerAttenuator;
    delete iAttenuator;
    delete iDecodedAudioValidatorDelay2;
    delete iRampValidatorDelay2;
    delete iLoggerVariableDelay2;
    delete iVariableDelay2;
    delete iLoggerDrainer;
    delete iDrainer;
    delete iDecodedAudioValidatorRouter;
    delete iLoggerRouter;
    delete iRouter;
    delete iLoggerReporter;
    delete iReporter;
    delete iLoggerSpotifyReporter;
    delete iSpotifyReporter;
    delete iDecodedAudioValidatorStopper;
    delete iRampValidatorStopper;
    delete iLoggerStopper;
    delete iStopper;
    delete iDecodedAudioValidatorWaiter;
    delete iRampValidatorWaiter;
    delete iLoggerWaiter;
    delete iWaiter;
    delete iLoggerTrackInspector;
    delete iTrackInspector;
    delete iDecodedAudioValidatorSkipper;
    delete iRampValidatorSkipper;
    delete iLoggerSkipper;
    delete iSkipper;
    delete iDecodedAudioValidatorDelay1;
    delete iRampValidatorDelay1;
    delete iLoggerVariableDelay1;
    delete iVariableDelay1;
    delete iDecodedAudioValidatorSeeker;
    delete iRampValidatorSeeker;
    delete iLoggerSeeker;
    delete iSeeker;
    delete iRampValidatorRamper;
    delete iLoggerRamper;
    delete iRamper;
    delete iLoggerDecodedAudioReservoir;
    delete iCodecController; // out of order - the start of a chain of pushers from iLoggerCodecController to iDecodedAudioReservoir
    delete iDecodedAudioReservoir;
    delete iLoggerDecodedAudioAggregator;
    delete iDecodedAudioAggregator;
    delete iLoggerSampleRateValidator;
    delete iSampleRateValidator;
    delete iRampValidatorCodec;
    delete iLoggerCodecController;
    delete iLoggerContainer;
    delete iContainer;
    delete iAudioDumper;
    delete iLoggerEncodedAudioReservoir;
    delete iEncodedAudioReservoir;
    delete iEventThread;
    delete iMsgFactory;
    delete iInitParams;
}

void Pipeline::AddContainer(Codec::ContainerBase* aContainer)
{
    iContainer->AddContainer(aContainer);
}

void Pipeline::AddCodec(Codec::CodecBase* aCodec)
{
    iCodecController->AddCodec(aCodec);
}

void Pipeline::Start(IAnalogBypassVolumeRamper& aAnalogBypassVolumeRamper, IVolumeRamper& aVolumeRamper)
{
    iAnalogBypassRamper->SetVolumeRamper(aAnalogBypassVolumeRamper);
    if (iMuterVolume != nullptr) {
        iMuterVolume->Start(aVolumeRamper);
    }
    iCodecController->Start();
}

void Pipeline::Quit()
{
    LOG(kPipeline, "> Pipeline::Quit()\n");
    if (iQuitting) {
        return;
    }
    iQuitting = true;
    DoPlay(true);
}

void Pipeline::NotifyStatus()
{
    EPipelineState state;
    {
        AutoMutex _(iLock);
        if (iQuitting) {
            return;
        }
        switch (iState)
        {
        case EPlaying:
            state = (iWaiting? EPipelineWaiting : (iBuffering? EPipelineBuffering : EPipelinePlaying));
            break;
        case EPaused:
            state = EPipelinePaused;
            break;
        case EStopped:
            state = EPipelineStopped;
            break;
        default:
            ASSERTS();
            state = EPipelineBuffering; // will never reach here but the compiler doesn't realise this
        }
        if (state == iLastReportedState) {
            return;
        }
        iLastReportedState = state;
    }
    iObserver.NotifyPipelineState(state);
}

MsgFactory& Pipeline::Factory()
{
    return *iMsgFactory;
}

void Pipeline::Play()
{
    DoPlay(false);
}

void Pipeline::DoPlay(TBool aQuit)
{
    TBool notifyStatus = true;
    iLock.Wait();
    if (iState == EPlaying) {
        notifyStatus = false;
    }
    iState = EPlaying;
    iLock.Signal();

    if (aQuit) {
        iStopper->Quit();
    }
    else {
        iStopper->Play();
    }
    if (notifyStatus) {
        NotifyStatus();
    }
}

void Pipeline::Pause()
{
    try {
        iStopper->BeginPause();
    }
    catch (StopperStreamNotPausable&) {
        THROW(PipelineStreamNotPausable);
    }
}

void Pipeline::Wait(TUint aFlushId)
{
    const TBool rampDown = (iState == EPlaying);
    iWaiter->Wait(aFlushId, rampDown);
}

void Pipeline::Stop(TUint aHaltId)
{
    iLock.Wait();
    /* FIXME - is there any race where iBuffering is true but the pipeline is also
               running, meaning that we want to allow Stopper to ramp down? */
    if (iBuffering) {
        iSkipper->RemoveAll(aHaltId, false);
    }
    iStopper->BeginStop(aHaltId);
    iLock.Signal();
}

void Pipeline::RemoveCurrentStream()
{
    const TBool rampDown = (iState == EPlaying);
    iSkipper->RemoveCurrentStream(rampDown);
}

void Pipeline::RemoveAll(TUint aHaltId)
{
    const TBool rampDown = (iState == EPlaying);
    iSkipper->RemoveAll(aHaltId, rampDown);
}

void Pipeline::Block()
{
    iSkipper->Block();
}

void Pipeline::Unblock()
{
    iSkipper->Unblock();
}

void Pipeline::Seek(TUint aStreamId, TUint aSecondsAbsolute)
{
    const TBool rampDown = (iState == EPlaying);
    iSeeker->Seek(aStreamId, aSecondsAbsolute, rampDown);
}

void Pipeline::AddObserver(ITrackObserver& aObserver)
{
    iTrackInspector->AddObserver(aObserver);
}

ISpotifyReporter& Pipeline::SpotifyReporter() const
{
    return *iSpotifyReporter;
}

ITrackChangeObserver& Pipeline::TrackChangeObserver() const
{
    return *iSpotifyReporter;
}

IPipelineElementUpstream& Pipeline::InsertElements(IPipelineElementUpstream& aTail)
{
    return iRouter->InsertElements(aTail);
}

TUint Pipeline::SenderMinLatencyMs() const
{
    return Jiffies::ToMs(kSenderMinLatency);
}

void Pipeline::GetThreadPriorityRange(TUint& aMin, TUint& aMax) const
{
    aMax = iInitParams->ThreadPriorityStarvationRamper();
    aMin = iInitParams->ThreadPriorityCodec();
}

void Pipeline::GetThreadPriorities(TUint& aFlywheelRamper, TUint& aStarvationRamper, TUint& aCodec, TUint& aEvent)
{
    aFlywheelRamper = iStarvationRamper->ThreadPriorityFlywheelRamper();
    aStarvationRamper = iStarvationRamper->ThreadPriorityStarvationRamper();
    aCodec = iInitParams->ThreadPriorityCodec();
    aEvent = iInitParams->ThreadPriorityEvent();
}

void PipelineLogBuffers()
{
    gPipeline->LogBuffers();
}

void Pipeline::LogBuffers() const
{
    const TUint encodedBytes = iEncodedAudioReservoir->SizeInBytes();
    const TUint decodedMs = Jiffies::ToMs(iDecodedAudioReservoir->SizeInJiffies());
    const TUint starvationMs = Jiffies::ToMs(iStarvationRamper->SizeInJiffies());
    Log::Print("Pipeline utilisation: encodedBytes=%u, decodedMs=%u, starvationRamper=%u\n",
               encodedBytes, decodedMs, starvationMs);
}

void Pipeline::Push(Msg* aMsg)
{
    iPipelineStart->Push(aMsg);
}

Msg* Pipeline::Pull()
{
    return iPipelineEnd->Pull();
}

void Pipeline::SetAnimator(IPipelineAnimator& aAnimator)
{
    iSampleRateValidator->SetAnimator(aAnimator);
    iVariableDelay2->SetAnimator(aAnimator);
    if (iMuterSamples != nullptr) {
        iMuterSamples->SetAnimator(aAnimator);
    }
}

void Pipeline::PipelinePaused()
{
    iLock.Wait();
    iState = EPaused;
    iLock.Signal();
    NotifyStatus();
}

void Pipeline::PipelineStopped()
{
    iLock.Wait();
    iState = EStopped;
    iLock.Signal();
    NotifyStatus();
}

void Pipeline::PipelinePlaying()
{
    iLock.Wait();
    iState = EPlaying;
    iLock.Signal();
    NotifyStatus();
}

TUint Pipeline::NextFlushId()
{
    /* non-use of iLock is deliberate.  It isn't absolutely required since all callers
       run in the Filler thread.  If we re-instate the lock, the call to
       RemoveCurrentStream() in Stop() will need to move outside its lock. */
    TUint id = iNextFlushId++;
    return id;
}

void Pipeline::PipelineWaiting(TBool aWaiting)
{
    iLock.Wait();
    iWaiting = aWaiting;
    iLock.Signal();
    NotifyStatus();
}

void Pipeline::RemoveStream(TUint aStreamId)
{
    (void)iSkipper->TryRemoveStream(aStreamId, !iBuffering);
}

void Pipeline::Mute()
{
    iMuteCounted->Mute();
}

void Pipeline::Unmute()
{
    iMuteCounted->Unmute();
}

void Pipeline::PostPipelineLatencyChanged()
{
    iVariableDelay2->PostPipelineLatencyChanged();
}

void Pipeline::SetAttenuation(TUint aAttenuation)
{
    iAttenuator->SetAttenuation(aAttenuation);
}

void Pipeline::NotifyMode(const Brx& aMode, const ModeInfo& aInfo)
{
    iObserver.NotifyMode(aMode, aInfo);
}

void Pipeline::NotifyTrack(Track& aTrack, const Brx& aMode, TBool aStartOfStream)
{
    iObserver.NotifyTrack(aTrack, aMode, aStartOfStream);
}

void Pipeline::NotifyMetaText(const Brx& aText)
{
    iObserver.NotifyMetaText(aText);
}

void Pipeline::NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds)
{
    iObserver.NotifyTime(aSeconds, aTrackDurationSeconds);
#if 0
    if (aSeconds % 8 == 0) {
        LogBuffers();
    }
#endif
}

void Pipeline::NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo)
{
    iObserver.NotifyStreamInfo(aStreamInfo);
}

void Pipeline::NotifyStarvationRamperBuffering(TBool aBuffering)
{
    iLock.Wait();
    iBuffering = aBuffering;
    const TBool notify = (iState == EPlaying);
    iLock.Signal();
    if (notify) {
        NotifyStatus();
#if 1
        if (aBuffering && !iWaiting) {
            const TUint encodedBytes = iEncodedAudioReservoir->SizeInBytes();
            const TUint decodedMs = Jiffies::ToMs(iDecodedAudioReservoir->SizeInJiffies());
            Log::Print("Pipeline utilisation: encodedBytes=%u, decodedMs=%u\n", encodedBytes, decodedMs);
        }
#endif
    }
}
