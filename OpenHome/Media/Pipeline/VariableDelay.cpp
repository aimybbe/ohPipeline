#include <OpenHome/Media/Pipeline/VariableDelay.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Media;

// VariableDelay

/*static const TChar* kStatus[] = { "Starting"
                                 ,"Running"
                                 ,"RampingDown"
                                 ,"RampedDown"
                                 ,"RampingUp" };*/

VariableDelay::VariableDelay(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, TUint aDownstreamDelay, TUint aRampDuration)
    : iMsgFactory(aMsgFactory)
    , iUpstreamElement(aUpstreamElement)
    , iDelayJiffies(0)
    , iLock("VDLY")
    , iDelayAdjustment(0)
    , iDownstreamDelay(aDownstreamDelay)
    , iRampDuration(aRampDuration)
    , iEnabled(false)
    , iWaitForAudioBeforeGeneratingSilence(false)
    , iStreamHandler(NULL)
{
    ResetStatusAndRamp();
}

VariableDelay::~VariableDelay()
{
}

Msg* VariableDelay::Pull()
{
    Msg* msg = NULL;
    iLock.Wait();
    if (iStatus != ERampingDown && iDelayAdjustment > 0) {
        if (iWaitForAudioBeforeGeneratingSilence) {
            do {
                msg = NextMsgLocked();
                if (msg != NULL) {
                    if (iWaitForAudioBeforeGeneratingSilence) {
                        iLock.Signal();
                        return msg;
                    }
                    else {
                        DoEnqueue(msg);
                    }
                }
            } while (msg == NULL && iWaitForAudioBeforeGeneratingSilence);
            msg = NULL; // DoEnqueue() above passed ownership of msg back to reservoir
        }
        // msg(s) pulled above may have altered iDelayAdjustment (e.g. MsgMode sets it to zero)
        if (iDelayAdjustment > 0) {
            const TUint size = ((TUint)iDelayAdjustment > kMaxMsgSilenceDuration? kMaxMsgSilenceDuration : (TUint)iDelayAdjustment);
            msg = iMsgFactory.CreateMsgSilence(size);
            iDelayAdjustment -= size;
            if (iDelayAdjustment == 0) {
                iStatus = (iStatus==ERampedDown? ERampingUp : ERunning);
                iRampDirection = Ramp::EUp;
                iCurrentRampValue = Ramp::kMin;
                iRemainingRampSize = iRampDuration;
            }
        }
    }
    if (msg == NULL) {
        do {
            msg = NextMsgLocked();
        } while (msg == NULL);
    }
    iLock.Signal();
    return msg;
}

Msg* VariableDelay::NextMsgLocked()
{
    Msg* msg;
    if (!IsEmpty()) {
        msg = DoDequeue();
    }
    else {
        iLock.Signal();
        msg = iUpstreamElement.Pull();
        iLock.Wait();
    }
    msg = msg->Process(*this);
    return msg;
}

MsgAudio* VariableDelay::DoProcessAudioMsg(MsgAudio* aMsg)
{
    MsgAudio* msg = aMsg;
    switch (iStatus)
    {
    case EStarting:
        iStatus = ERunning;
        break;
    case ERunning:
        // nothing to do, allow the message to be passed out unchanged
        break;
    case ERampingDown:
    {
        RampMsg(msg);
        if (iRemainingRampSize == 0) {
            if (iDelayAdjustment != 0) {
                iStatus = ERampedDown;
            }
            else {
                iStatus = ERampingUp;
                iRampDirection = Ramp::EUp;
                iRemainingRampSize = iRampDuration;
            }
        }
    }
        break;
    case ERampedDown:
    {
        if (iDelayAdjustment > 0) {
            // NotifyStarving() has been called since we last checked iDelayAdjustment in Pull()
            EnqueueAtHead(msg);
            return NULL;
        }
        ASSERT(iDelayAdjustment < 0)
        TUint jiffies = msg->Jiffies();
        if (jiffies > (TUint)(-iDelayAdjustment)) {
            MsgAudio* remaining = msg->Split((TUint)(-iDelayAdjustment));
            jiffies = msg->Jiffies();
            EnqueueAtHead(remaining);
        }
        iDelayAdjustment += jiffies;
        msg->RemoveRef();
        msg = NULL;
        if (iDelayAdjustment == 0) {
            iStatus = ERampingUp;
            iRampDirection = Ramp::EUp;
            iRemainingRampSize = iRampDuration;
        }
    }
        break;
    case ERampingUp:
    {
        RampMsg(msg);
        if (iRemainingRampSize == 0) {
            iStatus = ERunning;
        }
    }
        break;
    default:
        ASSERTS();
    }

    return msg;
}

