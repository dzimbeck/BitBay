// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2018-2020 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>

#include "rpcserver.h"
#include "main.h"
#include "base58.h"
#include "kernel.h"
#include "checkpoints.h"
#include "txdb-leveldb.h"
#include "pegdb-leveldb.h"

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;

extern void TxToJSON(const CTransaction& tx, 
                     const uint256 hashBlock, 
                     const MapFractions&,
                     int nSupply,
                     json_spirit::Object& entry);

double GetDifficulty(const CBlockIndex* blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (pindexBest == NULL)
            return 1.0;
        else
            blockindex = GetLastBlockIndex(pindexBest, false);
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

double GetPoWMHashPS()
{
    if (pindexBest->nHeight >= LAST_POW_TIME)
        return 0;

    int nPoWInterval = 72;
    int64_t nTargetSpacingWorkMin = 30, nTargetSpacingWork = 30;

    CBlockIndex* pindex = pindexGenesisBlock;
    CBlockIndex* pindexPrevWork = pindexGenesisBlock;

    while (pindex)
    {
        if (pindex->IsProofOfWork())
        {
            int64_t nActualSpacingWork = pindex->GetBlockTime() - pindexPrevWork->GetBlockTime();
            nTargetSpacingWork = ((nPoWInterval - 1) * nTargetSpacingWork + nActualSpacingWork + nActualSpacingWork) / (nPoWInterval + 1);
            nTargetSpacingWork = max(nTargetSpacingWork, nTargetSpacingWorkMin);
            pindexPrevWork = pindex;
        }

        pindex = pindex->pnext;
    }

    return GetDifficulty() * 4294.967296 / nTargetSpacingWork;
}

double GetPoSKernelPS()
{
    int nPoSInterval = 72;
    double dStakeKernelsTriedAvg = 0;
    int nStakesHandled = 0, nStakesTime = 0;

    CBlockIndex* pindex = pindexBest;;
    CBlockIndex* pindexPrevStake = NULL;

    while (pindex && nStakesHandled < nPoSInterval)
    {
        if (pindex->IsProofOfStake())
        {
            if (pindexPrevStake)
            {
                dStakeKernelsTriedAvg += GetDifficulty(pindexPrevStake) * 4294967296.0;
                nStakesTime += pindexPrevStake->nTime - pindex->nTime;
                nStakesHandled++;
            }
            pindexPrevStake = pindex;
        }

        pindex = pindex->pprev;
    }

    double result = 0;

    if (nStakesTime)
        result = dStakeKernelsTriedAvg / nStakesTime;

    if (IsProtocolV2(nBestHeight))
        result *= STAKE_TIMESTAMP_MASK + 1;

    return result;
}

Object blockToJSON(const CBlock& block, const CBlockIndex* blockindex, const MapFractions & mapFractions, bool fPrintTransactionDetail)
{
    Object result;
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (blockindex->IsInMainChain())
        confirmations = nBestHeight - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(Pair("mint", ValueFromAmount(blockindex->nMint)));
    result.push_back(Pair("time", (int64_t)block.GetBlockTime()));
    result.push_back(Pair("nonce", (uint64_t)block.nNonce));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("blocktrust", leftTrim(blockindex->GetBlockTrust().GetHex(), '0')));
    result.push_back(Pair("chaintrust", leftTrim(blockindex->nChainTrust.GetHex(), '0')));
    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    if (blockindex->pnext)
        result.push_back(Pair("nextblockhash", blockindex->pnext->GetBlockHash().GetHex()));

    result.push_back(Pair("flags", strprintf("%s%s", blockindex->IsProofOfStake()? "proof-of-stake" : "proof-of-work", blockindex->GeneratedStakeModifier()? " stake-modifier": "")));
    result.push_back(Pair("proofhash", blockindex->hashProof.GetHex()));
    result.push_back(Pair("entropybit", (int)blockindex->GetStakeEntropyBit()));
    result.push_back(Pair("modifier", strprintf("%016x", blockindex->nStakeModifier)));
    result.push_back(Pair("modifierv2", blockindex->bnStakeModifierV2.GetHex()));
    result.push_back(Pair("pegsupplyindex", blockindex->nPegSupplyIndex));
    result.push_back(Pair("pegvotesinflate", blockindex->nPegVotesInflate));
    result.push_back(Pair("pegvotesdeflate", blockindex->nPegVotesDeflate));
    result.push_back(Pair("pegvotesnochange", blockindex->nPegVotesNochange));
    Array txinfo;
    for(const CTransaction& tx : block.vtx)
    {
        if (fPrintTransactionDetail)
        {
            Object entry;
            entry.push_back(Pair("txid", tx.GetHash().GetHex()));
            TxToJSON(tx, 0, mapFractions, blockindex->nPegSupplyIndex, entry);

            txinfo.push_back(entry);
        }
        else
            txinfo.push_back(tx.GetHash().GetHex());
    }

    result.push_back(Pair("tx", txinfo));

    if (block.IsProofOfStake())
        result.push_back(Pair("signature", HexStr(block.vchBlockSig.begin(), block.vchBlockSig.end())));

    return result;
}

