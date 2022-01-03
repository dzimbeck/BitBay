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
        nMaxReorganizationDepth = 100;
        bnProofOfWorkLimit = CBigNum(~uint256(0) >> 20);

        const char* pszTimestamp = "BitBay gonna make an Impact on the altcoin market never seen before";
        CTransaction txNew;
        txNew.nTime = 1414351032;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txNew.vin[0].scriptSig = CScript() 
                << 0 
                << CBigNum(42) 
                << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        txNew.vout[0].SetEmpty();
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock = 0;
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        genesis.nTime    = 1414351032;
        genesis.nBits    = bnProofOfWorkLimit.GetCompact();
        genesis.nNonce   = 1491418;

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x0000075685d3be1f253ce777174b1594354e79954d2a32a6f77fe9cba00e6467"));
        assert(genesis.hashMerkleRoot == uint256("0xd2b4345a1b1f0df76ab0cadfa1b44ca52270ff551c43e1b229d25873f0adc90d"));

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

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,25);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,85);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,153);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();

        convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

        nLastPOWBlock = 10000;
        
        sPegInflateAddr    = "bNyZrPLQAMPvYedrVLDcBSd8fbLdNgnRPz";
        sPegDeflateAddr    = "bNyZrP2SbrV6v5HqeBoXZXZDE2e4fe6STo";
        sPegNochangeAddr   = "bNyZrPeFFNP6GFJZCkE82DDN7JC4K5Vrkk";
        
        nPegFrozenTime  = (3600 * 24 * 30);
        nPegVFrozenTime = (3600 * 24 * 30 *4);
        
        nPegInterval = 200;
        
        hashPegActivationTx.SetHex("0b00aa061b47d833204ef8d7debfc911d08456119fb236a2e966488263044ff6");
        
        // 1
        sTrustedStakers.insert("bNgmCcxPKgQQqUe6rhNtbGWowMJFCuxjZ3");
        sTrustedStakers.insert("bEfg9bMLSgmjB5TDiur9FscZij4XXrd8C5");
        sTrustedStakers.insert("bTuZboysrngsaqJvRj4db4CV2Qa21Q5Jcb");
        sTrustedStakers.insert("bMvqdtSZtxDDBEj6NBHAg38iCdvmFwALix");
        sTrustedStakers.insert("bJvBcHh45A6mjfKhy8Qg9AagbWfHWB1abC");
        sTrustedStakers.insert("bN2NGi2bF1cpcQcsxmY4daCpi2tQqW5tnS");
        sTrustedStakers.insert("bS4MGJKwN3vWCSgmsmYfXoJzS3QHPCRtcB");
        // 2
        sTrustedStakers.insert("bZ8sJgk1VsgbNcqUqBY5hNR9kMaJ5kksEG");
        sTrustedStakers.insert("bYf1uCCEc4Ge5juuHntYpJvuZ6L6fkqc9w");
        sTrustedStakers.insert("bYCwwHbSGo85k86Bd5S7drLQ2m1EnUcqTq");
        sTrustedStakers.insert("bURCwQiJhTSX2JA72LmPwCG3vF9zCpPB6J");
        sTrustedStakers.insert("bUjrA5QmFntnGABYfeaHbSf5QKF1ptmztr");
        sTrustedStakers.insert("bJMBgyS6u4SPFwJKGpcgWnPSRcuXF8iTme");
        sTrustedStakers.insert("bFEEzRWNWKGxFmjAkT1SmjNs8VTD9eTYje");
        sTrustedStakers.insert("bCqaStDHVoU89DWDjRxrsGbVFBWhxBFdP3");
        sTrustedStakers.insert("bbn4mJawLC8C26gfw4TVAcfRftiRvb6hZz");
        // 3
        sTrustedStakers.insert("bamTjYPT5R822PLgVXUUUdYG6mQTnwmLtj");
        sTrustedStakers.insert("bbaeKoaSbH23JP1PHM7Fa3oPAfDLQjA9fr");
        sTrustedStakers.insert("bbbVueUaexGJgxkh2o2Eicd7nKDkuygGoc");
        sTrustedStakers.insert("bE2sWfTAKR556uFwFjkeQcgMQTFZU2c5c6");
        sTrustedStakers.insert("bG5WbMoXhMYEVa52ucZWjnidsqTidH7XoV");
        sTrustedStakers.insert("bHcSb6MC3dxAZbyBSMtSfq81WUF8odrfs6");
        sTrustedStakers.insert("bLRmZWd5mhE8H5AeSdXuRgwsdXAfEPRdDD");
        sTrustedStakers.insert("bSg6gu7nH8aHwz2FTqfNF3h6TBExozfkMc");
        sTrustedStakers.insert("bU7Fr7yrYJWgx6dTqpLW7Xs2Ztc7DBShNC");
        sTrustedStakers.insert("bWVt3Qp1M2m3qNc2JgBcis6v2fu2ARoBzh");
        sTrustedStakers.insert("bZT1vZsC123vFHpxwiXYTAt9k9kpfmhD9Y");
        // 4
        sTrustedStakers.insert("BGGVksKTGoemBpDTUJw9tVw9M2t7EtFfzz");
        sTrustedStakers.insert("BKmirMrh6b5ku5scpc7AcJiTh8GSbc3aHR");
        sTrustedStakers.insert("BRHq9ae4FGD2sgDjqhbJj1K5iszWxZsju8");
        sTrustedStakers.insert("BEvukYqnXVw9Bj6q613igbBzeu7L8qydfZ");
        sTrustedStakers.insert("BScLEZPVsLZHeHjciV9boq5j1i8VtcJNkV");
        sTrustedStakers.insert("BGLCn3mQ4y8eMqm12cZmNtHohag2FvW5oc");
        sTrustedStakers.insert("BNV91VFGsRHPSepK4WAS9Bg7ghK9T179mM");
        sTrustedStakers.insert("BEpKZUcf7xCChU1xUgy9cCkou5Ujda1FTe");
        sTrustedStakers.insert("BEdhKEgAT1TvF3NBpTHmnXDPTrxG8SqvPj");
        sTrustedStakers.insert("BADnKcGJCFrvjhGxNjNgLw4pWmMLZTPDHm");
        sTrustedStakers.insert("BAvARoTNQa4e3pZcpso9JMwJbDgLRV3kaG");
        sTrustedStakers.insert("B9MWeWrJei6UCfeMN4yVnGSsjXR2fzPE7S");
        sTrustedStakers.insert("BJq1ChAvpqMPQ35PA12T6cAvwksGW2zNMa");
        sTrustedStakers.insert("B87SXHvyT1nco2ufyQjSfgDM8aCjutnqcJ");
        sTrustedStakers.insert("BNWtRUezdG26bn3AKNwvG4He1X6tLbrqQj");
        sTrustedStakers.insert("B8xvFpfLfLSadfmAv87JhqyGMjB86MD6Kz");
        sTrustedStakers.insert("B5ERP1AVtwa7BrjSyw9saqWp2dVzypgmDX");
        sTrustedStakers.insert("BF2o4AHkviLxH1ksxfMJr9PUY4mq94nXAe");
        sTrustedStakers.insert("B6B7QJwDBCBnumDdVCusNaX9FecKMdeEPM");
        sTrustedStakers.insert("BT8Kbtrqq9EWGAADGUKkrvFDN4GLoAZ5Xu");
        sTrustedStakers.insert("BLwT7rbNPBDVqMLnBTfbt4ARpdexjM1U34");
        sTrustedStakers.insert("BGLu8AzqiapcbufCabop4VWCzqZbYP2wJ8");
        sTrustedStakers.insert("BNuv6rfyadJ8HjCGgBbYuE1AcNQZFQBuKs");
        sTrustedStakers.insert("BHGUmQJZVN2vGjKCup2rBw6xn9b24FQaPh");
        sTrustedStakers.insert("BBoGzB9UpLHP8XLNJNAm7iG7f3SgvCYGJa");
        sTrustedStakers.insert("BTWKXR8Mi3s64bUaGVYnwL1XqmP6aTMWC3");
        sTrustedStakers.insert("B6q2EoNLbDDabWoDawaZBwtAS3FURncaHq");
        sTrustedStakers.insert("B515mPDfT4rTiLRUJFid2zrFRiGySxgj4Z");
        sTrustedStakers.insert("BSmBt2aNscgogCShoetG9aiRbtLznD4HEU");
        sTrustedStakers.insert("BNz1ZmfSaZwS2pduJ5QaVeBeUVnqwRPC1p");
        sTrustedStakers.insert("B6mh9dJi5zVYH2coYeyEFbutWrt7389159");
        sTrustedStakers.insert("BAuzwad1RErngpU4vs4TGoN61otqDq9eKE");
        sTrustedStakers.insert("BMPh6mYvDcUrLxHJDxqopuNejZPYwz5C1s");
        sTrustedStakers.insert("B8oTydfgHLZvA8n5UijXiht3f8mX3cSYEj");
        sTrustedStakers.insert("BCfVrB6Wrec9H3LTuy6PunUXAwJEHBXbka");
        sTrustedStakers.insert("B6dNMw2yd4LAiefePu7FaHGY3ALNZuMk3h");
        sTrustedStakers.insert("BLaqBwjuvytkE1HYDCCKWsvJ9gPxNypPAf");
        sTrustedStakers.insert("BAnifB1UKBMqV4hu9DtTZ7Qj4JBAEm8dKB");
        sTrustedStakers.insert("BByxgD9v6YbxvmauuPdgqa8Yk2o5pekVPW");
        sTrustedStakers.insert("BAJfus7iFaQ4rFSke5KzE367qzvf5R9thM");
        // m
        sTrustedStakers.insert("bP66u6L53PmFppSszfDnUN7dBh6jeNw1uJ");
        sTrustedStakers.insert("BNFFzvTApN8JtFcWgjQHKCezKthuu6bDdv");
        sTrustedStakers.insert("BCJxZgskT61557Jf2DmtwYvHJVaeRrwok6");
        sTrustedStakers.insert("B6QNEmPwd3ZDdqWRp1o6cTXaDGnXwevkjA");
        sTrustedStakers.insert("BDAXuYqpAjvP6P1rCQmvcBabbXhkg9KPSb");
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
        nMaxReorganizationDepth = 100;
        vAlertPubKey = ParseHex("04ce8ee24c208237c7b1992f8a2a459360f2921d57b2026e5139e0065838d13a457a51632cef02d5d00d85c3ae55dec8a61807ee75b3390b492f87f39f44199a4b");
        nDefaultPort = 21914;
        nRPCPort = 21915;
        strDataDir = "testnet";

        genesis.hashPrevBlock = 0;
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        
        // Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.nBits  = bnProofOfWorkLimit.GetCompact();
        genesis.nNonce = 349114;
        
        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0000e6e0e97c71119aaeaba384fa62583818582299538b70ba80a047168e6852"));

        vFixedSeeds.clear();
        vSeeds.clear();
        
        vSeeds.push_back(CDNSSeedData("testnet1", "116.202.18.67"));
        vSeeds.push_back(CDNSSeedData("testnet2", "116.202.17.166"));
        vSeeds.push_back(CDNSSeedData("testnet3", "159.69.202.60"));
        vSeeds.push_back(CDNSSeedData("testnet4", "116.202.27.98"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

        convertSeed6(vFixedSeeds, pnSeed6_test, ARRAYLEN(pnSeed6_test));

        nLastPOWBlock = 0x7fffffff;
        
        sPegInflateAddr    = "n29zWn5WYvU6WRLBMvK49i2eoFRkxMMCdV";
        sPegDeflateAddr    = "mzbVXs9bQtS7i82gXrKEGEhWUvJRStNtRh";
        sPegNochangeAddr   = "mzCbx5ioAgyndeiMeDAPjFgp3xjUaiYvma";
        
        nPegFrozenTime  = (3600 * 24);
        nPegVFrozenTime = (3600 * 24 *4);
        
        hashPegActivationTx.SetHex("b380b0340d5a2f716422866fce63bf47446e40e41b0cf2d3b76dd6e7d8fa6be4");
    }
    Network NetworkID() const override { return CChainParams::TESTNET; }
    int PegInterval(int nHeight) const override { 
        if (nHeight >= 10000) {
            // at block 10K switch to 20 blocks interval
            return 20;
        }
        return nPegInterval; 
    } 
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
        nMaxReorganizationDepth = 100;
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
