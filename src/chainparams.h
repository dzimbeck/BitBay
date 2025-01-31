// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAIN_PARAMS_H
#define BITCOIN_CHAIN_PARAMS_H

#include "bignum.h"
#include "uint256.h"
#include "util.h"

#include <vector>

using namespace std;

#define MESSAGE_START_SIZE 4
typedef unsigned char MessageStartChars[MESSAGE_START_SIZE];

class CAddress;
class CBlock;

struct CDNSSeedData {
	string name, host;
	CDNSSeedData(const string& strName, const string& strHost) : name(strName), host(strHost) {}
};

/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Bitcoin system. There are three: the main network on which people trade goods
 * and services, the public test network which gets reset from time to time and
 * a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams {
public:
	enum Network {
		MAIN,
		TESTNET,
		REGTEST,

		MAX_NETWORK_TYPES
	};

	enum Base58Type {
		PUBKEY_ADDRESS,
		SCRIPT_ADDRESS,
		SECRET_KEY,
		EXT_PUBLIC_KEY,
		EXT_SECRET_KEY,
		ETH_ADDRESS,

		MAX_BASE58_TYPES
	};

	const uint256&               HashGenesisBlock() const { return hashGenesisBlock; }
	const MessageStartChars&     MessageStart() const { return pchMessageStart; }
	const vector<unsigned char>& AlertKey() const { return vAlertPubKey; }
	int                          GetDefaultPort() const { return nDefaultPort; }
	const CBigNum&               ProofOfWorkLimit() const { return bnProofOfWorkLimit; }
	int                          CoinbaseMaturity() const { return nCoinbaseMaturity; }
	virtual int           MinStakeConfirmations(int block) const { return nMinStakeConfirmations; }
	int                   MaxReorganizationDepth() const { return nMaxReorganizationDepth; }
	int                   SubsidyHalvingInterval() const { return nSubsidyHalvingInterval; }
	virtual const CBlock& GenesisBlock() const = 0;
	virtual bool          RequireRPCPassword() const { return true; }
	const string&         DataDir() const { return strDataDir; }
	virtual Network       NetworkID() const = 0;
	const vector<CDNSSeedData>&       DNSSeeds() const { return vSeeds; }
	const std::vector<unsigned char>& Base58Prefix(Base58Type type) const {
		return base58Prefixes[type];
	}
	virtual const vector<CAddress>& FixedSeeds() const = 0;
	int                             RPCPort() const { return nRPCPort; }
	int                             LastPOWBlock() const { return nLastPOWBlock; }

	string PegInflateAddr() const { return sPegInflateAddr; }
	string PegDeflateAddr() const { return sPegDeflateAddr; }
	string PegNochangeAddr() const { return sPegNochangeAddr; }

	int PegFrozenTime() const { return nPegFrozenTime; }
	int PegVFrozenTime() const { return nPegVFrozenTime; }

	int     PegInterval() const { return nPegInterval; }
	uint256 PegActivationTxhash() const { return hashPegActivationTx; }

	int BridgeInterval() const { return nBridgeInterval; }

	std::set<string> sTrustedStakers1Init;
	std::set<string> sTrustedStakers2Init;
	std::set<string> setTimeLockPassesInit;

	enum ConsensusTypes {
		CONSENSUS_TSTAKERS = 1,
		CONSENSUS_CONSENSUS,
		CONSENSUS_BRIDGE,
		CONSENSUS_MERKLE,
		CONSENSUS_TIMELOCKPASS,
	};
	struct ConsensusVotes {
		int tstakers1;
		int tstakers2;
		int ostakers;
	};
	std::map<int, ConsensusVotes> mapProposalConsensusInit;
	static bool isAccepted(const ConsensusVotes& voted, const ConsensusVotes& needed) {
		return voted.tstakers1 >= needed.tstakers1 && voted.tstakers2 >= needed.tstakers2 &&
		       voted.ostakers >= needed.ostakers;
	}

	enum AcceptedStatesTypes {
		ACCEPTED_TSTAKERS1 = 1,
		ACCEPTED_TSTAKERS2,
		ACCEPTED_CONSENSUS,
		ACCEPTED_BRIDGES,
		ACCEPTED_MERKLES,
		ACCEPTED_TIMELOCKPASSES,
		ACCEPTED_BRIDGES_PAUSE,
	};

protected:
	CChainParams() {}

	uint256           hashGenesisBlock;
	MessageStartChars pchMessageStart;
	// Raw pub key bytes for the broadcast alert signing key.
	vector<unsigned char>      vAlertPubKey;
	int                        nDefaultPort;
	int                        nRPCPort;
	CBigNum                    bnProofOfWorkLimit;
	int                        nCoinbaseMaturity;
	int                        nMinStakeConfirmations;
	int                        nMaxReorganizationDepth;
	int                        nSubsidyHalvingInterval;
	string                     strDataDir;
	vector<CDNSSeedData>       vSeeds;
	std::vector<unsigned char> base58Prefixes[MAX_BASE58_TYPES];
	int                        nLastPOWBlock;

	string sPegInflateAddr;
	string sPegDeflateAddr;
	string sPegNochangeAddr;

	int nPegFrozenTime;
	int nPegVFrozenTime;

	int     nPegInterval;
	uint256 hashPegActivationTx;
	int     nBridgeInterval;
};

/**
 * Return the currently selected parameters. This won't change after app startup
 * outside of the unit tests.
 */
const CChainParams& Params();
CChainParams&       ParamsRef();

/**
 * Init for main, test and regtest params
 */
void InitParamsOnStart();

/** Sets the params returned by Params() to those for the given network. */
void SelectParams(CChainParams::Network network);

/**
 * Looks for -regtest or -testnet and then calls SelectParams as appropriate.
 * Returns false if an invalid combination is given.
 */
bool SelectParamsFromCommandLine();

inline bool TestNet() {
	// Note: it's deliberate that this returns "false" for regression test mode.
	return Params().NetworkID() == CChainParams::TESTNET;
}

#endif