Value getbestblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getbestblockhash\n"
            "Returns the hash of the best block in the longest block chain.");

    return hashBestChain.GetHex();
}

Value getblockcount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "Returns the number of blocks in the longest block chain.");

    return nBestHeight;
}


Value getdifficulty(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "Returns the difficulty as a multiple of the minimum difficulty.");

    Object obj;
    obj.push_back(Pair("proof-of-work",        GetDifficulty()));
    obj.push_back(Pair("proof-of-stake",       GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    return obj;
}


Value getrawmempool(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getrawmempool\n"
            "Returns all transaction ids in memory pool.");

    vector<uint256> vtxid;
    mempool.queryHashes(vtxid);

    Array a;
    for(const uint256& hash : vtxid) {
        a.push_back(hash.ToString());
    }

    return a;
}

Value getblockhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash <index>\n"
            "Returns hash of block in best-block-chain at <index>.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    CBlockIndex* pblockindex = FindBlockByHeight(nHeight);
    return pblockindex->phashBlock->GetHex();
}

Value getblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock \"blockhash\" ( verbosity ) \n"
            "\nIf verbosity is 0, returns a string that is serialized, hex-encoded data for block 'hash'.\n"
            "If verbosity is 1, returns an Object with information about block <hash>.\n"
            "If verbosity is 2, returns an Object with information about block <hash> and information about each transaction. \n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) The block hash\n"
            "2. verbosity              (numeric, optional, default=1) 0 for hex encoded data, 1 for a json object, and 2 for json object with transaction data\n"
            "\nResult (for verbosity = 0):\n"
            "\"data\"             (string) A string that is serialized, hex-encoded data for block 'hash'.\n"
            "\nResult (for verbosity = 1):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"strippedsize\" : n,    (numeric) The block size excluding witness data\n"
            "  \"weight\" : n           (numeric) The block weight as defined in BIP 141\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"tx\" : [               (array of string) The transaction ids\n"
            "     \"transactionid\"     (string) The transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes required to produce the chain up to this block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the next block\n"
            "}\n"
            "\nResult (for verbosity = 2):\n"
            "{\n"
            "  ...,                     Same output as verbosity = 1.\n"
            "  \"tx\" : [               (array of Objects) The transactions in the format of the getrawtransaction RPC. Different from verbosity = 1 \"tx\" result.\n"
            "         ,...\n"
            "  ],\n"
            "  ,...                     Same output as verbosity = 1.\n"
            "}\n"
            "\nExamples:\n"
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    int verbosity = 1;
    if (params.size() > 1) {
        verbosity = params[1].get_bool() ? 2 : 1;
    } else {
        verbosity = 1;	
	//off: Output RAW TX if second parameter is not set, useful for ElectrumX
    }

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex.ref(hash);
    
    if(!block.ReadFromDisk(pblockindex, true)){
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the
        // block).
        throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");
    }
    
    block.ReadFromDisk(pblockindex, true);

    MapFractions mapFractions;
    bool fverbosity = params.size() > 1 ? params[1].get_bool() : false;
    if (fverbosity) {
        CPegDB pegdb("r");
        for (const CTransaction & tx : block.vtx) {
            for(size_t i=0; i<tx.vout.size(); i++) {
                auto fkey = uint320(tx.GetHash(), i);
                CFractions fractions(0, CFractions::VALUE);
                if (pegdb.ReadFractions(fkey, fractions)) {
                    if (fractions.Total() == tx.vout[i].nValue) {
                        mapFractions[fkey] = fractions;
                    }
                }
            }
        }
    }
    
    if (verbosity <= 0)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

	return blockToJSON(block, pblockindex, mapFractions, fverbosity);
}

