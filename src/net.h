// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_NET_H
#define BITCOIN_NET_H

#include <openssl/rand.h>
#include <boost/array.hpp>
#include <boost/signals2/signal.hpp>
#include <deque>

#ifndef WIN32
#include <arpa/inet.h>
#endif

#include "addrman.h"
#include "hash.h"
#include "mruset.h"
#include "netbase.h"
#include "protocol.h"

class CNode;
class CBlockIndex;
extern int nBestHeight;

/** Time between pings automatically sent out for latency probing and keepalive (in seconds). */
static const int PING_INTERVAL = 2 * 60;
/** Time after which to disconnect, after waiting for a ping response (or inactivity). */
static const int TIMEOUT_INTERVAL = 20 * 60;

inline uint32_t ReceiveFloodSize() {
	return 1000 * GetArg("-maxreceivebuffer", 5 * 1000);
}
inline uint32_t SendBufferSize() {
	return 1000 * GetArg("-maxsendbuffer", 1 * 1000);
}

void           AddOneShot(std::string strDest);
bool           RecvLine(SOCKET hSocket, std::string& strLine);
void           AddressCurrentlyConnected(const CService& addr);
CNode*         FindNode(const CNetAddr& ip);
CNode*         FindNode(const std::string& addrName);
CNode*         FindNode(const CService& ip);
CNode*         ConnectNode(CAddress addrConnect, const char* strDest = NULL);
void           MapPort(bool fUseUPnP);
unsigned short GetListenPort();
bool           BindListenPort(const CService& bindAddr, std::string& strError = REF(std::string()));
void           StartNode(boost::thread_group& threadGroup);
bool           StopNode();
void           SocketSendData(CNode* pnode);

// Signals for message handling
struct CNodeSignals {
	boost::signals2::signal<bool(CNode*)>       ProcessMessages;
	boost::signals2::signal<bool(CNode*, bool)> SendMessages;
};

CNodeSignals& GetNodeSignals();

enum {
	LOCAL_NONE,    // unknown
	LOCAL_IF,      // address a local interface listens on
	LOCAL_BIND,    // address explicit bound to
	LOCAL_UPNP,    // address reported by UPnP
	LOCAL_MANUAL,  // address explicitly specified (-externalip=)

	LOCAL_MAX
};

bool     IsPeerAddrLocalGood(CNode* pnode);
void     AdvertizeLocal(CNode* pnode);
void     SetLimited(enum Network net, bool fLimited = true);
bool     IsLimited(enum Network net);
bool     IsLimited(const CNetAddr& addr);
bool     AddLocal(const CService& addr, int nScore = LOCAL_NONE);
bool     AddLocal(const CNetAddr& addr, int nScore = LOCAL_NONE);
bool     SeenLocal(const CService& addr);
bool     IsLocal(const CService& addr);
bool     GetLocal(CService& addr, const CNetAddr* paddrPeer = NULL);
bool     IsReachable(const CNetAddr& addr);
void     SetReachable(enum Network net, bool fFlag = true);
CAddress GetLocalAddress(const CNetAddr* paddrPeer = NULL);

enum {
	MSG_TX = 1,
	MSG_BLOCK,
};

extern bool     fDiscover;
extern uint64_t nLocalServices;
extern uint64_t nLocalHostNonce;
extern CAddrMan addrman;

extern std::vector<CNode*>                   vNodes;
extern CCriticalSection                      cs_vNodes;
extern std::map<CInv, CDataStream>           mapRelay;
extern std::deque<std::pair<int64_t, CInv> > vRelayExpiration;
extern CCriticalSection                      cs_mapRelay;
extern std::map<CInv, int64_t>               mapAlreadyAskedFor;

extern std::vector<std::string> vAddedNodes;
extern CCriticalSection         cs_vAddedNodes;

class CNodeStats {
public:
	uint64_t    nServices;
	int64_t     nLastSend;
	int64_t     nLastRecv;
	int64_t     nTimeConnected;
	int64_t     nTimeOffset;
	std::string addrName;
	int         nVersion;
	std::string strSubVer;
	bool        fInbound;
	int         nStartingHeight;
	int         nMisbehavior;
	uint64_t    nSendBytes;
	uint64_t    nRecvBytes;
	bool        fSyncNode;
	double      dPingTime;
	double      dPingWait;
	std::string addrLocal;
};

