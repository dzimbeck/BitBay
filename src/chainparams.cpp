// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assert.h"

#include "base58.h"
#include "chainparams.h"
#include "main.h"
#include "util.h"

#include <boost/assign/list_of.hpp>

using namespace boost::assign;

struct SeedSpec6 {
	uint8_t  addr[16];
	uint16_t port;
};

#include "chainparamsseeds.h"

//
// Main network
//

// Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress>& vSeedsOut, const SeedSpec6* data, uint32_t count) {
	// It'll only connect to one or two seed nodes because once it connects,
	// it'll get a pile of addresses with newer timestamps.
	// Seed nodes are given a random 'last seen time' of between one and two
	// weeks ago.
	const int64_t nOneWeek = 7 * 24 * 60 * 60;
	for (uint32_t i = 0; i < count; i++) {
		struct in6_addr ip;
		memcpy(&ip, data[i].addr, sizeof(ip));
		CAddress addr(CService(ip, data[i].port));
		addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
		vSeedsOut.push_back(addr);
	}
}

class CMainParams : public CChainParams {
public:
	CMainParams() {
		// The message start string is designed to be unlikely to occur in normal data.
		// The characters are rarely used upper ASCII, not valid as UTF-8, and produce
		// a large 4-byte int at any alignment.
		pchMessageStart[0] = 0x70;
		pchMessageStart[1] = 0x35;
		pchMessageStart[2] = 0x22;
		pchMessageStart[3] = 0x05;
		vAlertPubKey       = ParseHex(
            "04ce8ee24c208237c7b1992f8a2a459360f2921d57b2026e5139e0065838d13a457a51632cef02d5d00d85"
		          "c3ae55dec8a61807ee75b3390b492f87f39f44199a4b");
		nDefaultPort            = 19914;
		nRPCPort                = 19915;
		nCoinbaseMaturity       = 50;
		nMinStakeConfirmations  = 120;
		nMaxReorganizationDepth = 1000;
		bnProofOfWorkLimit      = CBigNum(~uint256(0) >> 20);

		const char* pszTimestamp =
		    "BitBay gonna make an Impact on the altcoin market never seen before";
		CTransaction txNew;
		txNew.nTime = 1414351032;
		txNew.vin.resize(1);
		txNew.vout.resize(1);
		txNew.vin[0].scriptSig = CScript()
		                         << 0 << CBigNum(42)
		                         << vector<unsigned char>(
		                                (const unsigned char*)pszTimestamp,
		                                (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
		txNew.vout[0].SetEmpty();
		genesis.vtx.push_back(txNew);
		genesis.hashPrevBlock  = 0;
		genesis.hashMerkleRoot = genesis.BuildMerkleTree();
		genesis.nVersion       = 1;
		genesis.nTime          = 1414351032;
		genesis.nBits          = bnProofOfWorkLimit.GetCompact();
		genesis.nNonce         = 1491418;

		hashGenesisBlock = genesis.GetHash();
		assert(hashGenesisBlock ==
		       uint256("0x0000075685d3be1f253ce777174b1594354e79954d2a32a6f77fe9cba00e6467"));
		assert(genesis.hashMerkleRoot ==
		       uint256("0xd2b4345a1b1f0df76ab0cadfa1b44ca52270ff551c43e1b229d25873f0adc90d"));

		vSeeds.push_back(CDNSSeedData("seeder1", "dnsseed.bitbay.market"));
		vSeeds.push_back(CDNSSeedData("seeder2", "dnsseed.dynamicpeg.com"));

		// new nodes, alive 2017-12
		vSeeds.push_back(CDNSSeedData("bbaynode (node12)", "195.181.242.206"));
		vSeeds.push_back(CDNSSeedData("bbaynode (tokio,ys)", "151.236.221.10"));
		vSeeds.push_back(CDNSSeedData("bbaynode (london,ys)", "108.61.163.182"));
		vSeeds.push_back(CDNSSeedData("bbaynode (new1)", "94.102.52.66"));
		vSeeds.push_back(CDNSSeedData("bbaynode (new2)", "80.82.64.64"));

		vSeeds.push_back(CDNSSeedData("bbaynode (new3)", "45.79.94.206"));
		vSeeds.push_back(CDNSSeedData("bbaynode (new4)", "139.162.226.144"));
		vSeeds.push_back(CDNSSeedData("bbaynode (new5)", "172.104.25.65"));
		vSeeds.push_back(CDNSSeedData("bbaynode (new6)", "172.104.248.46"));
		vSeeds.push_back(CDNSSeedData("bbaynode (new7)", "172.105.241.170"));
		vSeeds.push_back(CDNSSeedData("bbaynode (new8)", "172.104.185.75"));

		// old nodes (can come online) in end of list to avoid network entering failures
		vSeeds.push_back(CDNSSeedData("bbaynode (nyc)", "104.236.208.150"));
		vSeeds.push_back(CDNSSeedData("bbaynode (amsterdam)", "188.166.39.223"));
		vSeeds.push_back(CDNSSeedData("bbaynode (singapore)", "128.199.118.67"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node1)", "104.255.33.162"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node2)", "194.135.84.161"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node3)", "23.227.190.163"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node4)", "45.56.109.7"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node5)", "104.172.24.79"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node6)", "106.187.50.153"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node7)", "158.69.27.82"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node8)", "24.37.11.106"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node9)", "40.112.149.192"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node10)", "69.254.222.98"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node11)", "85.25.146.74"));
		vSeeds.push_back(CDNSSeedData("bbaynode (node12)", "195.181.242.206"));

		base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 25);
		base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 85);
		base58Prefixes[SECRET_KEY]     = std::vector<unsigned char>(1, 153);
		base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E)
		                                     .convert_to_container<std::vector<unsigned char> >();
		base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4)
		                                     .convert_to_container<std::vector<unsigned char> >();
		base58Prefixes[ETH_ADDRESS] =
		    boost::assign::list_of(69)(84)(72).convert_to_container<std::vector<unsigned char> >();

		convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

		nLastPOWBlock = 10000;

		sPegInflateAddr  = "bNyZrPLQAMPvYedrVLDcBSd8fbLdNgnRPz";
		sPegDeflateAddr  = "bNyZrP2SbrV6v5HqeBoXZXZDE2e4fe6STo";
		sPegNochangeAddr = "bNyZrPeFFNP6GFJZCkE82DDN7JC4K5Vrkk";

		nPegFrozenTime  = (3600 * 24 * 30);
		nPegVFrozenTime = (3600 * 24 * 30 * 4);

		nPegInterval    = 200;
		nBridgeInterval = 1600;

		hashPegActivationTx.SetHex(
		    "0b00aa061b47d833204ef8d7debfc911d08456119fb236a2e966488263044ff6");

		//'TrustedStakers1':
		sTrustedStakers1Init.insert("bNgmCcxPKgQQqUe6rhNtbGWowMJFCuxjZ3");
		sTrustedStakers1Init.insert("bEfg9bMLSgmjB5TDiur9FscZij4XXrd8C5");
		sTrustedStakers1Init.insert("bTuZboysrngsaqJvRj4db4CV2Qa21Q5Jcb");
		sTrustedStakers1Init.insert("bMvqdtSZtxDDBEj6NBHAg38iCdvmFwALix");
		sTrustedStakers1Init.insert("bJvBcHh45A6mjfKhy8Qg9AagbWfHWB1abC");
		sTrustedStakers1Init.insert("bN2NGi2bF1cpcQcsxmY4daCpi2tQqW5tnS");
		sTrustedStakers1Init.insert("bS4MGJKwN3vWCSgmsmYfXoJzS3QHPCRtcB");
		sTrustedStakers1Init.insert("BJLZ29gAk9aGW9HoAnsEzqmWp6BX7tZEN8");
		sTrustedStakers1Init.insert("bP66u6L53PmFppSszfDnUN7dBh6jeNw1uJ");
		sTrustedStakers1Init.insert("BAaGqLMM8sFRvAzPAReAxDFbAwzBMQuTni");
		sTrustedStakers1Init.insert("B8FqutWoHU6ixFxceZDkqvWARLERuTc4DL");
		sTrustedStakers1Init.insert("bKu6ZW9QhaURfanGs66VW78LnHHfThbsJK");
		sTrustedStakers1Init.insert("BHPiTGr8FMW59J1iwTxi7h7uQrK2R34GzF");

		//'TrustedStakers2':
		sTrustedStakers2Init.insert("bZ8sJgk1VsgbNcqUqBY5hNR9kMaJ5kksEG");
		sTrustedStakers2Init.insert("bYf1uCCEc4Ge5juuHntYpJvuZ6L6fkqc9w");
		sTrustedStakers2Init.insert("bYCwwHbSGo85k86Bd5S7drLQ2m1EnUcqTq");
		sTrustedStakers2Init.insert("bURCwQiJhTSX2JA72LmPwCG3vF9zCpPB6J");
		sTrustedStakers2Init.insert("bUjrA5QmFntnGABYfeaHbSf5QKF1ptmztr");
		sTrustedStakers2Init.insert("bJMBgyS6u4SPFwJKGpcgWnPSRcuXF8iTme");
		sTrustedStakers2Init.insert("bFEEzRWNWKGxFmjAkT1SmjNs8VTD9eTYje");
		sTrustedStakers2Init.insert("bCqaStDHVoU89DWDjRxrsGbVFBWhxBFdP3");
		sTrustedStakers2Init.insert("bbn4mJawLC8C26gfw4TVAcfRftiRvb6hZz");
		sTrustedStakers2Init.insert("bamTjYPT5R822PLgVXUUUdYG6mQTnwmLtj");
		sTrustedStakers2Init.insert("bbaeKoaSbH23JP1PHM7Fa3oPAfDLQjA9fr");
		sTrustedStakers2Init.insert("bbbVueUaexGJgxkh2o2Eicd7nKDkuygGoc");
		sTrustedStakers2Init.insert("bE2sWfTAKR556uFwFjkeQcgMQTFZU2c5c6");
		sTrustedStakers2Init.insert("bG5WbMoXhMYEVa52ucZWjnidsqTidH7XoV");
		sTrustedStakers2Init.insert("bHcSb6MC3dxAZbyBSMtSfq81WUF8odrfs6");
		sTrustedStakers2Init.insert("bLRmZWd5mhE8H5AeSdXuRgwsdXAfEPRdDD");
		sTrustedStakers2Init.insert("bSg6gu7nH8aHwz2FTqfNF3h6TBExozfkMc");
		sTrustedStakers2Init.insert("bU7Fr7yrYJWgx6dTqpLW7Xs2Ztc7DBShNC");
		sTrustedStakers2Init.insert("bWVt3Qp1M2m3qNc2JgBcis6v2fu2ARoBzh");
		sTrustedStakers2Init.insert("bZT1vZsC123vFHpxwiXYTAt9k9kpfmhD9Y");
		sTrustedStakers2Init.insert("BGGVksKTGoemBpDTUJw9tVw9M2t7EtFfzz");
		sTrustedStakers2Init.insert("BKmirMrh6b5ku5scpc7AcJiTh8GSbc3aHR");
		sTrustedStakers2Init.insert("BRHq9ae4FGD2sgDjqhbJj1K5iszWxZsju8");
		sTrustedStakers2Init.insert("BEvukYqnXVw9Bj6q613igbBzeu7L8qydfZ");
		sTrustedStakers2Init.insert("BScLEZPVsLZHeHjciV9boq5j1i8VtcJNkV");
		sTrustedStakers2Init.insert("BGLCn3mQ4y8eMqm12cZmNtHohag2FvW5oc");
		sTrustedStakers2Init.insert("BNV91VFGsRHPSepK4WAS9Bg7ghK9T179mM");
		sTrustedStakers2Init.insert("BEpKZUcf7xCChU1xUgy9cCkou5Ujda1FTe");
		sTrustedStakers2Init.insert("BEdhKEgAT1TvF3NBpTHmnXDPTrxG8SqvPj");
		sTrustedStakers2Init.insert("BADnKcGJCFrvjhGxNjNgLw4pWmMLZTPDHm");
		sTrustedStakers2Init.insert("BAvARoTNQa4e3pZcpso9JMwJbDgLRV3kaG");
		sTrustedStakers2Init.insert("B9MWeWrJei6UCfeMN4yVnGSsjXR2fzPE7S");
		sTrustedStakers2Init.insert("BJq1ChAvpqMPQ35PA12T6cAvwksGW2zNMa");
		sTrustedStakers2Init.insert("B87SXHvyT1nco2ufyQjSfgDM8aCjutnqcJ");
		sTrustedStakers2Init.insert("BNWtRUezdG26bn3AKNwvG4He1X6tLbrqQj");
		sTrustedStakers2Init.insert("B8xvFpfLfLSadfmAv87JhqyGMjB86MD6Kz");
		sTrustedStakers2Init.insert("B5ERP1AVtwa7BrjSyw9saqWp2dVzypgmDX");
		sTrustedStakers2Init.insert("BF2o4AHkviLxH1ksxfMJr9PUY4mq94nXAe");
		sTrustedStakers2Init.insert("B6B7QJwDBCBnumDdVCusNaX9FecKMdeEPM");
		sTrustedStakers2Init.insert("BT8Kbtrqq9EWGAADGUKkrvFDN4GLoAZ5Xu");
		sTrustedStakers2Init.insert("BLwT7rbNPBDVqMLnBTfbt4ARpdexjM1U34");
		sTrustedStakers2Init.insert("BGLu8AzqiapcbufCabop4VWCzqZbYP2wJ8");
		sTrustedStakers2Init.insert("BNuv6rfyadJ8HjCGgBbYuE1AcNQZFQBuKs");
		sTrustedStakers2Init.insert("BHGUmQJZVN2vGjKCup2rBw6xn9b24FQaPh");
		sTrustedStakers2Init.insert("BBoGzB9UpLHP8XLNJNAm7iG7f3SgvCYGJa");
		sTrustedStakers2Init.insert("BTWKXR8Mi3s64bUaGVYnwL1XqmP6aTMWC3");
		sTrustedStakers2Init.insert("B6q2EoNLbDDabWoDawaZBwtAS3FURncaHq");
		sTrustedStakers2Init.insert("B515mPDfT4rTiLRUJFid2zrFRiGySxgj4Z");
		sTrustedStakers2Init.insert("BSmBt2aNscgogCShoetG9aiRbtLznD4HEU");
		sTrustedStakers2Init.insert("BNz1ZmfSaZwS2pduJ5QaVeBeUVnqwRPC1p");
		sTrustedStakers2Init.insert("B6mh9dJi5zVYH2coYeyEFbutWrt7389159");
		sTrustedStakers2Init.insert("BAuzwad1RErngpU4vs4TGoN61otqDq9eKE");
		sTrustedStakers2Init.insert("BMPh6mYvDcUrLxHJDxqopuNejZPYwz5C1s");
		sTrustedStakers2Init.insert("B8oTydfgHLZvA8n5UijXiht3f8mX3cSYEj");
		sTrustedStakers2Init.insert("BCfVrB6Wrec9H3LTuy6PunUXAwJEHBXbka");
		sTrustedStakers2Init.insert("B6dNMw2yd4LAiefePu7FaHGY3ALNZuMk3h");
		sTrustedStakers2Init.insert("BLaqBwjuvytkE1HYDCCKWsvJ9gPxNypPAf");
		sTrustedStakers2Init.insert("BAnifB1UKBMqV4hu9DtTZ7Qj4JBAEm8dKB");
		sTrustedStakers2Init.insert("BByxgD9v6YbxvmauuPdgqa8Yk2o5pekVPW");
		sTrustedStakers2Init.insert("BAJfus7iFaQ4rFSke5KzE367qzvf5R9thM");
		sTrustedStakers2Init.insert("BNFFzvTApN8JtFcWgjQHKCezKthuu6bDdv");
		sTrustedStakers2Init.insert("BCJxZgskT61557Jf2DmtwYvHJVaeRrwok6");
		sTrustedStakers2Init.insert("B6QNEmPwd3ZDdqWRp1o6cTXaDGnXwevkjA");
		sTrustedStakers2Init.insert("BDAXuYqpAjvP6P1rCQmvcBabbXhkg9KPSb");

		setTimeLockPassesInit.insert(
		    "023a168403ff82ee974c229143b6d441f22381776e26f9910b3a8d170dd4760a87");

		ConsensusVotes tconsensus{4, 0, 0};
		mapProposalConsensusInit[CChainParams::CONSENSUS_TSTAKERS] = tconsensus;
		ConsensusVotes cconsensus{4, 0, 0};
		mapProposalConsensusInit[CChainParams::CONSENSUS_CONSENSUS] = cconsensus;
		ConsensusVotes bconsensus{4, 0, 0};
		mapProposalConsensusInit[CChainParams::CONSENSUS_BRIDGE] = bconsensus;
		ConsensusVotes mconsensus{4, 0, 0};
		mapProposalConsensusInit[CChainParams::CONSENSUS_MERKLE] = mconsensus;
		ConsensusVotes xconsensus{4, 0, 0};
		mapProposalConsensusInit[CChainParams::CONSENSUS_TIMELOCKPASS] = xconsensus;
	}

	virtual const CBlock& GenesisBlock() const { return genesis; }
	virtual Network       NetworkID() const { return CChainParams::MAIN; }

	virtual const vector<CAddress>& FixedSeeds() const { return vFixedSeeds; }