Value getblockbynumber(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockbynumber <number> [txinfo]\n"
            "txinfo optional to print more detailed tx info\n"
            "Returns details of a block with given block-number.");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");

    LOCK(cs_main);
    
    CBlock block;
    CBlockIndex* pblockindex = pindexBest;
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;

    uint256 hash = *pblockindex->phashBlock;

    pblockindex = mapBlockIndex.ref(hash);
    block.ReadFromDisk(pblockindex, true);

    MapFractions mapFractions;
    bool fverbosity = params.size() > 1 ? params[1].get_bool() : false;
    if (fverbosity) {
        CPegDB pegdb("r");
        for (const CTransaction & tx : block.vtx) {
            for(size_t i=0; i<tx.vout.size(); i++) {
                auto fkey = uint320(tx.GetHash(), i);
                CFractions fractions(0, CFractions::VALUE);
                if (pegdb.ReadFractions(fkey, fractions)) {
                    if (fractions.Total() == tx.vout[i].nValue) {
                        mapFractions[fkey] = fractions;
                    }
                }
            }
        }
    }
    
    return blockToJSON(block, pblockindex, mapFractions, fverbosity);
}

// ppcoin: get information of sync-checkpoint
Value getcheckpoint(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getcheckpoint\n"
            "Show info of synchronized checkpoint.\n");

    Object result;
    const CBlockIndex* pindexCheckpoint = Checkpoints::AutoSelectSyncCheckpoint();

    result.push_back(Pair("synccheckpoint", pindexCheckpoint->GetBlockHash().ToString().c_str()));
    result.push_back(Pair("height", pindexCheckpoint->nHeight));
    result.push_back(Pair("timestamp", DateTimeStrFormat(pindexCheckpoint->GetBlockTime()).c_str()));

    result.push_back(Pair("policy", "rolling"));

    return result;
}

void ScriptPubKeyToJSON(const CScript& scriptPubKey, Object& out, bool fIncludeHex);

Value gettxout(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "gettxout <txid> <n> [includemempool=true]\n"
            "Returns details about an unspent transaction output.");

    Object ret;

    uint256 hash;
    hash.SetHex(params[0].get_str());

    int n = params[1].get_int();
    bool fMempool = true;
    if (params.size() > 2)
        fMempool = params[2].get_bool();

    CTransaction tx;
    uint256 hashBlock = 0;
    bool found = GetTransaction(hash, tx, hashBlock);
    if (!found)
        return  Value::null;

    if (hashBlock == 0 && !fMempool) // not to include mempool
        return  Value::null;

    if (n<0 || (unsigned int)n>=tx.vout.size() || tx.vout[n].IsNull())
        return Value::null;

    const CTxOut& txout = tx.vout[n];

    // find out if there are transactions spending this output
    // to do this use CTxIndex which contains refernces to spending transactions
    CTxDB txdb("r");
    CTxIndex txindex;
    if (!txdb.ReadTxIndex(tx.GetHash(), txindex)) {
        cout << "gettxout fail, txdb.ReadTxIndex" << endl;
        return Value::null;
    }
    if (0 <= n && n < long(txindex.vSpent.size())) {
        CDiskTxPos pos = txindex.vSpent[n];
        if (!pos.IsNull()) {
            // this vout is spent in next transaction
            // this pos is pointing to spending transaction
            return Value::null;
        }
    }

    ret.push_back(Pair("value", ValueFromAmount(txout.nValue)));

    Object o;
    ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("bestblock", hashBlock.GetHex()));

    bool is_in_main_chain = false;
    if (hashBlock != 0)
    {
        map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
        {
            CBlockIndex* pindex = (*mi).second;
            if (pindex->IsInMainChain())
            {
                ret.push_back(Pair("confirmations", 1 + nBestHeight - pindex->nHeight));
                is_in_main_chain = true;
            }
        }
    }

    if (!is_in_main_chain)
        ret.push_back(Pair("confirmations", 0));

    ret.push_back(Pair("version", 1));
    ret.push_back(Pair("coinbase", tx.IsCoinBase()));

    return ret;
}