class CNodeShortStat {
public:
	std::string addrName;
	int         nVersion;
	std::string strSubVer;
	int         nStartingHeight;
	bool        operator==(const CNodeShortStat& b) const {
		       return addrName == b.addrName && nVersion == b.nVersion && strSubVer == b.strSubVer &&
		              nStartingHeight == b.nStartingHeight;
	}
};
typedef std::vector<CNodeShortStat> CNodeShortStats;

class CNetMessage {
public:
	bool in_data;  // parsing header (false) or data (true)

	CDataStream    hdrbuf;  // partially received header
	CMessageHeader hdr;     // complete header
	uint32_t       nHdrPos;

	CDataStream vRecv;  // received message data
	uint32_t    nDataPos;

	int64_t nTime;  // time (in microseconds) of message receipt.

	CNetMessage(int nTypeIn, int nVersionIn)
	    : hdrbuf(nTypeIn, nVersionIn), vRecv(nTypeIn, nVersionIn) {
		hdrbuf.resize(24);
		in_data  = false;
		nHdrPos  = 0;
		nDataPos = 0;
		nTime    = 0;
	}

	bool complete() const {
		if (!in_data)
			return false;
		return (hdr.nMessageSize == nDataPos);
	}

	void SetVersion(int nVersionIn) {
		hdrbuf.SetVersion(nVersionIn);
		vRecv.SetVersion(nVersionIn);
	}

	int readHeader(const char* pch, uint32_t nBytes);
	int readData(const char* pch, uint32_t nBytes);
};

/** Information about a peer */
class CNode {
public:
	// socket
	uint64_t                   nServices;
	SOCKET                     hSocket;
	CDataStream                ssSend;
	size_t                     nSendSize;    // total size of all vSendMsg entries
	size_t                     nSendOffset;  // offset inside the first vSendMsg already sent
	uint64_t                   nSendBytes;
	std::deque<CSerializeData> vSendMsg;
	CCriticalSection           cs_vSend;

	std::deque<CInv>        vRecvGetData;
	std::deque<CNetMessage> vRecvMsg;
	CCriticalSection        cs_vRecvMsg;
	uint64_t                nRecvBytes;
	int                     nRecvVersion;

	int64_t         nLastSend;
	int64_t         nLastRecv;
	int64_t         nTimeConnected;
	int64_t         nTimeOffset;
	CAddress        addr;
	std::string     addrName;
	CService        addrLocal;
	int             nVersion;
	std::string     strSubVer;
	bool            fOneShot;
	bool            fClient;
	bool            fInbound;
	bool            fNetworkNode;
	bool            fSuccessfullyConnected;
	bool            fDisconnect;
	CSemaphoreGrant grantOutbound;
	int             nRefCount;

protected:
	// Denial-of-service detection/prevention
	// Key is IP address, value is banned-until-time
	static std::map<CNetAddr, int64_t> setBanned;
	static CCriticalSection            cs_setBanned;
	int                                nMisbehavior;

public:
	uint256      hashContinue;
	CBlockIndex* pindexLastGetBlocksBegin;
	uint256      hashLastGetBlocksEnd;
	int          nStartingHeight;
	bool         fStartSync;

	// flood relay
	std::vector<CAddress> vAddrToSend;
	mruset<CAddress>      setAddrKnown;
	bool                  fGetAddr;
	std::set<uint256>     setKnown;

	// inventory based relay
	mruset<CInv>                 setInventoryKnown;
	std::vector<CInv>            vInventoryToSend;
	CCriticalSection             cs_inventory;
	std::multimap<int64_t, CInv> mapAskFor;

	// Ping time measurement:
	// The pong reply we're expecting, or 0 if no pong expected.
	uint64_t nPingNonceSent;
	// Time (in usec) the last ping was sent, or 0 if no ping was ever sent.
	int64_t nPingUsecStart;
	// Last measured round-trip time.
	int64_t nPingUsecTime;
	// Whether a ping is requested.
	bool fPingQueued;

