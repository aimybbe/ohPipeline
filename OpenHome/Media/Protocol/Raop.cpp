#include <OpenHome/Media/Protocol/ProtocolRaop.h>
#include <OpenHome/Media/Protocol/Raop.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Net/Private/MdnsProvider.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Thread.h>

#include <openssl/evp.h>


using namespace OpenHome;
using namespace OpenHome::Media;

// RaopDevice
RaopDevice::RaopDevice(Net::DvStack& aDvStack, const Brx& aName, TIpAddress aIpAddr, const Brx& aMacAddr)
    : iProvider(*aDvStack.MdnsProvider())
    , iName(aName)
    , iEndpoint(kPortRaopDiscovery, aIpAddr)
    , iMacAddress(aMacAddr)
    , iRegistered(false)
{
    ASSERT(aMacAddr.Bytes() == 12);
    iName.Replace("");
    iName.Append(iMacAddress);
    iName.Append("@");
    iName.Append(aName);

    iHandleRaop = iProvider.MdnsCreateService();

    TByte testarr[6];
    MacAddressOctets(testarr);
}

void RaopDevice::Register()
{
    if(iRegistered) {
        return; // already registered
    }

    Bws<200> info;

    info.SetBytes(0);
    iProvider.MdnsAppendTxtRecord(info, "txtvers", "1");    // does mdns support multiple records clumped together?
    iProvider.MdnsAppendTxtRecord(info, "ch", "2");
    iProvider.MdnsAppendTxtRecord(info, "cn", "0,1");
    iProvider.MdnsAppendTxtRecord(info, "ek", "1");
    iProvider.MdnsAppendTxtRecord(info, "et", "0,1");
    iProvider.MdnsAppendTxtRecord(info, "sv", "false");
    iProvider.MdnsAppendTxtRecord(info, "da", "true");
    iProvider.MdnsAppendTxtRecord(info, "sr", "44100");
    iProvider.MdnsAppendTxtRecord(info, "ss", "16");
    iProvider.MdnsAppendTxtRecord(info, "pw", "false");
    iProvider.MdnsAppendTxtRecord(info, "sm", "false");
    iProvider.MdnsAppendTxtRecord(info, "tp", "UDP");
    iProvider.MdnsAppendTxtRecord(info, "vn", "3");

    LOG(kCodec, "RaopDevice::Register name: ");
    LOG(kCodec, iName);
    LOG(kCodec, "\n");

    iProvider.MdnsRegisterService(iHandleRaop, iName.PtrZ(), "_raop._tcp", iEndpoint.Address(), kPortRaopDiscovery, info.PtrZ());
    iRegistered = true;
}

void RaopDevice::Deregister()
{
    if(!iRegistered) {
        return;     // not registered
    }

    iProvider.MdnsDeregisterService(iHandleRaop);
    iRegistered = false;
}

const Endpoint& RaopDevice::GetEndpoint() const
{
    return iEndpoint;
}

const Brx& RaopDevice::MacAddress() const
{
    return iMacAddress;
}

void RaopDevice::MacAddressOctets(TByte (&aOctets)[6]) const
{
    LOG(kMedia, ">RaopDevice::MacAddressOctets\n");
    aOctets[0] = static_cast<TByte>(Ascii::UintHex(Brn(iMacAddress.Ptr(), 2)));
    aOctets[1] = static_cast<TByte>(Ascii::UintHex(Brn(iMacAddress.Ptr()+2, 2)));
    aOctets[2] = static_cast<TByte>(Ascii::UintHex(Brn(iMacAddress.Ptr()+4, 2)));
    aOctets[3] = static_cast<TByte>(Ascii::UintHex(Brn(iMacAddress.Ptr()+6, 2)));
    aOctets[4] = static_cast<TByte>(Ascii::UintHex(Brn(iMacAddress.Ptr()+8, 2)));
    aOctets[5] = static_cast<TByte>(Ascii::UintHex(Brn(iMacAddress.Ptr()+10, 2)));

    LOG(kMedia, "RaopDevice::MacAddressOctets ");
    for (TUint i=0; i<sizeof(aOctets); i++) {
        if (i==sizeof(aOctets)-1) {
            LOG(kMedia, "%u", aOctets[i]);
        }
        else {
            LOG(kMedia, "%u:", aOctets[i]);
        }
    }
    LOG(kMedia, "\n");
}


