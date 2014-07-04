#ifndef HEADER_CODEC_OHM
#define HEADER_CODEC_OHM

#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/OhNetTypes.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Songcast/Ohm.h>
#include <OpenHome/Av/Songcast/OhmMsg.h>

namespace OpenHome {
namespace Av {

class CodecOhm : public Media::Codec::CodecBase, private IReader
{
public:
    CodecOhm(OhmMsgFactory& aMsgFactory);
    ~CodecOhm();
private: // from CodecBase
    TBool SupportsMimeType(const Brx& aMimeType);
    TBool Recognise();
    void StreamInitialise();
    void Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
private: // from IReader
    Brn Read(TUint aBytes);
    Brn ReadUntil(TByte aSeparator);
    void ReadFlush();
    void ReadInterrupt();
private:
    void OutputDelay();
private:
    OhmMsgFactory& iMsgFactory;
    Bws<OhmMsgAudioBlob::kMaxBytes> iBuf;
    TUint iOffset;
    TBool iStreamOutput;
    TBool iSendSession;
    TUint iSampleRate;
    TUint iLatency;
};

} // namespace Av
} // namespace OpenHome

#endif // HEADER_CODEC_OHM