	CNode(SOCKET hSocketIn, CAddress addrIn, std::string addrNameIn = "", bool fInboundIn = false)
	    : ssSend(SER_NETWORK, INIT_PROTO_VERSION), setAddrKnown(5000) {
		nServices                = 0;
		hSocket                  = hSocketIn;
		nRecvVersion             = INIT_PROTO_VERSION;
		nLastSend                = 0;
		nLastRecv                = 0;
		nSendBytes               = 0;
		nRecvBytes               = 0;
		nTimeConnected           = GetTime();
		nTimeOffset              = 0;
		addr                     = addrIn;
		addrName                 = addrNameIn == "" ? addr.ToStringIPPort() : addrNameIn;
		nVersion                 = 0;
		strSubVer                = "";
		fOneShot                 = false;
		fClient                  = false;  // set by version message
		fInbound                 = fInboundIn;
		fNetworkNode             = false;
		fSuccessfullyConnected   = false;
		fDisconnect              = false;
		nRefCount                = 0;
		nSendSize                = 0;
		nSendOffset              = 0;
		hashContinue             = 0;
		pindexLastGetBlocksBegin = 0;
		hashLastGetBlocksEnd     = 0;
		nStartingHeight          = -1;
		fStartSync               = false;
		fGetAddr                 = false;
		nMisbehavior             = 0;
		setInventoryKnown.max_size(SendBufferSize() / 1000);
		nPingNonceSent = 0;
		nPingUsecStart = 0;
		nPingUsecTime  = 0;
		fPingQueued    = false;

		// Be shy and don't send version until we hear
		if (hSocket != INVALID_SOCKET && !fInbound)
			PushVersion();
	}

	~CNode() {
		if (hSocket != INVALID_SOCKET) {
			closesocket(hSocket);
			hSocket = INVALID_SOCKET;
		}
	}

private:
	// Network usage totals
	static CCriticalSection cs_totalBytesRecv;
	static CCriticalSection cs_totalBytesSent;
	static uint64_t         nTotalBytesRecv;
	static uint64_t         nTotalBytesSent;

	CNode(const CNode&);
	void operator=(const CNode&);

public:
	int GetRefCount() {
		assert(nRefCount >= 0);
		return nRefCount;
	}

	// requires LOCK(cs_vRecvMsg)
	uint32_t GetTotalRecvSize() {
		uint32_t total = 0;
		for (const CNetMessage& msg : vRecvMsg) {
			total += msg.vRecv.size() + 24;
		}
		return total;
	}

	// requires LOCK(cs_vRecvMsg)
	bool ReceiveMsgBytes(const char* pch, uint32_t nBytes);

	// requires LOCK(cs_vRecvMsg)
	void SetRecvVersion(int nVersionIn) {
		nRecvVersion = nVersionIn;
		for (CNetMessage& msg : vRecvMsg) {
			msg.SetVersion(nVersionIn);
		}
	}

	CNode* AddRef() {
		nRefCount++;
		return this;
	}

	void Release() { nRefCount--; }

	void AddAddressKnown(const CAddress& addr) { setAddrKnown.insert(addr); }

	void PushAddress(const CAddress& addr) {
		// Known checking here is only to save space from duplicates.
		// SendMessages will filter it again for knowns that were added
		// after addresses were pushed.
		if (addr.IsValid() && !setAddrKnown.count(addr))
			vAddrToSend.push_back(addr);
	}

	void AddInventoryKnown(const CInv& inv) {
		{
			LOCK(cs_inventory);
			setInventoryKnown.insert(inv);
		}
	}

	void PushInventory(const CInv& inv) {
		{
			LOCK(cs_inventory);
			if (!setInventoryKnown.count(inv))
				vInventoryToSend.push_back(inv);
		}
	}

	void AskFor(const CInv& inv) {
		// We're using mapAskFor as a priority queue,
		// the key is the earliest time the request can be sent
		int64_t& nRequestTime = mapAlreadyAskedFor[inv];
		LogPrint("net", "askfor %s   %d (%s)\n", inv.ToString(), nRequestTime,
		         DateTimeStrFormat("%H:%M:%S", nRequestTime / 1000000));

		// Make sure not to reuse time indexes to keep things in the same order
		int64_t        nNow = (GetTime() - 1) * 1000000;
		static int64_t nLastTime;
		++nLastTime;
		nNow      = std::max(nNow, nLastTime);
		nLastTime = nNow;

		// Each retry is 2 minutes after the last
		nRequestTime = std::max(nRequestTime + 2 * 60 * 1000000, nNow);
		mapAskFor.insert(std::make_pair(nRequestTime, inv));
	}

