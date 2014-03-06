#include <OpenHome/Media/Protocol/RaopHeader.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Media/Protocol/ProtocolRaop.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Av/Raop/Raop.h>

EXCEPTION(ResendTimeout);
EXCEPTION(ResendInvalid);
EXCEPTION(InvalidHeader);
EXCEPTION(RaopAudioServerClosed);

using namespace OpenHome;
using namespace OpenHome::Media;


Protocol* ProtocolFactory::NewRaop(Environment& aEnv, IRaopDiscovery& aDiscovery, UdpServerManager& aServerManager, TUint aAudioId, TUint aControlId, TUint aTimingId)
{ // static
    return new ProtocolRaop(aEnv, aDiscovery, aServerManager, aAudioId, aControlId, aTimingId);
}

ProtocolRaop::ProtocolRaop(Environment& aEnv, IRaopDiscovery& aDiscovery, UdpServerManager& aServerManager, TUint aAudioId, TUint aControlId, TUint /*aTimingId*/)
    : ProtocolNetwork(aEnv)
    , iDiscovery(aDiscovery)
    , iServerManager(aServerManager)
    , iRaopAudio(iServerManager.Find(aAudioId))
    , iRaopControl(aEnv, iServerManager.Find(aControlId))
//  , iRaopTiming(iServerManager.Find(aTimingId))
    , iLockRaop("PRAL")
{
}

ProtocolRaop::~ProtocolRaop()
{
}

void ProtocolRaop::DoInterrupt()
{
    LOG(kMedia, "ProtocolRaop::DoInterrupt\n");

    iRaopAudio.DoInterrupt();
    iRaopControl.DoInterrupt();
}

ProtocolStreamResult ProtocolRaop::Stream(const Brx& aUri)
{
    iLockRaop.Wait();
    iNextFlushId = MsgFlush::kIdInvalid;
    iStopped = false;
    iLockRaop.Signal();

    // raop doesn't actually stream from a URI, so just expect a dummy uri
    Uri uri;
    uri.Replace(aUri);
    LOG(kMedia, "ProtocolRaop::Stream ");
    LOG(kMedia, uri.AbsoluteUri());
    LOG(kMedia, "\n");

    if (uri.Scheme() != Brn("raop")) {
        LOG(kMedia, "ProtocolRaop::Stream Scheme not recognised\n");
        iLockRaop.Wait();
        iStopped = true;
        iLockRaop.Signal();
        return EProtocolErrorNotSupported;
    }

    // FIXME - parse uri to get ports for sending control/timing info to
    // (could also get server ids for audio/control/timing and retrieve servers
    // on behalf of them, and pass in servers while resetting)

    TBool start = true;
    TUint aesSid = 0;
    // FIXME - should we pass an ID into Reset() methods here and update server
    // each time, instead of doing it in constructor? - would require dynamic
    // re-allocation of UdpReader
    iRaopControl.Reset();
    iRaopAudio.Reset();
    Brn audio;
    TUint16 expected = 0;

    StartStream();

    // Output audio stream
    for (;;) {
        iLockRaop.Wait();
        if (iStopped) {
            iSupply->OutputFlush(iNextFlushId);
            iStopped = true;
            iLockRaop.Signal();
            return EProtocolStreamStopped;
        }
        iLockRaop.Signal();

        try {
            TUint16 count = iRaopAudio.ReadPacket();

            if (!iDiscovery.Active()) {
                LOG(kMedia, "ProtocolRaop::Stream() no active session\n");
                iLockRaop.Wait();
                iStopped = true;
                iLockRaop.Signal();
                return EProtocolStreamStopped;
            }

            iDiscovery.KeepAlive();

            if(aesSid != iDiscovery.AesSid()) {
                aesSid = iDiscovery.AesSid();       // aes key has been updated

                LOG(kMedia, "ProtocolRaop::Stream() new sid\n");

                iRaopAudio.Initialise(iDiscovery.Aeskey(), iDiscovery.Aesiv());
            }
            if(start) {
                LOG(kMedia, "ProtocolRaop::Stream() new container\n");
                start = false;        // create dummy container for codec recognition and initialisation
                OutputContainer(Brn(iDiscovery.Fmtp()));
                expected = count;   // init expected count
            }
            TInt padding = count;
            padding -= expected;

            if(padding >= 0) { //ignore if packet is out of order
                TUint SenderSkew, latency;
                iRaopControl.Time(SenderSkew, latency);

                // if there are missing packets request re-send and wait for response
                if(padding > 0) {
                    iRaopControl.RequestResend(expected, padding);
                    while(padding > 0) {
                        LOG(kMedia, "ProtocolRaop get resent data, padding %d\n", padding);
                        try {
                            iRaopControl.GetResentData(iResentData, expected);  //this will block until data received or timed out
                            iRaopControl.LockRx();  // don't allow any more data to be received while processing
                            LOG(kMedia, "ProtocolRaop received resent data, %d bytes\n", iResentData.Bytes());
                            Brn data(iResentData);
                            iRaopAudio.DecodePacket(SenderSkew, latency, data);
                            OutputAudio(iRaopAudio.Audio());
                            padding--;
                            expected++;
                            iRaopControl.UnlockRx();
                        }
                        catch(ResendTimeout) {
                            LOG(kMedia, "ProtocolRaop NOT received resent data, padding %d\n", padding);

                            //just mute if resent packets not available
                            //this assumes that iRaopAudio has already been set
                            iRaopAudio.SetMute();
                            while(padding--) {
                                OutputAudio(iRaopAudio.Audio());
                            }
                        }
                        catch(ResendInvalid) {
                            // may be corrupted or a redundant frame left from a previous timeout
                            LOG(kMedia, "ProtocolRaop unexpected data - ignore, padding %d\n", padding);
                        }
                    }
                }
                try {
                    iRaopAudio.DecodePacket(SenderSkew, latency); // send original
                    OutputAudio(iRaopAudio.Audio());
                    expected = count+1;
                }
                catch (InvalidHeader&)
                {
                    LOG(kMedia, "ProtocolRaop::Stream caught InvalidHeader exception\n");
                }
            }
        }
        catch (InvalidHeader&) {
            LOG(kMedia, "<ProtocolRaop::Stream Invalid Header\n");
            //break;
        }
        catch (NetworkError&) {
            LOG(kMedia, "<ProtocolRaop::Stream Network error\n");
            //break;
        }
        catch (ReaderError&) {
            LOG(kMedia, "<ProtocolRaop::Stream Reader error\n");
            // This can indicate an interrupt (caused by, e.g., a TryStop)
            // Continue around loop and see if iStopped has been set.
        }
        catch (HttpError&) {
            LOG(kMedia, "<ProtocolRaop::Stream sdp not received\n");
            // ignore and continue - sender should stop on a closed connection! wait for sender to re-select device
        }
        catch (RaopAudioServerClosed&) {
            LOG(kMedia, "ProtocolRaop::Stream RaopAudioServerClosed\n");
            iLockRaop.Wait();
            iStopped = true;
            iLockRaop.Signal();
            return EProtocolStreamStopped;
        }
    }
}

