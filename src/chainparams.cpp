// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assert.h"

#include "chainparams.h"
#include "main.h"
#include "util.h"

#include <boost/assign/list_of.hpp>

using namespace boost::assign;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

//
// Main network
//

// Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress> &vSeedsOut, const SeedSpec6 *data, unsigned int count)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7*24*60*60;
    for (unsigned int i = 0; i < count; i++)
    {
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
        vAlertPubKey = ParseHex("04ce8ee24c208237c7b1992f8a2a459360f2921d57b2026e5139e0065838d13a457a51632cef02d5d00d85c3ae55dec8a61807ee75b3390b492f87f39f44199a4b");
        nDefaultPort = 19914;
        nRPCPort = 19915;
        bnProofOfWorkLimit = CBigNum(~uint256(0) >> 20);

        // Build the genesis block. Note that the output of the genesis coinbase cannot
        // be spent as it did not originally exist in the database.
        //
        //CBlock(hash=000001faef25dec4fbcf906e6242621df2c183bf232f263d0ba5b101911e4563, ver=1, hashPrevBlock=0000000000000000000000000000000000000000000000000000000000000000, hashMerkleRoot=12630d16a97f24b287c8c2594dda5fb98c9e6c70fc61d44191931ea2aa08dc90, nTime=1393221600, nBits=1e0fffff, nNonce=164482, vtx=1, vchBlockSig=)
        //  Coinbase(hash=12630d16a9, nTime=1393221600, ver=1, vin.size=1, vout.size=1, nLockTime=0)
        //    CTxIn(COutPoint(0000000000, 4294967295), coinbase 00012a24323020466562203230313420426974636f696e2041544d7320636f6d6520746f20555341)
        //    CTxOut(empty)
        //  vMerkleTree: 12630d16a9

        //Change the text and nTime below if you wish to make a new genesis
        const char* pszTimestamp = "BitBay gonna make an Impact on the altcoin market never seen before";
        CTransaction txNew;
        txNew.nTime = 1414351032;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txNew.vin[0].scriptSig = CScript() << 0 << CBigNum(42) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        txNew.vout[0].SetEmpty();
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock = 0;
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        genesis.nTime    = 1414351032;
        genesis.nBits    = bnProofOfWorkLimit.GetCompact();
        genesis.nNonce   = 1491418;

        // Uncommenting this will make a new genesis which you can change the info above
        //uint256 hashTarget = CBigNum().SetCompact(genesis.nBits).getuint256();
        //printf("Making genesis");
        //while (genesis.GetHash() > hashTarget)
        //{
        //  ++genesis.nNonce;
        //  if (genesis.nNonce == 0)
        //             {
        //                  printf("NONCE WRAPPED, incrementing time");
        //                  ++genesis.nTime;
        //             }
        //}
        //printf("genesis.GetHash() == %s\n", genesis.GetHash().ToString().c_str());
        //printf("genesis.hashMerkleRoot == %s\n", genesis.hashMerkleRoot.ToString().c_str());
        //printf("genesis.nTime = %u \n", genesis.nTime);
        //printf("genesis.nNonce = %u \n", genesis.nNonce);
        //hashGenesisBlock = genesis.GetHash();
        //assert(hashGenesisBlock == uint256("0x0"));

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x0000075685d3be1f253ce777174b1594354e79954d2a32a6f77fe9cba00e6467"));
        assert(genesis.hashMerkleRoot == uint256("0xd2b4345a1b1f0df76ab0cadfa1b44ca52270ff551c43e1b229d25873f0adc90d"));

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

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,25);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,85);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,153);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();

        convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

        nLastPOWBlock = 10000;
    }

    virtual const CBlock& GenesisBlock() const { return genesis; }
    virtual Network NetworkID() const { return CChainParams::MAIN; }

    virtual const vector<CAddress>& FixedSeeds() const {
        return vFixedSeeds;
    }
protected:
    CBlock genesis;
    vector<CAddress> vFixedSeeds;
};
static CMainParams *mainParams = nullptr;


//
// Testnet
//

class CTestNetParams : public CMainParams {
public:
    CTestNetParams() {
        // The message start string is designed to be unlikely to occur in normal data.
        // The characters are rarely used upper ASCII, not valid as UTF-8, and produce
        // a large 4-byte int at any alignment.
        pchMessageStart[0] = 0xcd;
        pchMessageStart[1] = 0xf2;
        pchMessageStart[2] = 0xc0;
        pchMessageStart[3] = 0xef;
        bnProofOfWorkLimit = CBigNum(~uint256(0) >> 16);
        vAlertPubKey = ParseHex("04ce8ee24c208237c7b1992f8a2a459360f2921d57b2026e5139e0065838d13a457a51632cef02d5d00d85c3ae55dec8a61807ee75b3390b492f87f39f44199a4b");
        nDefaultPort = 21914;
        nRPCPort = 21915;
        strDataDir = "testnet";

        // Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.nBits  = bnProofOfWorkLimit.GetCompact();
        genesis.nNonce = 216178;
        hashGenesisBlock = genesis.GetHash();
        //assert(hashGenesisBlock == uint256("0x000012424a3b8d8c1a4ae9ca4426d67a1678c26c4e8cc6e72cb368d687dc07dd"));

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();


        convertSeed6(vFixedSeeds, pnSeed6_test, ARRAYLEN(pnSeed6_test));

        nLastPOWBlock = 0x7fffffff;
    }
    virtual Network NetworkID() const { return CChainParams::TESTNET; }
};
static CTestNetParams *testNetParams = nullptr;


//
// Regression test
//
class CRegTestParams : public CTestNetParams {
public:
    CRegTestParams() {
        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        bnProofOfWorkLimit = CBigNum(~uint256(0) >> 1);
        genesis.nTime = 1411111111;
        genesis.nBits  = bnProofOfWorkLimit.GetCompact();
        genesis.nNonce = 2;
        hashGenesisBlock = genesis.GetHash();
        nDefaultPort = 18444;
        strDataDir = "regtest";
        //assert(hashGenesisBlock == uint256("0x523dda6d336047722cbaf1c5dce622298af791bac21b33bf6e2d5048b2a13e3d"));

        vSeeds.clear();  // Regtest mode doesn't have any DNS seeds.
    }

    virtual bool RequireRPCPassword() const { return false; }
    virtual Network NetworkID() const { return CChainParams::REGTEST; }
};
static CRegTestParams *regTestParams = nullptr;

static CChainParams *pCurrentParams = mainParams;

const CChainParams &Params() {
    return *pCurrentParams;
}

void InitParamsOnStart() {
	mainParams = new CMainParams();
	testNetParams = new CTestNetParams(); 
	regTestParams = new CRegTestParams();
	pCurrentParams = mainParams;
}

void SelectParams(CChainParams::Network network) {
    switch (network) {
        case CChainParams::MAIN:
            pCurrentParams = mainParams;
            break;
        case CChainParams::TESTNET:
            pCurrentParams = testNetParams;
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

    if (fTestNet && fRegTest) {
        return false;
    }

    if (fRegTest) {
        SelectParams(CChainParams::REGTEST);
    } else if (fTestNet) {
        SelectParams(CChainParams::TESTNET);
    } else {
        SelectParams(CChainParams::MAIN);
    }
    return true;
}