RaopDiscovery::RaopDiscovery(Environment& aEnv, ProtocolRaop& aProtocolRaop, RaopDevice& aRaopDevice, TUint aInstance)
    : iAeskeyPresent(false)
    , iAesSid(0)
    , iProtocolRaop(aProtocolRaop)
    //, iVolume(aVolume)
    , iRaopDevice(aRaopDevice)
    , iInstance(aInstance)
    , iActive(false)
{
    iReaderBuffer = new Srs<kMaxReadBufferBytes>(*this);
    iWriterBuffer = new Sws<kMaxWriteBufferBytes>(*this);
    iWriterAscii = new WriterAscii(*iWriterBuffer);
    iReaderRequest = new ReaderHttpRequest(aEnv, *iReaderBuffer);
    iWriterRequest = new WriterRtspRequest(*iWriterBuffer);
    iWriterResponse = new WriterHttpResponse(*iWriterBuffer);

    iRaopDevice.Register();

    iReaderRequest->AddHeader(iHeaderContentLength);
    iReaderRequest->AddHeader(iHeaderContentType);
    iReaderRequest->AddHeader(iHeaderCSeq);
    iReaderRequest->AddHeader(iHeaderAppleChallenge);
    iReaderRequest->AddMethod(RtspMethod::kOptions);
    iReaderRequest->AddMethod(RtspMethod::kAnnounce);
    iReaderRequest->AddMethod(RtspMethod::kSetup);
    iReaderRequest->AddMethod(RtspMethod::kRecord);
    iReaderRequest->AddMethod(RtspMethod::kSetParameter);
    iReaderRequest->AddMethod(RtspMethod::kFlush);
    iReaderRequest->AddMethod(RtspMethod::kTeardown);
    iReaderRequest->AddMethod(RtspMethod::kPost);

    iDeactivateTimer = new Timer(aEnv, MakeFunctor(*this, &RaopDiscovery::DeactivateCallback));
}

void RaopDiscovery::WriteSeq(TUint aCSeq)
{
    iWriterAscii->Write(Brn("CSeq: "));
    iWriterAscii->WriteUint(aCSeq);
    iWriterAscii->WriteNewline();
}