void VariableDelay::RampMsg(MsgAudio* aMsg)
{
    if (aMsg->Jiffies() > iRemainingRampSize) {
        MsgAudio* remaining = aMsg->Split(iRemainingRampSize);
        DoEnqueue(remaining);
    }
    MsgAudio* split;
    iCurrentRampValue = aMsg->SetRamp(iCurrentRampValue, iRemainingRampSize, iRampDirection, split);
    if (split != NULL) {
        DoEnqueue(split);
    }
}

void VariableDelay::HandleStarving(const Brx& aMode)
{
    AutoMutex _(iLock);
    if (iEnabled && iMode == aMode) {
        iDelayAdjustment = iDelayJiffies;
        if (iDelayAdjustment == 0) {
            return;
        }
        iWaitForAudioBeforeGeneratingSilence = true;
        switch (iStatus)
        {
        case EStarting:
            break;
        case ERunning:
            iStatus = ERampingDown;
            iRampDirection = Ramp::EDown;
            iCurrentRampValue = Ramp::kMax;
            iRemainingRampSize = iRampDuration;
            break;
        case ERampingDown:
            break;
        case ERampedDown:
            break;
        case ERampingUp:
            iRampDirection = Ramp::EDown;
            // retain current value of iCurrentRampValue
            iRemainingRampSize = iRampDuration - iRemainingRampSize;
            if (iRemainingRampSize == 0) {
                iStatus = ERampedDown;
            }
            else {
                iStatus = ERampingDown;
            }
            break;
        default:
            ASSERTS();
        }
    }
}

void VariableDelay::ResetStatusAndRamp()
{
    iStatus = EStarting;
    iRampDirection = Ramp::ENone;
    iCurrentRampValue = Ramp::kMax;
    iRemainingRampSize = iRampDuration;
}

Msg* VariableDelay::ProcessMsg(MsgMode* aMsg)
{
    iEnabled = aMsg->SupportsLatency();
    iMode.Replace(aMsg->Mode());
    iDelayJiffies = 0;
    iDelayAdjustment = 0;
    iWaitForAudioBeforeGeneratingSilence = true;
    ResetStatusAndRamp();
    return aMsg;
}

Msg* VariableDelay::ProcessMsg(MsgSession* aMsg)
{
    iDelayAdjustment = iDelayJiffies; // FIXME - should be (iDelayJiffies - downstreamAudio)
    iWaitForAudioBeforeGeneratingSilence = true;
    return aMsg;
}

Msg* VariableDelay::ProcessMsg(MsgTrack* aMsg)
{
    return aMsg;
}