void ProtocolRaop::StartStream()
{
    LOG(kMedia, "ProtocolRaop::StartStream\n");
    AutoMutex a(iLockRaop);
    iStreamId = iIdProvider->NextStreamId();
    iSupply->OutputStream(Uri().AbsoluteUri(), 0, false, false, *this, iStreamId);
}

void ProtocolRaop::OutputContainer(const Brn &aFmtp)
{
    Bws<60> container(Brn("Raop"));
    container.Append(Brn(" "));
    Ascii::AppendDec(container, aFmtp.Bytes()+1);   // account for newline char added
    container.Append(" ");
    container.Append(aFmtp);
    container.Append(Brn("\n"));
    LOG(kMedia, "ProtocolRaop::OutputContainer container %d bytes [", container.Bytes()); LOG(kMedia, container); LOG(kMedia, "]\n");
    iSupply->OutputData(container);
}

void ProtocolRaop::OutputAudio(const Brn &aPacket)
{
    iSupply->OutputData(aPacket);
}

TUint ProtocolRaop::TryStop(TUint aTrackId, TUint aStreamId)
{
    TBool stop = false;
    iLockRaop.Wait();
    if (!iStopped) {
        stop = (iProtocolManager->IsCurrentTrack(aTrackId) && iStreamId == aStreamId);
        if (stop) {
            iNextFlushId = iFlushIdProvider->NextFlushId();
            iStopped = true;
            DoInterrupt();
        }
    }
    iLockRaop.Signal();
    return (stop? iNextFlushId : MsgFlush::kIdInvalid);
}

TBool ProtocolRaop::Active()
{
    return iDiscovery.Active();
}

void ProtocolRaop::Close()
{
    LOG(kMedia, "ProtocolRaop::Close\n");
    iDiscovery.Close();
}

void ProtocolRaop::Deactivate()
{
    LOG(kMedia, "ProtocolRaop::Deactivate\n");
    iDiscovery.Deactivate();
}