	// TODO: Document the postcondition of this function.  Is cs_vSend locked?
	void BeginMessage(const char* pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSend) {
		ENTER_CRITICAL_SECTION(cs_vSend);
		assert(ssSend.size() == 0);
		ssSend << CMessageHeader(pszCommand, 0);
		LogPrint("net", "sending: %s ", pszCommand);
	}

	// TODO: Document the precondition of this function.  Is cs_vSend locked?
	void AbortMessage() UNLOCK_FUNCTION(cs_vSend) {
		ssSend.clear();

		LEAVE_CRITICAL_SECTION(cs_vSend);

		LogPrint("net", "(aborted)\n");
	}

	// TODO: Document the precondition of this function.  Is cs_vSend locked?
	void EndMessage() UNLOCK_FUNCTION(cs_vSend) {
		if (mapArgs.count("-dropmessagestest") &&
		    GetRand(atoi(mapArgs["-dropmessagestest"])) == 0) {
			LogPrint("net", "dropmessages DROPPING SEND MESSAGE\n");
			AbortMessage();
			return;
		}

		if (ssSend.size() == 0)
			return;

		// Set the size
		uint32_t nSize = ssSend.size() - CMessageHeader::HEADER_SIZE;
		memcpy((char*)&ssSend[CMessageHeader::MESSAGE_SIZE_OFFSET], &nSize, sizeof(nSize));

		// Set the checksum
		uint256  hash      = Hash(ssSend.begin() + CMessageHeader::HEADER_SIZE, ssSend.end());
		uint32_t nChecksum = 0;
		memcpy(&nChecksum, &hash, sizeof(nChecksum));
		assert(ssSend.size() >= CMessageHeader::CHECKSUM_OFFSET + sizeof(nChecksum));
		memcpy((char*)&ssSend[CMessageHeader::CHECKSUM_OFFSET], &nChecksum, sizeof(nChecksum));

		LogPrint("net", "(%d bytes)\n", nSize);

		std::deque<CSerializeData>::iterator it = vSendMsg.insert(vSendMsg.end(), CSerializeData());
		ssSend.GetAndClear(*it);
		nSendSize += (*it).size();

		// If write queue empty, attempt "optimistic write"
		if (it == vSendMsg.begin())
			SocketSendData(this);

		LEAVE_CRITICAL_SECTION(cs_vSend);
	}

	void PushVersion();

	void PushMessage(const char* pszCommand) {
		try {
			BeginMessage(pszCommand);
			EndMessage();
		} catch (...) {
			AbortMessage();
			throw;
		}
	}

	template <typename T1>
	void PushMessage(const char* pszCommand, const T1& a1) {
		try {
			BeginMessage(pszCommand);
			ssSend << a1;
			EndMessage();
		} catch (...) {
			AbortMessage();
			throw;
		}
	}