void RaopDiscovery::WriteFply(Brn aData)
{
    const TByte cfply1[] = {
                    0x02, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x82, 0x02, 0x00,

                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    const TByte cfply2[] = {
                    0x02, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x14
    };

    Bws<200> fply(Brn("FPLY"));
    if(aData[6] == 1) {     // respond to first POST
        fply.Append(cfply1, sizeof(cfply1));
        fply[13] = aData[14];       // copy current variable count
    }
    else {
        fply.Append(cfply2, sizeof(cfply2));
        fply.Append(&aData[0x90], 0x14);    // data is same as last 0x14 bytes of iTunes data
    }
    iWriterAscii->Write(RtspHeader::kContentLength);
    iWriterAscii->Write(Brn(": "));
    iWriterAscii->WriteUint(fply.Bytes());
    iWriterAscii->WriteNewline();
    iWriterAscii->WriteNewline();
    iWriterAscii->Write(fply);
}

void RaopDiscovery::GetRsa()
{
    static const unsigned char key_private[] = {
            0x30, 0x82, 0x04, 0xA5, 0x02, 0x01, 0x00, 0x02, 0x82, 0x01, 0x01, 0x00, 0xE7, 0xD7, 0x44, 0xF2,
            0xA2, 0xE2, 0x78, 0x8B, 0x6C, 0x1F, 0x55, 0xA0, 0x8E, 0xB7, 0x05, 0x44, 0xA8, 0xFA, 0x79, 0x45,
            0xAA, 0x8B, 0xE6, 0xC6, 0x2C, 0xE5, 0xF5, 0x1C, 0xBD, 0xD4, 0xDC, 0x68, 0x42, 0xFE, 0x3D, 0x10,
            0x83, 0xDD, 0x2E, 0xDE, 0xC1, 0xBF, 0xD4, 0x25, 0x2D, 0xC0, 0x2E, 0x6F, 0x39, 0x8B, 0xDF, 0x0E,
            0x61, 0x48, 0xEA, 0x84, 0x85, 0x5E, 0x2E, 0x44, 0x2D, 0xA6, 0xD6, 0x26, 0x64, 0xF6, 0x74, 0xA1,
            0xF3, 0x04, 0x92, 0x9A, 0xDE, 0x4F, 0x68, 0x93, 0xEF, 0x2D, 0xF6, 0xE7, 0x11, 0xA8, 0xC7, 0x7A,
            0x0D, 0x91, 0xC9, 0xD9, 0x80, 0x82, 0x2E, 0x50, 0xD1, 0x29, 0x22, 0xAF, 0xEA, 0x40, 0xEA, 0x9F,
            0x0E, 0x14, 0xC0, 0xF7, 0x69, 0x38, 0xC5, 0xF3, 0x88, 0x2F, 0xC0, 0x32, 0x3D, 0xD9, 0xFE, 0x55,
            0x15, 0x5F, 0x51, 0xBB, 0x59, 0x21, 0xC2, 0x01, 0x62, 0x9F, 0xD7, 0x33, 0x52, 0xD5, 0xE2, 0xEF,
            0xAA, 0xBF, 0x9B, 0xA0, 0x48, 0xD7, 0xB8, 0x13, 0xA2, 0xB6, 0x76, 0x7F, 0x6C, 0x3C, 0xCF, 0x1E,
            0xB4, 0xCE, 0x67, 0x3D, 0x03, 0x7B, 0x0D, 0x2E, 0xA3, 0x0C, 0x5F, 0xFF, 0xEB, 0x06, 0xF8, 0xD0,
            0x8A, 0xDD, 0xE4, 0x09, 0x57, 0x1A, 0x9C, 0x68, 0x9F, 0xEF, 0x10, 0x72, 0x88, 0x55, 0xDD, 0x8C,
            0xFB, 0x9A, 0x8B, 0xEF, 0x5C, 0x89, 0x43, 0xEF, 0x3B, 0x5F, 0xAA, 0x15, 0xDD, 0xE6, 0x98, 0xBE,
            0xDD, 0xF3, 0x59, 0x96, 0x03, 0xEB, 0x3E, 0x6F, 0x61, 0x37, 0x2B, 0xB6, 0x28, 0xF6, 0x55, 0x9F,
            0x59, 0x9A, 0x78, 0xBF, 0x50, 0x06, 0x87, 0xAA, 0x7F, 0x49, 0x76, 0xC0, 0x56, 0x2D, 0x41, 0x29,
            0x56, 0xF8, 0x98, 0x9E, 0x18, 0xA6, 0x35, 0x5B, 0xD8, 0x15, 0x97, 0x82, 0x5E, 0x0F, 0xC8, 0x75,
            0x34, 0x3E, 0xC7, 0x82, 0x11, 0x76, 0x25, 0xCD, 0xBF, 0x98, 0x44, 0x7B, 0x02, 0x03, 0x01, 0x00,
            0x01, 0x02, 0x82, 0x01, 0x01, 0x00, 0xE5, 0xF0, 0x0C, 0x72, 0xF5, 0x77, 0xD6, 0x04, 0xB9, 0xA4,
            0xCE, 0x41, 0x22, 0xAA, 0x84, 0xB0, 0x17, 0x43, 0xEC, 0x99, 0x5A, 0xCF, 0xCC, 0x7F, 0x4A, 0xB2,
            0x7C, 0x0B, 0x18, 0x7F, 0x90, 0x66, 0x5B, 0xE3, 0x59, 0xDF, 0x12, 0x59, 0x81, 0x8D, 0xEE, 0xED,
            0x79, 0xD3, 0xB1, 0xEF, 0x84, 0x5E, 0x4D, 0xDD, 0xDA, 0xC9, 0xA1, 0x55, 0x37, 0x3B, 0x5E, 0x27,
            0x0D, 0x8E, 0x13, 0x15, 0x00, 0x1A, 0x2E, 0x52, 0x7D, 0x54, 0xCD, 0xF9, 0x00, 0x0A, 0x57, 0x68,
            0xBC, 0x98, 0xD4, 0x44, 0x6B, 0x37, 0xBB, 0xBD, 0x00, 0xB2, 0x9D, 0xD8, 0xB5, 0x30, 0x62, 0x13,
            0x3B, 0x2A, 0x6E, 0x77, 0xF4, 0xEE, 0x32, 0x50, 0x56, 0x22, 0x90, 0x4D, 0xA7, 0x20, 0xFB, 0x1C,
            0x12, 0xC0, 0x39, 0x96, 0xDA, 0x71, 0x3A, 0x05, 0x06, 0x09, 0x8E, 0xDB, 0xED, 0xEC, 0xF9, 0x36,
            0xD0, 0xFA, 0x9C, 0xBD, 0x59, 0x29, 0xAB, 0xB0, 0xED, 0xA3, 0x57, 0x99, 0x50, 0x2F, 0x98, 0x94,
            0xDC, 0xB8, 0xFC, 0x56, 0x9A, 0x89, 0x2D, 0x17, 0x78, 0x03, 0x24, 0xA2, 0xB6, 0xC3, 0x16, 0x6E,
            0x34, 0x67, 0x09, 0x13, 0x4B, 0x85, 0x40, 0x41, 0xB8, 0x67, 0x70, 0x6B, 0x58, 0xFE, 0xF2, 0xA0,
            0xDB, 0x92, 0x2B, 0x77, 0x62, 0x8B, 0x68, 0xE6, 0x96, 0x93, 0xC7, 0xAF, 0x43, 0xBF, 0x2A, 0x73,
            0xD0, 0xB7, 0x32, 0x37, 0x7A, 0x0B, 0xA1, 0x7B, 0x44, 0xF0, 0x51, 0xE9, 0xBF, 0x79, 0x84, 0x9D,
            0xCB, 0x33, 0x32, 0x57, 0x1F, 0xD8, 0xA7, 0x09, 0x33, 0xC2, 0xD6, 0x0B, 0xDE, 0xC4, 0x79, 0x93,
            0x4A, 0x3D, 0xAC, 0xA4, 0x0B, 0xB6, 0xF2, 0xF3, 0x7C, 0x0A, 0x9D, 0x07, 0x10, 0x6E, 0xAD, 0xC8,
            0xB3, 0x69, 0xA0, 0x3F, 0x2F, 0x41, 0xC8, 0x80, 0x09, 0x8E, 0x8A, 0xDD, 0x46, 0x24, 0x0D, 0xAC,
            0x68, 0xCC, 0x53, 0x54, 0xF3, 0x61, 0x02, 0x81, 0x81, 0x00, 0xF7, 0xE0, 0xBF, 0x5A, 0x1E, 0x67,
            0x18, 0x31, 0x9A, 0x8B, 0x62, 0x09, 0xC3, 0x17, 0x14, 0x44, 0x04, 0x59, 0xF9, 0x73, 0x85, 0x66,
            0x13, 0xB1, 0x7A, 0xE1, 0x50, 0x8B, 0xB3, 0xE6, 0x31, 0x6E, 0x6B, 0x7F, 0x46, 0x2D, 0x2F, 0x7D,
            0x64, 0x41, 0x2B, 0x84, 0xB7, 0x6B, 0xC2, 0x3F, 0x2B, 0x0C, 0x35, 0x62, 0x45, 0x52, 0x79, 0xB2,
            0x43, 0xA9, 0xF7, 0x31, 0x6F, 0x95, 0x80, 0x07, 0xB3, 0x4C, 0x61, 0xF7, 0x68, 0xE2, 0xD4, 0x4E,
            0xD5, 0xFF, 0x2B, 0x27, 0x28, 0x17, 0xEC, 0x32, 0xB3, 0xE4, 0x93, 0x92, 0x92, 0x28, 0xFA, 0xE7,
            0x8E, 0x77, 0x4C, 0xA0, 0xF7, 0x5E, 0xBD, 0x69, 0xD5, 0x92, 0x02, 0x79, 0x8F, 0x11, 0x6E, 0x36,
            0x0C, 0x64, 0x38, 0xB3, 0x2E, 0x1B, 0xD8, 0xB9, 0xDC, 0x1E, 0x32, 0x32, 0xF0, 0xD3, 0x09, 0x18,
            0x88, 0x3C, 0xC4, 0x3E, 0xF8, 0xDD, 0xA2, 0x2C, 0x36, 0x91, 0x02, 0x81, 0x81, 0x00, 0xEF, 0x6F,
            0xFF, 0xF9, 0x94, 0xF1, 0xE5, 0x64, 0x41, 0xAA, 0x00, 0x35, 0xFD, 0x19, 0xA0, 0xC8, 0xD6, 0xF0,
            0x23, 0x78, 0xC7, 0x05, 0x80, 0xD9, 0xC4, 0x84, 0x20, 0x79, 0x1D, 0xF4, 0x07, 0xC5, 0x91, 0xFB,
            0x6E, 0xBF, 0xCA, 0x32, 0x2C, 0x30, 0x86, 0xDD, 0x90, 0x1F, 0xD2, 0xFA, 0xE1, 0xAE, 0xBB, 0x64,
            0xAD, 0xF6, 0xBB, 0x79, 0xFF, 0x80, 0x51, 0xBE, 0xBD, 0x0C, 0xD8, 0x20, 0xAB, 0x89, 0x87, 0x40,
            0x06, 0x01, 0xA7, 0xB2, 0xFE, 0x93, 0x90, 0xCA, 0xCC, 0x9A, 0xCA, 0xB8, 0xED, 0x2B, 0xF9, 0x1D,
            0x18, 0x6D, 0x8F, 0x69, 0x64, 0x3D, 0x7E, 0xFE, 0x0F, 0x5D, 0x56, 0xDF, 0x75, 0x77, 0xA2, 0xD0,
            0x35, 0xEA, 0x54, 0x13, 0xFC, 0x98, 0xD8, 0xF3, 0xF9, 0x08, 0xDA, 0x05, 0x9A, 0x37, 0x9D, 0xA4,
            0xB1, 0xCC, 0x38, 0xF1, 0x5D, 0x56, 0x0A, 0x83, 0xCC, 0x31, 0x71, 0x53, 0xC8, 0x4B, 0x02, 0x81,
            0x81, 0x00, 0xD0, 0xEB, 0xAF, 0xBC, 0x40, 0x25, 0xBA, 0x81, 0x8C, 0x75, 0x70, 0x23, 0x34, 0x38,
            0x4E, 0x8F, 0x69, 0x6F, 0x80, 0x4D, 0x7A, 0xA0, 0xE7, 0x76, 0x4E, 0x50, 0x7B, 0xB7, 0xD3, 0xDF,
            0xEF, 0xC7, 0xD6, 0x78, 0xC6, 0x68, 0x2D, 0x3F, 0xAD, 0x71, 0x34, 0x41, 0xBE, 0xEA, 0xE7, 0x24,
            0xA0, 0x9E, 0xC0, 0x9B, 0xDC, 0x3B, 0xC0, 0x70, 0x9C, 0x91, 0x33, 0xD4, 0x89, 0xEC, 0xE2, 0xA5,
            0x1A, 0xDD, 0x05, 0x31, 0x27, 0x49, 0x0F, 0x92, 0x86, 0xD1, 0x73, 0xC8, 0xA4, 0x05, 0x4D, 0xC2,
            0x0A, 0x57, 0x5C, 0x7E, 0x4C, 0x0C, 0x98, 0x34, 0xF4, 0xA1, 0xDE, 0x87, 0x49, 0x17, 0xA3, 0xE4,
            0x00, 0xEA, 0xF8, 0x85, 0x06, 0x2D, 0xB5, 0xCB, 0x7E, 0x34, 0x36, 0x89, 0xE7, 0x11, 0xF7, 0x5F,
            0xE7, 0x83, 0xD7, 0xE1, 0x91, 0x92, 0xFD, 0x76, 0x9C, 0xD5, 0x42, 0xBE, 0xA4, 0xB9, 0x01, 0x07,
            0xEC, 0xD1, 0x02, 0x81, 0x80, 0x7F, 0x40, 0x18, 0xDC, 0x7D, 0xEA, 0x29, 0x2D, 0xA5, 0x30, 0x42,
            0x38, 0x6F, 0x31, 0x05, 0xA0, 0x77, 0x8A, 0xDC, 0x6F, 0x3D, 0xE6, 0x90, 0xDA, 0x2B, 0x74, 0xC5,
            0x05, 0x59, 0x83, 0xED, 0xF5, 0x74, 0x66, 0x1A, 0x2F, 0xD7, 0xB7, 0xDE, 0x80, 0x53, 0xCC, 0xC0,
            0xE2, 0x08, 0xF0, 0xC8, 0xAC, 0x62, 0x6F, 0x59, 0x7D, 0x3D, 0x99, 0xD2, 0xCE, 0x51, 0xA3, 0x7B,
            0x39, 0xAE, 0x4B, 0x7E, 0x9E, 0xF2, 0xC0, 0x75, 0xF0, 0xBF, 0x3D, 0x83, 0xCA, 0xCD, 0x32, 0xDA,
            0x96, 0x91, 0x92, 0xC2, 0x89, 0x92, 0x35, 0x82, 0x5C, 0x07, 0xD1, 0xCD, 0x32, 0x59, 0xA1, 0x90,
            0x6C, 0xDC, 0xD4, 0x99, 0xCB, 0x61, 0x3E, 0x22, 0xC9, 0x4C, 0xB1, 0xEA, 0x97, 0x19, 0x06, 0x60,
            0x9D, 0xF1, 0xB0, 0xF4, 0x8B, 0x06, 0x3F, 0x17, 0x37, 0x20, 0x34, 0x36, 0x94, 0x99, 0xB5, 0xFD,
            0xF9, 0x70, 0xEF, 0x44, 0x0D, 0x02, 0x81, 0x81, 0x00, 0x90, 0x4E, 0xE9, 0x20, 0xF9, 0x44, 0xEF,
            0x5A, 0xAF, 0x7C, 0x94, 0x20, 0xA0, 0x0F, 0x5E, 0x9B, 0x48, 0x08, 0x2C, 0x0B, 0x84, 0xE0, 0xFB,
            0xB5, 0xDD, 0xA2, 0xA2, 0x26, 0x77, 0xDF, 0xB7, 0xB8, 0x48, 0x8D, 0xB2, 0xBE, 0xE6, 0x4C, 0x9B,
            0xDD, 0x3C, 0xAC, 0x66, 0xFA, 0x32, 0x0E, 0x76, 0xF7, 0x1C, 0xE2, 0xAF, 0x22, 0x72, 0xBB, 0xBD,
            0x76, 0xCA, 0xB9, 0x4E, 0x08, 0x4A, 0x0C, 0x41, 0xD9, 0xB0, 0x77, 0x1D, 0xC6, 0x33, 0x40, 0xC1,
            0xAC, 0xCF, 0x5A, 0x89, 0xDA, 0x01, 0xB4, 0x37, 0x98, 0x6F, 0x26, 0x9C, 0xF0, 0xC2, 0x16, 0xE1,
            0x5E, 0xA1, 0x4A, 0x03, 0x8C, 0xDA, 0x69, 0x2A, 0xF0, 0xEB, 0x6D, 0xB0, 0x0E, 0x78, 0x80, 0x2B,
            0x93, 0x25, 0x20, 0x4D, 0x2D, 0x20, 0x02, 0x8A, 0x3F, 0x8C, 0xB1, 0x34, 0x68, 0xE8, 0x0F, 0x64,
            0x18, 0x8E, 0x10, 0x46, 0xBA, 0x1B, 0xE4, 0x58, 0xA6, 0x97, 0x4F, 0x26
    };

//#warning NOTE: there is currently a problem with rand and PREDICT has been enabled in md_rand to temporarily overcome this

    const unsigned char *keypp = key_private;
    EVP_PKEY *key = d2i_AutoPrivateKey(0, &keypp, sizeof(key_private));

    if (key != NULL)
        iRsa = EVP_PKEY_get1_RSA(key);
    else
    {
        THROW(HttpError);
    }
    EVP_PKEY_free(key);
}

RaopDiscovery::~RaopDiscovery()
{
    delete iWriterResponse;
    delete iWriterRequest;
    delete iReaderRequest;
    delete iWriterAscii;
    delete iWriterBuffer;
    delete iReaderBuffer;
}

TBool RaopDiscovery::Active()
{
    return(iActive);
}

void RaopDiscovery::Run()
{
    LOG(kMedia, "RaopDiscovery::Run\n");
    iActive = false;

    iAeskeyPresent = false;

    LOG(kMedia, "RaopDiscovery::Run - Started, instance %d\n", iInstance);
    try {
        for (;;) {
            try {
                iReaderRequest->Read();

                KeepAlive();

                const Brx& method = iReaderRequest->Method();
                LOG(kMedia, "RaopDiscovery::Run - Read Method "); LOG(kMedia, method); LOG(kMedia, ", instance %d\n", iInstance);
                if(method == RtspMethod::kPost) {
                    Brn data(iReaderBuffer->Read(iHeaderContentLength.ContentLength()));

                    iWriterResponse->WriteStatus(HttpStatus::kOk, Http::eRtsp10);
                    iWriterResponse->WriteHeader(RtspHeader::kContentType, Brn("application/octet-stream"));
                    WriteSeq(iHeaderCSeq.CSeq());
                    WriteFply(data);
                    iWriterResponse->WriteFlush();
                }
                if(method == RtspMethod::kOptions) {
                    iWriterResponse->WriteStatus(HttpStatus::kOk, Http::eRtsp10);
                    if(iHeaderAppleChallenge.Received()) {
                        GenerateAppleResponse(iHeaderAppleChallenge.Challenge());   //encrypt challenge using rsa private key
                        iWriterResponse->WriteHeaderBase64(Brn("Apple-Response"), iResponse);
                        iHeaderAppleChallenge.Reset();
                    }
                    iWriterResponse->WriteHeader(Brn("Audio-Jack-Status"), Brn("connected; type=analog"));
                    WriteSeq(iHeaderCSeq.CSeq());
                    iWriterResponse->WriteHeader(RtspHeader::kPublic, Brn("ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER"));
                    iWriterResponse->WriteFlush();
                }
                else if(method == RtspMethod::kAnnounce) {
                    LOG(kMedia, "kAnnounce iActive = %d,  iInstance %d\n", iActive, iInstance);
                    if(!iActive && iProtocolRaop.Active()) { /*i.e. the other connection is active */
                        /* if already actively connected to another device respond with:
                        RTSP/1.0 453 Not Enough Bandwidth
                        Server: AirTunes/102.2
                        CSeq: 1
                        */
                        LOG(kMedia, "Reject second connection\n");
                        iWriterResponse->WriteStatus(RtspStatus::kNotEnoughBandwidth, Http::eRtsp10);
                        iWriterResponse->WriteHeader(Brn("Server"), Brn("AirTunes/102.2"));
                        WriteSeq(1);
                    }
                    else {
                        iProtocolRaop.Deactivate();     // deactivate both streams (effectively the other one!)
                        iActive = true;     // don't allow second stream to connect
                        LOG(kMedia, "Announce, instance %d\n", iInstance);
                        ReadSdp(iSdpInfo); //get encoded aes key
                        DecryptAeskey();
                        iWriterResponse->WriteStatus(HttpStatus::kOk, Http::eRtsp10);
                        iWriterResponse->WriteHeader(Brn("Audio-Jack-Status"), Brn("connected; type=analog"));
                        WriteSeq(iHeaderCSeq.CSeq());
                        if(iHeaderAppleChallenge.Received()) {
                            GenerateAppleResponse(iHeaderAppleChallenge.Challenge());   //encrypt challenge using rsa private key
                            iWriterResponse->WriteHeaderBase64(Brn("Apple-Response"), iResponse);
                            iHeaderAppleChallenge.Reset();
                            LOG(kMedia, "Challenge response, instance %d\n", iInstance);
                        }
                    }
                    iWriterResponse->WriteFlush();
                }
                else if(method == RtspMethod::kSetup) {
                    iWriterResponse->WriteStatus(HttpStatus::kOk, Http::eRtsp10);
                    iWriterResponse->WriteHeader(Brn("Audio-Jack-Status"), Brn("connected; type=analog"));
                    WriteSeq(iHeaderCSeq.CSeq());
                    iWriterResponse->WriteHeader(RtspHeader::kSession, Brn("1"));
                    iWriterResponse->WriteHeader(Brn("Transport"), Brn("RTP/AVP/UDP;unicast;mode=record;server_port=60400;control_port=60401;timing_port=60402"));
                    iWriterResponse->WriteFlush();
                }
                else if(method == RtspMethod::kRecord) {
                    iWriterResponse->WriteStatus(HttpStatus::kOk, Http::eRtsp10);
                    //iWriterResponse.WriteHeader(Brn("Audio-Latency"), Brn("15409"));  // has no effect on iTunes 
                    iWriterResponse->WriteHeader(Brn("Audio-Jack-Status"), Brn("connected; type=analog"));
                    WriteSeq(iHeaderCSeq.CSeq());
                    iWriterResponse->WriteFlush();

                    // select pairplay source
                    //iPairplaySource.Play();
                    LOG(kMedia, "RaopDiscovery::Run - Playing\n");
                }
                else if(method == RtspMethod::kSetParameter) {
                    if(iHeaderContentType.Type() == Brn("text/parameters")) {
                        Brn data(iReaderBuffer->Read(iHeaderContentLength.ContentLength()));
                        Parser parser(data);
                        Brn entry = parser.Next(':');
                        //if(Trim(entry) == Brn("volume")) {
                        if(entry == Brn("volume")) {
                            //try {
                            //    // volume range is -30 to 0, < -30 == mute
                            //    // get int part plus first digit after dp
                            //    Bws<10> vStr(parser.Next('.'));
                            //    vStr.Append(parser.At(0));

                            //    TInt vol = Int(vStr);
                            //    vol = 300 + vol; // range 0-300
                            //    if(vol < 0) {
                            //        vol = 0;	// muted
                            //    }
                            //    vol *= vol;		// convert from linear scale
                            //    vol /= 90; // range 0-1000

                            //    vol *= Linn::Media::Volume::kUnityGain;
                            //    vol /= 1000; // convert so that 100% = 256 for efficient scaling

                            //    LOG(kMedia, "raop volume ["); LOG(kMedia, vStr); LOG(kMedia, "] %d%\n", vol);
                            //    iVolume.Set(vol);
                            //}
                            //catch(AsciiError) {
                            //    // ignore entry if it can't be decoded
                            //}
                        }
                    }
                    else {
                        // need to purge non text data...  may be bitmap
                        TUint i;
                        for(i = iHeaderContentLength.ContentLength(); i > 10000; i -= 10000) {
                            iReaderBuffer->Read(10000);
                        }
                        iReaderBuffer->Read(i);
                    }

                    iWriterResponse->WriteStatus(HttpStatus::kOk, Http::eRtsp10);
                    iWriterResponse->WriteHeader(Brn("Audio-Jack-Status"), Brn("connected; type=analog"));
                    WriteSeq(iHeaderCSeq.CSeq());
                    iWriterResponse->WriteFlush();
                }
                else if(method == RtspMethod::kFlush) {
                    iWriterResponse->WriteStatus(HttpStatus::kOk, Http::eRtsp10);
                    iWriterResponse->WriteHeader(Brn("Audio-Jack-Status"), Brn("connected; type=analog"));
                    WriteSeq(iHeaderCSeq.CSeq());
                    iWriterResponse->WriteFlush();
                    //iPairplaySource.Play(); //restart
                }
                else if(method == RtspMethod::kTeardown) {
                    iWriterResponse->WriteStatus(HttpStatus::kOk, Http::eRtsp10);
                    iWriterResponse->WriteHeader(Brn("Audio-Jack-Status"), Brn("connected; type=analog"));
                    WriteSeq(iHeaderCSeq.CSeq());
                    iWriterResponse->WriteFlush();
                    Deactivate();
                    LOG(kMedia, "RaopDiscovery::Run - kTeardown\n");
                    return;
                }
            }
            catch (HttpError) {
                LOG(kMedia, "RaopDiscovery::Run - HttpError\n");
            }
        }
    }
    catch (ReaderError) {   // closed by client
        LOG(kMedia, "RaopDiscovery::Run - ReaderError\n");
    }
    catch (WriterError) {   // closed by client
        LOG(kMedia, "RaopDiscovery::Run - WriterError\n");
    }

    LOG(kMedia, "RaopDiscovery::Run - Exit iActive = %d\n", iActive);
}

void RaopDiscovery::Close()
{
    LOG(kMedia, "RaopDiscovery::Close iActive = %d, instance %d\n", iActive, iInstance);

    // set timeout and deactivate on expiry
    KeepAlive();
}

void RaopDiscovery::KeepAlive()
{
    if(iActive) {
        iDeactivateTimer->FireIn(10000);  // 10s timeout - deactivate of no data received
    }
}

void RaopDiscovery::DeactivateCallback()
{
    LOG(kMedia, "RaopDiscovery::DeactivateCallback\n");
    Deactivate();
    iReaderRequest->Interrupt(); //terminate run()
}


void RaopDiscovery::Deactivate()
{
    LOG(kMedia, "RaopDiscovery::Deactivate iActive = %d, instance %d\n", iActive, iInstance);
    iDeactivateTimer->Cancel();     // reset timeout
    iActive = false;
}


//encrypt challenge using rsa private key
void RaopDiscovery::GenerateAppleResponse(const Brx& aChallenge)
{
    GetRsa();

    unsigned char response[256];
    iResponse.SetBytes(0);

    unsigned char challenge[32];
    TUint i = 16;               // challenge is always 16 bytes
    memcpy(&challenge[0], aChallenge.Ptr(), i);

    TByte ip[4];
    iRaopDevice.GetEndpoint().GetAddressOctets(ip);
    // append ip address (must be actual IP address of device)
    for (TUint j=0; j<sizeof(ip); j++) {
        challenge[i++] = ip[j];
    }

    TByte mac[6];
    iRaopDevice.MacAddressOctets(mac);
    // append mac address (must be same as used in device name before @ symbol, but does not have to relate to device)
    for (TUint j=0; j<sizeof(mac); j++) {
        challenge[i++] = mac[j];
    }

    for (TUint j=0; j<6; j++) {     // shairport zero pads up to 32 bytes
        challenge[i++] = 0;
    }

    LOG(kMedia, "challenge:");
    for(TUint i = 0; i < 32; i++) {
        if(i % 16 == 0) { LOG(kMedia, "\n    ");}
        LOG(kMedia, "%02x, ", challenge[i]);
    }
    LOG(kMedia, "\n");

    TInt res = RSA_private_encrypt(32, challenge, response, iRsa, RSA_PKCS1_PADDING);

    LOG(kMedia, "encrypted response %d:", res);
    for(TInt i = 0; i < res; i++) {
        if(i % 16 == 0) { LOG(kMedia, "\n    ");}
        LOG(kMedia, "%02x, ", response[i]);
    }
    LOG(kMedia, "\n");

    if(res > 0) {
        iResponse.Replace(response, res);

        unsigned char decrypted[256];
        GetRsa();
        res = RSA_public_decrypt(256, response, decrypted, iRsa, RSA_PKCS1_PADDING);
        LOG(kMedia, "decrypted response %d:", res);
        for(TInt i = 0; i < res; i++) {
            if(i % 16 == 0) { LOG(kMedia, "\n    ");}
            LOG(kMedia, "%02x, ", decrypted[i]);
        }
        LOG(kMedia, "\n");
    }
}

void RaopDiscovery::DecryptAeskey()
{
    GetRsa();

    Brn rsaaeskey(iSdpInfo.Rsaaeskey());
    unsigned char aeskey[128];
    TInt res = RSA_private_decrypt(rsaaeskey.Bytes(), rsaaeskey.Ptr(), aeskey, iRsa, RSA_PKCS1_OAEP_PADDING);
    if(res > 0) {
        AES_KEY key;
        AES_set_decrypt_key(aeskey, 128, &key);
        iAeskey.Replace((const TByte* )&key, sizeof(AES_KEY));
        iAeskeyPresent = true;
        iAesSid++;
    }
}

TUint RaopDiscovery::AesSid()
{
    return iAesSid;
}

const Brx &RaopDiscovery::Aeskey()
{
    if(!iAeskeyPresent) {
        THROW(HttpError); // should be RoapError but need to add handling everywhere so just throw http, same as rtsp
    }
    return iAeskey;
}

const Brx &RaopDiscovery::Aesiv()
{
    if(!iAeskeyPresent) {
        THROW(HttpError); // should be RoapError but need to add handling everywhere so just throw http, same as rtsp
    }
    return iSdpInfo.Aesiv();
}

const Brx &RaopDiscovery::Fmtp()
{
    if(!iAeskeyPresent) {
        THROW(HttpError); // should be RoapError but need to add handling everywhere so just throw http, same as rtsp
    }
    return iSdpInfo.Fmtp();
}

void RaopDiscovery::ReadSdp(ISdpHandler& aSdpHandler)
{
    aSdpHandler.Reset();

    Brn line;

    TUint remaining = iHeaderContentLength.ContentLength();

    while (remaining > 0) {
        line.Set(iReaderBuffer->ReadUntil(Ascii::kLf));

        LOG(kHttp, "SDP: ");
        LOG(kHttp, line);
        LOG(kHttp, "\n");

        remaining -= line.Bytes() + 1;

        Parser parser(line);

        Brn type = parser.Next('=');
        Brn value = Ascii::Trim(parser.Remaining());

        if (type.Bytes() == 1) {
            aSdpHandler.Decode(type[0], value);
        }
    }
}


// HeaderCSeq

TBool HeaderCSeq::Received() const
{
    return (iReceived);
}

TUint HeaderCSeq::CSeq() const
{
    return (iCSeq);
}

TBool HeaderCSeq::Recognise(const Brx& aHeader)
{
    return (Ascii::CaseInsensitiveEquals(aHeader, Brn("CSeq")));
}

void HeaderCSeq::Reset()
{
    iReceived = false;
}

void HeaderCSeq::Process(const Brx& aValue)
{
    if (aValue.Bytes() > kMaxCSeqBytes) {
        THROW(HttpError);
    }

    Parser parser(aValue);
    Brn cseq = parser.Remaining();
    iCSeq = Ascii::Uint(cseq);
    iReceived = true;
}


// HeaderAppleChallenge

TBool HeaderAppleChallenge::Received() const
{
    return (iReceived);
}

const Brx& HeaderAppleChallenge::Challenge() const
{
    return (iChallenge);
}

TBool HeaderAppleChallenge::Recognise(const Brx& aHeader)
{
    return (Ascii::CaseInsensitiveEquals(aHeader, Brn("Apple-Challenge")));
}

void HeaderAppleChallenge::Reset()
{
    iReceived = false;
}

void HeaderAppleChallenge::Process(const Brx& aValue)
{
    if (aValue.Bytes() > kMaxChallengeBytes) {
        THROW(HttpError);
    }

    Parser parser(aValue);
    Brn challengeb64 = parser.Remaining();
    iChallenge.Replace(challengeb64);
    Converter::FromBase64(iChallenge);
    iReceived = true;
}