RaopControl::RaopControl(Environment& aEnv, SocketUdpServer& aServer)
    : iServer(aServer)
    , iReceive(iServer)
    , iMutex("raoc")
    , iMutexRx("raoR")
    , iSemaResend("raoc", 0)
    , iResend(0)
    , iExit(false)
{
    iTimerExpiry = new Timer(aEnv, MakeFunctor(*this, &RaopControl::TimerExpired));
    iThreadControl = new ThreadFunctor("RaopControl", MakeFunctor(*this, &RaopControl::Run), kPriority-1, kSessionStackBytes);
    iThreadControl->Start();

    // monitor this port to get the latency
    // format is:
    // 4b header
    // 4b now-latency (samples)
    // 8b ntp time of last sync
    // 4b now (samples)
    // use this to calc initial delay - pass in data msg, along with new 'restart' flag
}

RaopControl::~RaopControl()
{
    iExit = true;
    iServer.ReadInterrupt();
    delete iThreadControl;
    delete iTimerExpiry;
}

void RaopControl::LockRx()
{
    iMutexRx.Wait();
}

void RaopControl::UnlockRx()
{
    iMutexRx.Signal();
}

void RaopControl::DoInterrupt()
{
    LOG(kMedia, "RaopControl::DoInterrupt()\n");
    iMutex.Wait();
    if(iResend) {
        iResend = 0;
        iSemaResend.Signal();
    }

    iMutex.Signal();
}

void RaopControl::Reset()
{
    // FIXME - should take a TUint aPort as param and set up iEndpoint here
}

void RaopControl::Run()
{
    LOG(kMedia, "RaopControl::Run\n");
    iResend = 0;

    while (!iExit) {
        try {
            Brn id = iReceive.Read(2);
            iEndpoint = iServer.Sender(); // FIXME - will the sender (iTunes) always be using the same port for in/out?
            if(id.Bytes() < 2) {
                LOG(kMedia, " RaopControl id bytes %d\n", id.Bytes());
                continue;
            }

            TUint type = Converter::BeUint16At(id, 0) & 0xefff; // first id is 0x90xx
            if(type == 0x80D4) {
                //read rest of header
                Brn control = iReceive.Read(18);
                iEndpoint = iServer.Sender();
                //extract timing info from control message and allow mutexed external access to data...
                if(control.Bytes() < 18) {
                    THROW(ReaderError);
                }
                //TUint mclk = iI2sDriver.MclkCount();  // record current mclk count at dac - use this to calculate the drift
                //mclk /= 256;  // convert to samples
                iMutex.Wait();
                iLatency = Converter::BeUint32At(control, 14) - Converter::BeUint32At(control, 2);
                //iSenderSkew = Converter::BeUint32At(control, 2) - mclk;   // calculate count when this should play relative to current mclk count
                iMutex.Signal();
                //LOG(kMedia, " RaopControl: now %u, play at %u, iSenderSkew %u, iLatency %u, mclk %u, iResend %d\n", Converter::BeUint32At(control, 14), Converter::BeUint32At(control, 2), iSenderSkew, iLatency, mclk, iResend);
                LOG(kMedia, " RaopControl: now %u, play at %u, iSenderSkew %u, iLatency %u, iResend %d\n", Converter::BeUint32At(control, 14), Converter::BeUint32At(control, 2), iSenderSkew, iLatency, iResend);
                iReceive.ReadFlush();
            }
            else if(type == 0x80D6) {
                // resent packet
                iReceive.Read(2);   //ignore next 2 bytes
                iEndpoint = iServer.Sender();
                Bws<kMaxReadBufferBytes> data;
                iServer.Read(data);   // read a full udp packet
                iEndpoint = iServer.Sender();
                LOG(kMedia, "RaopControl read %d bytes, iResend %d\n", data.Bytes(), iResend);
                iMutexRx.Wait();    // wait for processing of previous resend message
                iMutex.Wait();
                TBool resend = false;
                if (iResend) {
                    resend = true;
                }
                if(resend) {            // ignore if unexpected (may have been delayed and timed out)
                    if(iResentData.Bytes() != 0) { // previous data hasn't been processed yet so fail
                        iResentData.SetBytes(0);
                    }
                    else {
                        iResentData.Replace(data);
                    }
                }
                iMutex.Signal();
                iMutexRx.Signal();

                // inform audio thread that there is a resent packet waiting
                if(resend) {
                    iSemaResend.Signal();
                }
                iReceive.ReadFlush();
            }
            else {
                LOG(kMedia, "RaopControl unknown type %x, %x - iResend %d\n", type, Converter::BeUint16At(id, 0), iResend);
                iReceive.ReadFlush(); // unexpected so ignore
            }
        }
        catch (ReaderError) {
            LOG(kMedia, "RaopControl Exception - iResend %d\n", iResend);

            // either no data or invalid header
            iReceive.ReadFlush();
        }
    }
}