	template <typename T1, typename T2>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2) {
		try {
			BeginMessage(pszCommand);
			ssSend << a1 << a2;
			EndMessage();
		} catch (...) {
			AbortMessage();
			throw;
		}
	}

	template <typename T1, typename T2, typename T3>
	void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3) {
		try {
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3;
			EndMessage();
		} catch (...) {
			AbortMessage();
			throw;
		}
	}

	template <typename T1, typename T2, typename T3, typename T4>
	void PushMessage(const char* pszCommand,
	                 const T1&   a1,
	                 const T2&   a2,
	                 const T3&   a3,
	                 const T4&   a4) {
		try {
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4;
			EndMessage();
		} catch (...) {
			AbortMessage();
			throw;
		}
	}

	template <typename T1, typename T2, typename T3, typename T4, typename T5>
	void PushMessage(const char* pszCommand,
	                 const T1&   a1,
	                 const T2&   a2,
	                 const T3&   a3,
	                 const T4&   a4,
	                 const T5&   a5) {
		try {
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5;
			EndMessage();
		} catch (...) {
			AbortMessage();
			throw;
		}
	}

	template <typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
	void PushMessage(const char* pszCommand,
	                 const T1&   a1,
	                 const T2&   a2,
	                 const T3&   a3,
	                 const T4&   a4,
	                 const T5&   a5,
	                 const T6&   a6) {
		try {
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5 << a6;
			EndMessage();
		} catch (...) {
			AbortMessage();
			throw;
		}
	}

	template <typename T1,
	          typename T2,
	          typename T3,
	          typename T4,
	          typename T5,
	          typename T6,
	          typename T7>
	void PushMessage(const char* pszCommand,
	                 const T1&   a1,
	                 const T2&   a2,
	                 const T3&   a3,
	                 const T4&   a4,
	                 const T5&   a5,
	                 const T6&   a6,
	                 const T7&   a7) {
		try {
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7;
			EndMessage();
		} catch (...) {
			AbortMessage();
			throw;
		}
	}

	template <typename T1,
	          typename T2,
	          typename T3,
	          typename T4,
	          typename T5,
	          typename T6,
	          typename T7,
	          typename T8>
	void PushMessage(const char* pszCommand,
	                 const T1&   a1,
	                 const T2&   a2,
	                 const T3&   a3,
	                 const T4&   a4,
	                 const T5&   a5,
	                 const T6&   a6,
	                 const T7&   a7,
	                 const T8&   a8) {
		try {
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8;
			EndMessage();
		} catch (...) {
			AbortMessage();
			throw;
		}
	}

	template <typename T1,
	          typename T2,
	          typename T3,
	          typename T4,
	          typename T5,
	          typename T6,
	          typename T7,
	          typename T8,
	          typename T9>
	void PushMessage(const char* pszCommand,
	                 const T1&   a1,
	                 const T2&   a2,
	                 const T3&   a3,
	                 const T4&   a4,
	                 const T5&   a5,
	                 const T6&   a6,
	                 const T7&   a7,
	                 const T8&   a8,
	                 const T9&   a9) {
		try {
			BeginMessage(pszCommand);
			ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8 << a9;
			EndMessage();
		} catch (...) {
			AbortMessage();
			throw;
		}
	}

	bool IsSubscribed(uint32_t nChannel);
	void Subscribe(uint32_t nChannel, uint32_t nHops = 0);
	void CancelSubscribe(uint32_t nChannel);
	void CloseSocketDisconnect();

	// Denial-of-service detection/prevention
	// The idea is to detect peers that are behaving
	// badly and disconnect/ban them, but do it in a
	// one-coding-mistake-won't-shatter-the-entire-network
	// way.
	// IMPORTANT:  There should be nothing I can give a
	// node that it will forward on that will make that
	// node's peers drop it. If there is, an attacker
	// can isolate a node and/or try to split the network.
	// Dropping a node for sending stuff that is invalid
	// now but might be valid in a later version is also
	// dangerous, because it can cause a network split
	// between nodes running old code and nodes running
	// new code.
	static void ClearBanned();  // needed for unit testing
	static bool IsBanned(CNetAddr ip);
	bool        Misbehaving(int howmuch);  // 1 == a little, 100 == a lot
	void        copyStats(CNodeStats& stats);

	// Network stats
	static void RecordBytesRecv(uint64_t bytes);
	static void RecordBytesSent(uint64_t bytes);

	static uint64_t GetTotalBytesRecv();
	static uint64_t GetTotalBytesSent();
};

inline void RelayInventory(const CInv& inv) {
	// Put on lists to offer to the other nodes
	{
		LOCK(cs_vNodes);
		for (CNode* pnode : vNodes) {
			pnode->PushInventory(inv);
		}
	}
}

class CTransaction;
void RelayTransaction(const CTransaction& tx, const uint256& hash);
void RelayTransaction(const CTransaction& tx, const uint256& hash, const CDataStream& ss);

/** Access to the (IP) address database (peers.dat) */
class CAddrDB {
private:
	boost::filesystem::path pathAddr;

public:
	CAddrDB();
	bool Write(const CAddrMan& addr);
	bool Read(CAddrMan& addr);
};

#endif