Msg* VariableDelay::ProcessMsg(MsgDelay* aMsg)
{
    TUint delayJiffies = aMsg->DelayJiffies();
    /*Log::Print("VariableDelay::ProcessMsg(MsgDelay*): delay=%u, iDownstreamDelay=%u, iDelayJiffies=%u, iStatus=%s\n",
               delayJiffies, iDownstreamDelay, iDelayJiffies, kStatus[iStatus]);*/
    if (iDownstreamDelay >= delayJiffies) {
        return aMsg;
    }
    delayJiffies -= iDownstreamDelay;
    if (delayJiffies == iDelayJiffies) {
        return aMsg;
    }

    iDelayAdjustment += (TInt)(delayJiffies - iDelayJiffies);
    iDelayJiffies = delayJiffies;
    //Log::Print("VariableDelay: delay=%u, adjustment=%d\n", iDelayJiffies/Jiffies::kPerMs, iDelayAdjustment/(TInt)Jiffies::kJiffiesPerMs);
    switch (iStatus)
    {
    case EStarting:
        iRampDirection = Ramp::ENone;
        iCurrentRampValue = Ramp::kMax;
        iRemainingRampSize = iRampDuration;
        break;
    case ERunning:
        iStatus = ERampingDown;
        iRampDirection = Ramp::EDown;
        iCurrentRampValue = Ramp::kMax;
        iRemainingRampSize = iRampDuration;
        break;
    case ERampingDown:
        if (iDelayAdjustment == 0) {
            if (iRampDuration == iRemainingRampSize) {
                iStatus = ERunning;
                iRampDirection = Ramp::ENone;
                iRemainingRampSize = 0;
            }
            else {
                iStatus = ERampingUp;
                iRampDirection = Ramp::EUp;
                // retain current value of iCurrentRampValue
                iRemainingRampSize = iRampDuration - iRemainingRampSize;
            }
        }
        break;
    case ERampedDown:
        break;
    case ERampingUp:
        iStatus = ERampingDown;
        iRampDirection = Ramp::EDown;
        // retain current value of iCurrentRampValue
        iRemainingRampSize = iRampDuration - iRemainingRampSize;
        if (iRemainingRampSize == 0) {
            iStatus = ERampedDown;
        }
        break;
    }

    aMsg->RemoveRef();
    return iMsgFactory.CreateMsgDelay(iDownstreamDelay);
}

Msg* VariableDelay::ProcessMsg(MsgEncodedStream* aMsg)
{
    return aMsg;
}

Msg* VariableDelay::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS(); /* only expect to deal with decoded audio at this stage of the pipeline */
    return NULL;
}

Msg* VariableDelay::ProcessMsg(MsgMetaText* aMsg)
{
    return aMsg;
}

Msg* VariableDelay::ProcessMsg(MsgHalt* aMsg)
{
    return aMsg;
}

Msg* VariableDelay::ProcessMsg(MsgFlush* aMsg)
{
    return aMsg;
}

Msg* VariableDelay::ProcessMsg(MsgWait* aMsg)
{
    return aMsg;
}

Msg* VariableDelay::ProcessMsg(MsgDecodedStream* aMsg)
{
    const DecodedStreamInfo& stream = aMsg->StreamInfo();
    iStreamHandler = stream.StreamHandler();
    ResetStatusAndRamp();
    MsgDecodedStream* msg = iMsgFactory.CreateMsgDecodedStream(stream.StreamId(), stream.BitRate(), stream.BitDepth(),
                                                               stream.SampleRate(), stream.NumChannels(), stream.CodecName(), 
                                                               stream.TrackLength(), stream.SampleStart(), stream.Lossless(), 
                                                               stream.Seekable(), stream.Live(), this);
    aMsg->RemoveRef();
    return msg;
}

Msg* VariableDelay::ProcessMsg(MsgAudioPcm* aMsg)
{
    iWaitForAudioBeforeGeneratingSilence = false;
    return DoProcessAudioMsg(aMsg);
}

Msg* VariableDelay::ProcessMsg(MsgSilence* aMsg)
{
    return DoProcessAudioMsg(aMsg);
}

Msg* VariableDelay::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    // MsgPlayable not supported at this stage of the pipeline (as we don't know how to ramp it)
    ASSERTS();
    return NULL;
}

Msg* VariableDelay::ProcessMsg(MsgQuit* aMsg)
{
    return aMsg;
}

EStreamPlay VariableDelay::OkToPlay(TUint aStreamId)
{
    if (iStreamHandler != NULL) {
        iStreamHandler->OkToPlay(aStreamId);
    }
    return ePlayNo;
}

TUint VariableDelay::TrySeek(TUint aStreamId, TUint64 aOffset)
{
    if (iStreamHandler != NULL) {
        return iStreamHandler->TrySeek(aStreamId, aOffset);
    }
    return MsgFlush::kIdInvalid;
}

TUint VariableDelay::TryStop(TUint aStreamId)
{
    if (iStreamHandler != NULL) {
        iStreamHandler->TryStop(aStreamId);
    }
    return MsgFlush::kIdInvalid;
}

void VariableDelay::NotifyStarving(const Brx& aMode, TUint aStreamId)
{
    HandleStarving(aMode);
    if (iStreamHandler != NULL) {
        iStreamHandler->NotifyStarving(aMode, aStreamId);
    }
}