void RaopControl::Time(TUint& aSenderSkew, TUint& aLatency)
{
    iMutex.Wait();
    aSenderSkew = iSenderSkew;
    aLatency = iLatency;
    iMutex.Signal();
}

void RaopControl::RequestResend(TUint aPacketId, TUint aPackets)
{
    LOG(kMedia, "RequestResend aPackets %d\n", aPackets);

    iMutex.Wait();
    TUint resend = iResend;
    if(resend == 0) {   // ignore if already requested data
        iResend = aPackets;
        iResentData.SetBytes(0);
        //while(iSemaResend.TryWait() != 0) {
        //    // this should never occur, but if it does it is recoverable
        //    LOG(kMedia, "******************* purge stale semaphore ****************\n");
        //}
    }
    else {
        LOG(kMedia, "RequestResend already active, resend %d\n", resend);
    }

    iMutex.Signal();

    if(resend == 0) {
        Bws<16> request(Brn(""));
        request.Append((TByte)0x80);
        request.Append((TByte)0xD5);
        request.Append((TByte)0x00);
        request.Append((TByte)0x01);
        request.Append((TByte)((aPacketId >> 8) & 0xff));
        request.Append((TByte)((aPacketId) & 0xff));
        request.Append((TByte)((aPackets >> 8) & 0xff));
        request.Append((TByte)((aPackets) & 0xff));

        try {
            iServer.Send(request, iEndpoint);
        }
        catch(NetworkError) {
            // will handle this by timing out on receive
        }
    }
}


void RaopControl::GetResentData(Bwx& aData, TUint16 aCount)
{
    static const TUint kTimerExpiryTimeoutMs = 80; // set this to avoid loss of data in main stream buffer

    // must wait until resent data is received, or timed out

    iTimerExpiry->FireIn(kTimerExpiryTimeoutMs);

    LOG(kMedia, "RaopControl::GetResentData wait for data\n");
    iSemaResend.Wait(); // wait for resent data to be received or timeout
    iMutex.Wait();
    //if timed out, resent data will be empty
    aData.SetBytes(0);
    TBool valid = false;
    TBool timeout = false;
    if(iResentData.Bytes() >= 8) { // ensure valid
        TUint type = Converter::BeUint16At(iResentData, 0) & 0xffff;
        if(type == 0x8060) {
            TUint16 count = Converter::BeUint16At(iResentData, 2) & 0xffff;
            if(count == aCount) {
                aData.Replace(iResentData);
                iResend--;
                valid = true;
            }
            else{
                LOG(kMedia, "RaopControl::GetResentData invalid resent data count, is %d, should be %d\n", count, aCount);
            }
        }
        else {
            LOG(kMedia, "RaopControl::GetResentData Converter::BeUint16At(iResentData,0) %x\n", type);
        }
    }
    else {
        LOG(kMedia, "RaopControl::GetResentData iResentData.Bytes() %d\n", iResentData.Bytes());
        if(iResentData.Bytes() == 0) {
            timeout = true; // no data, so must have timed out
        }
    }
    iResentData.SetBytes(0);
    iMutex.Signal();
    if(timeout) {
        THROW(ResendTimeout);
    }
    if(!valid) {
        THROW(ResendInvalid);
    }
}


void RaopControl::TimerExpired()
{

    iMutex.Wait();
    LOG(kMedia, "RaopControl TimerExpired, iResend %d\n", iResend);
    if(iResend) {
        iResend = 0;        // not received sent frames
        iMutex.Signal();
        iSemaResend.Signal();
    }
    else {
        iMutex.Signal();
    }

}

//RaopTiming::RaopTiming(SocketUdpServer& aServer)
//    : iServer(aServer)
//{
//}


RaopAudio::RaopAudio(SocketUdpServer& aServer)
    : iServer(aServer)
{
}

RaopAudio::~RaopAudio()
{
}

void RaopAudio::Reset()
{
    iDataBuffer.SetBytes(0);
    iAudio.SetBytes(0);
    iAeskey.SetBytes(0);
    iAesiv.SetBytes(0);
    iInitId = true;
    iInterrupted = false;
    iServer.Interrupt(false);
    iServer.ReadFlush();  // set to read next udp packet
}