Value createbootstrap(const Array& params, bool fHelp)
{
    if (fHelp)
        throw runtime_error(
            "createbootstrap\n"
            "Create bootstrap file.");

    int nWritten = 0;

    Object ret;

    boost::filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    FILE *file = fopen(pathBootstrap.string().c_str(), "wb");
    CAutoFile fileout = CAutoFile(file, SER_DISK, CLIENT_VERSION);
    if (!fileout) {
        return JSONRPCError(RPC_MISC_ERROR, "Open bootstrap failed");
    }
    
    uint256 blockHash = Params().HashGenesisBlock();
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(blockHash);
    if (mi == mapBlockIndex.end()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Genesis block not found");
    }
    CBlockIndex* pindex = (*mi).second;
    while (pindex) {
        CBlock block;
        if (!block.ReadFromDisk(pindex, true)) {
            throw JSONRPCError(RPC_MISC_ERROR, "Block read failed");
        }
        // Write index header
        unsigned int nSize = fileout.GetSerializeSize(block);
        fileout << FLATDATA(Params().MessageStart()) << nSize;
        fileout << block;
        nWritten++;
        pindex = pindex->pnext;
    }
    
    ret.push_back(Pair("written", nWritten));
    return ret;
}

Value listunspent(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 4)
        throw runtime_error(
            "listunspent [minconf=1] [maxconf=9999999] [\"address\",...] [pegsupplyindex]\n"
            "\t(wallet api)\n"
            "\tReturns array of unspent transaction outputs\n"
            "\twith between minconf and maxconf (inclusive) confirmations.\n"
            "\tOptionally filtered to only include txouts paid to specified addresses.\n"
            "\tIf peg supply index is provided then liquid and reserve are calculated for specified peg value.\n"
            "\tResults are an array of Objects, each of which has:\n"
            "\t{txid, vout, scriptPubKey, amount, liquid, reserve, confirmations}\n\n"

            "listunspent address [minconf=1] [maxconf=9999999] [pegsupplyindex]\n"
            "\t(blockchain api)\n"
            "\tReturns array of unspent transaction outputs\n"
            "\twith between minconf and maxconf (inclusive) confirmations.\n"
            "\tIf peg supply index is provided then liquid and reserve are calculated for specified peg value.\n"
            "\tResults are an array of Objects, each of which has:\n"
            "\t{txid, vout, amount, liquid, reserve, height, txindex, confirmations}");
    
    if (params.size() > 0) {
        if (params[0].type() == str_type) {
            return listunspent1(params, fHelp);
        }
    }
    
#ifdef ENABLE_WALLET
    return listunspent2(params, fHelp);
#else
    return listunspent1(params, fHelp);
#endif
}

Value listunspent1(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "listunspent address [minconf=1] [maxconf=9999999] [pegsupplyindex]\n"
            "\t(blockchain api)\n"
            "\tReturns array of unspent transaction outputs\n"
            "\twith between minconf and maxconf (inclusive) confirmations.\n"
            "\tIf peg supply index is provided then liquid and reserve are calculated for specified peg value.\n"
            "\tResults are an array of Objects, each of which has:\n"
            "\t{txid, vout, amount, liquid, reserve, height, txindex, confirmations}");

    RPCTypeCheck(params, list_of(str_type)(int_type)(int_type)(int_type));

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid BitBay address: ")+params[0].get_str());
    
    string sAddress = params[0].get_str();
    if (sAddress.length() != 34)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid BitBay address: ")+params[0].get_str());
    
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    int nMaxDepth = 9999999;
    if (params.size() > 2)
        nMaxDepth = params[2].get_int();
    
    int nSupply = 0;
    if (pindexBest) {
        nSupply = pindexBest->nPegSupplyIndex;
    }
    if (params.size() > 3) {
        nSupply = params[3].get_int();
    }
    
    int nHeightNow = nBestHeight;
    
    CTxDB txdb("r");
    CPegDB pegdb("r");
    
    bool fIsReady = false;
    bool fEnabled = false;
    txdb.ReadUtxoDbIsReady(fIsReady);
    txdb.ReadUtxoDbEnabled(fEnabled);
    if (!fEnabled)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Balance/unspent database is not enabled"));
    if (!fIsReady)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Balance/unspent database is not ready (may require restart)"));
    
    vector<CAddressUnspent> records;
    if (!txdb.ReadAddressUnspent(sAddress, records))
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Failed ReadAddressUnspent"));
    
    Array results;
    for (const auto & record : records) {
        int nDepth = nHeightNow - record.nHeight +1;
        if (nDepth < nMinDepth || nDepth > nMaxDepth)
            continue;
        
        uint320 txoutid(record.txoutid);
        
        Object entry;
        entry.push_back(Pair("txid", txoutid.b1().GetHex()));
        entry.push_back(Pair("vout", txoutid.b2()));
        entry.push_back(Pair("address", sAddress));
        entry.push_back(Pair("amount",ValueFromAmount(record.nAmount)));

        CFractions fractions(record.nAmount, CFractions::STD);
        if (record.nHeight > nPegStartHeight) {
            if (pegdb.ReadFractions(txoutid, fractions, true /*must_have*/)) {
                int64_t nUnspentLiquid = fractions.High(nSupply);
                int64_t nUnspentReserve = fractions.Low(nSupply);
                entry.push_back(Pair("liquid",ValueFromAmount(nUnspentLiquid)));
                entry.push_back(Pair("reserve",ValueFromAmount(nUnspentReserve)));
            }
        } else {
            int64_t nUnspentLiquid = fractions.High(nSupply);
            int64_t nUnspentReserve = fractions.Low(nSupply);
            entry.push_back(Pair("liquid",ValueFromAmount(nUnspentLiquid)));
            entry.push_back(Pair("reserve",ValueFromAmount(nUnspentReserve)));
        }
        
        entry.push_back(Pair("height", record.nHeight));
        entry.push_back(Pair("txindex", record.nIndex));
        entry.push_back(Pair("confirmations", nDepth));
        results.push_back(entry);
    }
    
    return results;
}