protected:
	CBlock           genesis;
	vector<CAddress> vFixedSeeds;
};
static CMainParams* mainParams = nullptr;

//
// Testnet
//

class CTestNetParams : public CMainParams {
public:
	CTestNetParams() {
		// The message start string is designed to be unlikely to occur in normal data.
		// The characters are rarely used upper ASCII, not valid as UTF-8, and produce
		// a large 4-byte int at any alignment.
		pchMessageStart[0]      = 0xcd;
		pchMessageStart[1]      = 0xf2;
		pchMessageStart[2]      = 0xc0;
		pchMessageStart[3]      = 0xef;
		bnProofOfWorkLimit      = CBigNum(~uint256(0) >> 16);
		nCoinbaseMaturity       = 10;
		nMinStakeConfirmations  = 10;
		nMaxReorganizationDepth = 1000;
		vAlertPubKey            = ParseHex(
            "04ce8ee24c208237c7b1992f8a2a459360f2921d57b2026e5139e0065838d13a457a51632cef02d5d00d85"
		               "c3ae55dec8a61807ee75b3390b492f87f39f44199a4b");
		nDefaultPort = 21914;
		nRPCPort     = 21915;
		strDataDir   = "testnet";

		const char*  pszTimestamp = "BitBay testnet v2, bridge works";
		CTransaction txNew;
		txNew.nTime = 1699098243;
		txNew.vin.resize(1);
		txNew.vout.resize(1);
		txNew.vin[0].scriptSig = CScript()
		                         << 0 << CBigNum(42)
		                         << vector<unsigned char>(
		                                (const unsigned char*)pszTimestamp,
		                                (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
		txNew.vout[0].SetEmpty();
		genesis.vtx.push_back(txNew);
		genesis.hashPrevBlock  = 0;
		genesis.hashMerkleRoot = genesis.BuildMerkleTree();
		genesis.nVersion       = 1;
		genesis.nTime          = 1699098244;
		genesis.nBits          = bnProofOfWorkLimit.GetCompact();
		genesis.nNonce         = 1783678;

		hashGenesisBlock = genesis.GetHash();

		assert(hashGenesisBlock ==
		       uint256("b0c3a1bedca21fcd17ad826e83c6ed1bdfc0fe85a7a22f360af78f8de35c090f"));

		vFixedSeeds.clear();
		vSeeds.clear();

		vSeeds.push_back(CDNSSeedData("testnet7", "116.202.18.67"));
		vSeeds.push_back(CDNSSeedData("testnet4", "116.202.27.98"));
		vSeeds.push_back(CDNSSeedData("testnet6", "116.202.17.166"));

		base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 111);
		base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 196);
		base58Prefixes[SECRET_KEY]     = std::vector<unsigned char>(1, 239);
		base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF)
		                                     .convert_to_container<std::vector<unsigned char> >();
		base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94)
		                                     .convert_to_container<std::vector<unsigned char> >();
		base58Prefixes[ETH_ADDRESS] =
		    boost::assign::list_of(69)(84)(72).convert_to_container<std::vector<unsigned char> >();

		convertSeed6(vFixedSeeds, pnSeed6_test, ARRAYLEN(pnSeed6_test));

		nLastPOWBlock = 0x7fffffff;

		sPegInflateAddr  = "musA7Qzr98xjJH8e8BQBmWFkDMVBK63cCR";
		sPegDeflateAddr  = "moHwuGzcwREF1kcomvJXzxQ6BVAnEQWXsC";
		sPegNochangeAddr = "n4qKmAngtBJo5Pbu9FNy9jwwThThgjDAVH";

		nPegFrozenTime  = (3600 * 24);
		nPegVFrozenTime = (3600 * 24 * 4);

		hashPegActivationTx.SetHex(
		    "b380b0340d5a2f716422866fce63bf47446e40e41b0cf2d3b76dd6e7d8fa6be4");
		nPegInterval    = 20;
		nBridgeInterval = 400;

		sTrustedStakers1Init.clear();
		// 1
		sTrustedStakers1Init.insert("mo3U44xcfqx51R68psnxGFr1LCxju8DjQi");
		sTrustedStakers1Init.insert("mivQA1dktGgtRfomuMptiKkzrdKcvvV5vy");
		sTrustedStakers1Init.insert("mm3APdrhkE3oYtimAc5a9KZ2DQqMtr8QY1");
		sTrustedStakers1Init.insert("mmAUiSXH1haiJWcxvRmoHqFDarSunRhCcL");
		sTrustedStakers1Init.insert("mr3ynFZ1vnfQjE3LvmjwrM1YB76esdHfvg");
		sTrustedStakers1Init.insert("mhBpvYpZg6MHhsmNyRnUyGgmfJPrwmgmwJ");
		sTrustedStakers1Init.insert("n4TvARHFGGrs7XH1RaSeNgMD3iib1t6nq6");
		sTrustedStakers1Init.insert("mhn7waPmURMvTqoGcAGxJQy8VfLhesAkZK");
		sTrustedStakers1Init.insert("mnNDUtPy8rN69uCZmvZ43V6LycBctokTp4");
		sTrustedStakers1Init.insert("mrbzogHiJYsrsWyoNJLSq8pcCaJRt3HBRf");
		// 2
		sTrustedStakers1Init.insert("mns69BvE7mYKK8AfRxZdBCUBpVNFMQJTgq");
		sTrustedStakers1Init.insert("mkubTyr99kYUueuiFo6oSShQk5jGqfAaHs");
		sTrustedStakers1Init.insert("muYUjAh3QKjj5ckegBcF5v8VwroiiGEFZu");
		sTrustedStakers1Init.insert("n4Vn5Xw9zmXyM2yDWmu5wcaAnYrZKfuc4L");
		sTrustedStakers1Init.insert("n48yAuvrdhFPWrgndDLf2L4sPuJ4wKWiWh");
		sTrustedStakers1Init.insert("n2o6vJKbpSg6HmA2atxLeFKbpCaqP2NJsh");
		sTrustedStakers1Init.insert("muAwKLeNNK9xVJ5neWXUfRuDYo6f7DRR5S");
		sTrustedStakers1Init.insert("moTzgFQVD1HX5MbYKxk5YUiPXutLSgmLdr");
		sTrustedStakers1Init.insert("mhgFUeH8PdBFvauQqFRNDzU1qtjajtARY6");
		sTrustedStakers1Init.insert("mz8PVE1UZHaLT7LVyg6bwzJKi1pqYSrkF4");
		// 3
		sTrustedStakers1Init.insert("mhNPMgWrrukihpyRweEuk9sanVUVdmy1n7");
		sTrustedStakers1Init.insert("mtC5P4AaFziUmxp6oNxCqTi7r9zSnv4jA2");
		sTrustedStakers1Init.insert("mnjsJnv3hrczsyoXtQGAoviVzPXgKtaNMY");
		sTrustedStakers1Init.insert("mhXeUfa8VCP6FE314toUcNnsgmNmqv4gVk");
		sTrustedStakers1Init.insert("n3787RcHLPXLXvBCgaskHYXDoRxr8kr5sY");
		sTrustedStakers1Init.insert("msZmAHQ3dqQJHCAtUcatfguTh3uQ3hLZMZ");
		sTrustedStakers1Init.insert("mtVQSaQhbi23FqmAGzot45UDLCe68smQuj");
		sTrustedStakers1Init.insert("mwKSipUmvfEs2Ff3mfEjioZGNNbo4N22zw");
		sTrustedStakers1Init.insert("msgaXZMXUABUJGzGzTgFULoTNQNBfR4q87");
		sTrustedStakers1Init.insert("mhijwT8KYTivrQt918rUN44R7HJurL2QE8");
		// 4
		sTrustedStakers1Init.insert("mz3Bs2PbxzDbYoNt36BjfL7Z4SHjDGiCmw");
		sTrustedStakers1Init.insert("mnMY8qtgxnXmgTtJtNDJsQaN9KnmzyBLY4");
		sTrustedStakers1Init.insert("mmLWtM15JWmQBE1M53owLfmJxsKi4G5NZS");
		sTrustedStakers1Init.insert("moz6prmbu5YZMVip3XvBnmQyyR8rLwuKmQ");
		sTrustedStakers1Init.insert("mp43oJhqkSAQkoimzW6ntjUhtnq2FweM8N");
		sTrustedStakers1Init.insert("mnUAc4q5GVqLMaaHLiVrRDAjF6ULqu5ihY");
		sTrustedStakers1Init.insert("mocUPWm4aoNrhXr6pWq2ARTD3XGzTxwUTK");
		sTrustedStakers1Init.insert("mzmiRdcUt8oxo96HRguNFNSZyvNWy72Bdi");
		sTrustedStakers1Init.insert("n3H7LatKfc93xCMX6ByHYnNtNgW9kACAUJ");
		sTrustedStakers1Init.insert("n36p4G3F6SPA4AgboRT1J4if4RMhrPQ6so");
	}
	Network NetworkID() const override { return CChainParams::TESTNET; }

	int MinStakeConfirmations(int block) const override {
		if (block > 25000)
			return 30;
		return 10;
	}
};
static CTestNetParams* testNetParams = nullptr;

