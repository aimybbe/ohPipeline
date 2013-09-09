#ifndef HEADER_PIPELINE_UDPSERVER
#define HEADER_PIPELINE_UDPSERVER

#include <OpenHome/Private/Fifo.h>
#include <OpenHome/Private/Network.h>

namespace OpenHome {
namespace Media {

/**
 * Storage class for the output of a UdpSocketBase::Receive call
 */
class MsgUdp
{
public:
    MsgUdp(TUint aMaxSize);
    MsgUdp(TUint aMaxSize, const Endpoint& aEndpoint);
    ~MsgUdp();
    Bwx& Buffer();
    Endpoint& GetEndpoint();
    void Clear();
private:
    Bwh* iBuf;
    Endpoint iEndpoint;
};

/**
 * Class for a continuously running server which buffers packets while active
 * and discards packets when deactivated
 */
class SocketUdpServer : public SocketUdp
{
public:
    SocketUdpServer(Environment& aEnv, TUint aMaxSize, TUint aMaxPackets, TUint aPort = 0, TIpAddress aInterface = 0);
    ~SocketUdpServer();
    void Open();
    void Close();
public: // from SocketUdpBase
    Endpoint Receive(Bwx& aBuf);
private:
    void ClearAndRequeue(MsgUdp& aMsg);
    void CopyMsgToBuf(MsgUdp& aMsg, Bwx& aBuf, Endpoint& aEndpoint);
    void ServerThread();
    void CurrentAdapterChanged();
private:
    Environment& iEnv;
    TUint iMaxSize;
    TBool iOpen;
    Fifo<MsgUdp*> iFifoWaiting;
    Fifo<MsgUdp*> iFifoReady;
    Mutex iWaitingLock;
    Mutex iReadyLock;
    ThreadFunctor* iServerThread;
    TBool iQuit;
    TUint iAdapterListenerId;
};

/**
 * Class for managing a collection of SocketUdpServers. UdpServerManager owns
 * all the SocketUdpServers contained within it.
 */
class UdpServerManager
{
public:
    UdpServerManager(Environment& aEnv, TUint aMaxSize, TUint aMaxPackets);
    ~UdpServerManager();
    TUint CreateServer(TUint aPort = 0, TIpAddress aInterface = 0); // return ID of server
    SocketUdpServer& Find(TUint aId); // find server by ID
private:
    std::vector<SocketUdpServer*> iServers;
    Environment& iEnv;
    TUint iMaxSize;
    TUint iMaxPackets;
    Mutex iLock;
};

} // namespace Media
} // namespace OpenHome

#endif // HEADER_PIPELINE_UDPSERVER