Value listfrozen(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 4)
        throw runtime_error(
            "listfrozen [minconf=1] [maxconf=9999999] [\"address\",...] [pegsupplyindex]\n"
            "\t(wallet api)\n"
            "\tReturns array of frozen transaction outputs\n"
            "\twith between minconf and maxconf (inclusive) confirmations.\n"
            "\tOptionally filtered to only include txouts paid to specified addresses.\n"
            "\tIf peg supply index is provided then liquid and reserve are calculated for specified peg value.\n"
            "\tResults are an array of Objects, each of which has:\n"
            "\t{txid, vout, scriptPubKey, amount, liquid, reserve, confirmations}\n\n"

            "listfrozen address [minconf=1] [maxconf=9999999] [pegsupplyindex]\n"
            "\t(blockchain api)\n"
            "\tReturns array of frozen transaction outputs\n"
            "\twith between minconf and maxconf (inclusive) confirmations.\n"
            "\tIf peg supply index is provided then liquid and reserve are calculated for specified peg value.\n"
            "\tResults are an array of Objects, each of which has:\n"
            "\t{txid, vout, amount, liquid, reserve, height, txindex, confirmations}");
    
    if (params.size() > 0) {
        if (params[0].type() == str_type) {
            return listfrozen1(params, fHelp);
        }
    }
    
#ifdef ENABLE_WALLET
    return listfrozen2(params, fHelp);
#else
    return listfrozen1(params, fHelp);
#endif
}