//
// Regression test
//
class CRegTestParams : public CTestNetParams {
public:
	CRegTestParams() {
		pchMessageStart[0]      = 0xfa;
		pchMessageStart[1]      = 0xbf;
		pchMessageStart[2]      = 0xb5;
		pchMessageStart[3]      = 0xda;
		bnProofOfWorkLimit      = CBigNum(~uint256(0) >> 1);
		nMinStakeConfirmations  = 120;
		nMaxReorganizationDepth = 1000;
		genesis.nTime           = 1411111111;
		genesis.nBits           = bnProofOfWorkLimit.GetCompact();
		genesis.nNonce          = 2;
		hashGenesisBlock        = genesis.GetHash();
		nDefaultPort            = 18444;
		strDataDir              = "regtest";
		// assert(hashGenesisBlock ==
		// uint256("0x523dda6d336047722cbaf1c5dce622298af791bac21b33bf6e2d5048b2a13e3d"));

		vSeeds.clear();  // Regtest mode doesn't have any DNS seeds.
	}

	virtual bool    RequireRPCPassword() const { return false; }
	virtual Network NetworkID() const { return CChainParams::REGTEST; }
};
static CRegTestParams* regTestParams = nullptr;

static CChainParams* pCurrentParams = mainParams;

const CChainParams& Params() {
	return *pCurrentParams;
}

