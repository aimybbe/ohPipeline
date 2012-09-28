#include <OpenHome/Media/Logger.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Msg.h>

using namespace OpenHome;
using namespace OpenHome::Media;

//  Logger

Logger::Logger(IPipelineElement& aUpstreamElement, const TChar* aId)
    : iUpstreamElement(aUpstreamElement)
    , iId(aId)
{
}

Msg* Logger::Pull()
{
    Msg* msg = iUpstreamElement.Pull();
    (void)msg->Process(*this);
    return msg;
}

Msg* Logger::ProcessMsg(MsgAudioPcm* aMsg)
{
    Log::Print("Pipeline (%s): audioPcm {jiffies: %u}\n", iId, aMsg->Jiffies());
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgSilence* aMsg)
{
    Log::Print("Pipeline (%s): silence {jiffies: %u}\n", iId, aMsg->Jiffies());
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgPlayable* aMsg)
{
    Log::Print("Pipeline (%s): playable {bytes: %u}\n", iId, aMsg->Bytes());
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgAudioFormat* aMsg)
{
    const AudioFormat& af = aMsg->Format();
    Log::Print("Pipeline (%s): audio format {bitRate: %u, bitDepth: %u, sampleRate: %u, codec: ", iId, af.BitRate(), af.BitDepth(), af.SampleRate());
    Log::Print(af.CodecName());
    Log::Print(", trackLength: %llu, lossless: %s}\n", af.TrackLength(), (af.Lossless()? "true" : "false"));
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgTrack* aMsg)
{
    Log::Print("Pipeline (%s): track\n", iId);
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgMetaText* aMsg)
{
    Log::Print("Pipeline (%s): metaText {", iId);
    Log::Print(aMsg->MetaText());
    Log::Print("}\n");
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgHalt* aMsg)
{
    Log::Print("Pipeline (%s): halt\n", iId);
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgFlush* aMsg)
{
    Log::Print("Pipeline (%s): flush\n", iId);
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgQuit* aMsg)
{
    Log::Print("Pipeline (%s): quit\n", iId);
    return aMsg;
}