Value listfrozen1(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
            "listfrozen address [minconf=1] [maxconf=9999999] [pegsupplyindex]\n"
            "\t(blockchain api)\n"
            "\tReturns array of frozen transaction outputs\n"
            "\twith between minconf and maxconf (inclusive) confirmations.\n"
            "\tIf peg supply index is provided then liquid and reserve are calculated for specified peg value.\n"
            "\tResults are an array of Objects, each of which has:\n"
            "\t{txid, vout, amount, liquid, reserve, height, txindex, confirmations}");

    RPCTypeCheck(params, list_of(str_type)(int_type)(int_type)(int_type));

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid BitBay address: ")+params[0].get_str());
    
    string sAddress = params[0].get_str();
    if (sAddress.length() != 34)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid BitBay address: ")+params[0].get_str());
    
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    int nMaxDepth = 9999999;
    if (params.size() > 2)
        nMaxDepth = params[2].get_int();
    
    int nSupply = 0;
    if (pindexBest) {
        nSupply = pindexBest->nPegSupplyIndex;
    }
    if (params.size() > 3) {
        nSupply = params[3].get_int();
    }
    
    int nHeightNow = nBestHeight;
    
    CTxDB txdb("r");
    CPegDB pegdb("r");
    
    bool fIsReady = false;
    bool fEnabled = false;
    txdb.ReadUtxoDbIsReady(fIsReady);
    txdb.ReadUtxoDbEnabled(fEnabled);
    if (!fEnabled)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Balance/unspent database is not enabled"));
    if (!fIsReady)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Balance/unspent database is not ready (may require restart)"));
    
    vector<CAddressUnspent> records;
    if (!txdb.ReadAddressFrozen(sAddress, records))
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Failed ReadAddressFrozen"));
    
    Array results;
    for (const auto & record : records) {
        int nDepth = nHeightNow - record.nHeight +1;
        if (nDepth < nMinDepth || nDepth > nMaxDepth)
            continue;
        
        uint320 txoutid(record.txoutid);
        
        Object entry;
        entry.push_back(Pair("txid", txoutid.b1().GetHex()));
        entry.push_back(Pair("vout", txoutid.b2()));
        entry.push_back(Pair("address", sAddress));
        entry.push_back(Pair("amount",ValueFromAmount(record.nAmount)));

        CFractions fractions(record.nAmount, CFractions::STD);
        if (record.nHeight > nPegStartHeight) {
            if (pegdb.ReadFractions(txoutid, fractions, true /*must_have*/)) {
                int64_t nUnspentLiquid = fractions.High(nSupply);
                int64_t nUnspentReserve = fractions.Low(nSupply);
                entry.push_back(Pair("liquid",ValueFromAmount(nUnspentLiquid)));
                entry.push_back(Pair("reserve",ValueFromAmount(nUnspentReserve)));
            }
        } else {
            int64_t nUnspentLiquid = fractions.High(nSupply);
            int64_t nUnspentReserve = fractions.Low(nSupply);
            entry.push_back(Pair("liquid",ValueFromAmount(nUnspentLiquid)));
            entry.push_back(Pair("reserve",ValueFromAmount(nUnspentReserve)));
        }
        
        entry.push_back(Pair("height", record.nHeight));
        entry.push_back(Pair("txindex", record.nIndex));
        entry.push_back(Pair("confirmations", nDepth));
        entry.push_back(Pair("unlocktime", record.nLockTime));
        results.push_back(entry);
    }
    
    return results;
}

Value balance(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "balance address [pegsupplyindex]\n"
            "\t(blockchain api)\n"
            "\tReturns current balance of the specified address\n"
            "\tIf peg supply index is provided then liquid and reserve are calculated for specified peg value.\n");

    RPCTypeCheck(params, list_of(str_type)(int_type));

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid BitBay address: ")+params[0].get_str());
    
    string sAddress = params[0].get_str();
    if (sAddress.length() != 34)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid BitBay address: ")+params[0].get_str());
    
    int nSupply = 0;
    if (pindexBest) {
        nSupply = pindexBest->nPegSupplyIndex;
    }
    if (params.size() > 1) {
        nSupply = params[1].get_int();
    }
    
    CTxDB txdb("r");
    
    bool fIsReady = false;
    bool fEnabled = false;
    txdb.ReadUtxoDbIsReady(fIsReady);
    txdb.ReadUtxoDbEnabled(fEnabled);
    if (!fEnabled)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Balance/unspent database is not enabled"));
    if (!fIsReady)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Balance/unspent database is not ready (may require restart)"));
    
    int64_t nLastIndex = -1;
    CAddressBalance balance;
    bool fFound = txdb.ReadAddressLastBalance(sAddress, balance, nLastIndex);
    
    Object result;
    
    result.push_back(Pair("address", sAddress));
    result.push_back(Pair("amount",ValueFromAmount(balance.nBalance)));
    result.push_back(Pair("frozen",ValueFromAmount(balance.nFrozen)));

    int64_t nUnspentLiquid = 0;
    int64_t nUnspentReserve = 0;
    if (fFound) {
        CFractions fractions(balance.nBalance - balance.nFrozen, CFractions::STD);
        if (!txdb.ReadPegBalance(sAddress, fractions))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("ReadPegBalance failed"));
        nUnspentLiquid = fractions.High(nSupply);
        nUnspentReserve = fractions.Low(nSupply);
    }
    result.push_back(Pair("liquid",ValueFromAmount(nUnspentLiquid)));
    result.push_back(Pair("reserve",ValueFromAmount(nUnspentReserve)));
    result.push_back(Pair("transactions", nLastIndex+1));
    
    return result;
}