void RaopAudio::Initialise(const Brx &aAeskey, const Brx &aAesiv)
{
    iAeskey.Replace(aAeskey);
    iAesiv.Replace(aAesiv);
}

void RaopAudio::DoInterrupt()
{
    LOG(kMedia, "RaopAudio::DoInterrupt()\n");
    iInterrupted = true;
    iServer.ReadInterrupt();
}

TUint16 RaopAudio::ReadPacket()
{
    // Read a packet at a time and process it.
    LOG(kMedia, ">RaopAudio::ReadPacket()\n");
    TUint16 count = 0;

    for (;;) {
        try {
            iServer.Read(iDataBuffer);
        }
        catch (ReaderError&) {
            iServer.ReadFlush();

            // Either no data, user abort or invalid header
            if (iInterrupted) {
                LOG(kMedia, "RaopAudio::ReadPacket() Exception, iInterrupted %d\n", iInterrupted);
                THROW(ReaderError);
            }
            if (!iServer.IsOpen()) {
                LOG(kMedia, "RaopAudio::ReadPacket() RaopAudioServerClosed\n");
                THROW(RaopAudioServerClosed);
            }
            if(iDataBuffer.Bytes() < 12) {  // may get here if kMaxReadBufferBytes not read
                continue;   // keep trying if not interrupted
            }
            return count;
        }
        iServer.ReadFlush();  // set to read next udp packet

        if(iDataBuffer.Bytes() < 12) {  // may get here if kMaxReadBufferBytes not read
            continue;
        }

        // Check header
        if((iDataBuffer[0] != 0x80)
                || !((iDataBuffer[1] == 0x60)
                || (iDataBuffer[1] == 0xe0))) {
            LOG(kMedia, "RaopAudio::ReadPacket() invalid header %x\n", iDataBuffer[0]);
            THROW(InvalidHeader); // invalid header
        }

        // Process ID
        TUint32 id = Converter::BeUint32At(iDataBuffer, 8);
        LOG(kMedia, "RaopAudio::ReadPacket() %d, id %d\n", iDataBuffer.Bytes(), id);

        if(iInitId) {
            iInitId = false;
            iId = id;   // appears to be random id generated by iTunes for each session
            LOG(kMedia, "RaopAudio::ReadPacket() New iId = %d\n", iId);
        }

        if(iId == id) {
            count = Converter::BeUint16At(iDataBuffer, 2);
            LOG(kMedia, "RaopAudio::ReadPacket() iId = %d, count = %d\n", iId, count);
            break;  // packet with same id found
        }
        LOG(kMedia, "RaopAudio::ReadPacket() no data so retry\n");
        // rogue id so ignore
    }

    return count;
}

void RaopAudio::DecodePacket(TUint aSenderSkew, TUint aLatency)
{
    DecodePacket(aSenderSkew, aLatency, iDataBuffer);
}

void RaopAudio::DecodePacket(TUint aSenderSkew, TUint aLatency, Brx& aData)
{
    //LOG(kMedia, "RaopAudio::DecodePacket() bytes %d\n", aData.Bytes());

    RaopDataHeader header(aData, aSenderSkew, aLatency);

    unsigned char *inbuf = (unsigned char *)aData.Ptr()+12;
    unsigned char *outbuf = (unsigned char *)iAudio.Ptr()+sizeof(header);
    unsigned char iv[16];

    if( (header.Bytes() + sizeof(header)) > iAudio.MaxBytes())
    {
        THROW(InvalidHeader);   // invalid data received, should really add different exception
    }

    memcpy(iv, iAesiv.Ptr(), sizeof(iv));   //use same iv at start of each decryption block

    TUint i;
    for (i = 0; i+16 <= header.Bytes(); i += 16)
        AES_cbc_encrypt(inbuf+i, outbuf+i, 0x10, (AES_KEY*)iAeskey.Ptr(), iv, AES_DECRYPT);
    if (header.Bytes() & 0xf)
        memcpy(outbuf+i, inbuf+i, header.Bytes() & 0xf);

    // create new header:
    iAudio.SetBytes(header.Bytes() + sizeof(header));
    memcpy((unsigned char *)iAudio.Ptr(), (unsigned char *)&header, sizeof(header));
}

Brn RaopAudio::Audio()
{
    return(Brn(iAudio));
}

void RaopAudio::SetMute()
{
    ((RaopDataHeader*)iAudio.Ptr())->SetMute();
}