CChainParams& ParamsRef() {
	return *pCurrentParams;
}

void InitParamsOnStart() {
	mainParams     = new CMainParams();
	testNetParams  = new CTestNetParams();
	regTestParams  = new CRegTestParams();
	pCurrentParams = mainParams;
}

void SelectParams(CChainParams::Network network) {
	switch (network) {
		case CChainParams::MAIN:
			pCurrentParams = mainParams;
			break;
		case CChainParams::TESTNET:
			pCurrentParams  = testNetParams;
			nPegStartHeight = 7000;
			break;
		case CChainParams::REGTEST:
			pCurrentParams = regTestParams;
			break;
		default:
			assert(false && "Unimplemented network");
			return;
	}
}

bool SelectParamsFromCommandLine() {
	bool fRegTest = GetBoolArg("-regtest", false);
	bool fTestNet = GetBoolArg("-testnet", false);
#ifdef USE_TESTNET
	fTestNet = true;
#endif

	if (fTestNet && fRegTest) {
		return false;
	}

	if (fRegTest) {
		SelectParams(CChainParams::REGTEST);
		fReopenDebugLog = true;
	} else if (fTestNet) {
		SelectParams(CChainParams::TESTNET);
		fReopenDebugLog = true;
	} else {
		SelectParams(CChainParams::MAIN);
	}
	return true;
}
