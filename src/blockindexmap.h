// Copyright (c) 2020 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITBAY_BLOCKINDEXMAP_H
#define BITBAY_BLOCKINDEXMAP_H

#include "bignum.h"
#include "uint256.h"
#include "util.h"

#include <map>
#include <vector>

class CBlockIndex;

class CBlockIndexMap {
public:
    std::map<uint256, CBlockIndex*> mapBlockIndex;
    
    bool empty() const;
    size_t size() const;
    size_t count(const uint256& hashBlock) const;
    std::map<uint256, CBlockIndex*>::const_iterator begin() const;
    std::map<uint256, CBlockIndex*>::const_iterator end() const;
    std::map<uint256, CBlockIndex*>::iterator begin();
    std::map<uint256, CBlockIndex*>::iterator end();
    std::map<uint256, CBlockIndex*>::iterator find(const uint256& hashBlock);
    CBlockIndex* ref(const uint256& hashBlock);
    std::pair<std::map<uint256, CBlockIndex*>::iterator, bool> insert(const uint256& hashBlock, CBlockIndex* pindex);
};

#endif
