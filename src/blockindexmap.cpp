// Copyright (c) 2020 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockindexmap.h"

bool CBlockIndexMap::empty() const {
    return mapBlockIndex.empty();
}

size_t CBlockIndexMap::size() const {
    return mapBlockIndex.size();
}

size_t CBlockIndexMap::count(const uint256& hashBlock) const {
    return mapBlockIndex.count(hashBlock);
}

std::map<uint256, CBlockIndex*>::iterator CBlockIndexMap::find(const uint256& hashBlock) {
    return mapBlockIndex.find(hashBlock);
}

CBlockIndex* CBlockIndexMap::ref(const uint256& hashBlock) {
    return mapBlockIndex[hashBlock];
}

std::map<uint256, CBlockIndex*>::const_iterator CBlockIndexMap::begin() const {
    return mapBlockIndex.begin();
}

std::map<uint256, CBlockIndex*>::const_iterator CBlockIndexMap::end() const {
    return mapBlockIndex.end();
}

std::map<uint256, CBlockIndex*>::iterator CBlockIndexMap::begin() {
    return mapBlockIndex.begin();
}

std::map<uint256, CBlockIndex*>::iterator CBlockIndexMap::end() {
    return mapBlockIndex.end();
}

std::pair<std::map<uint256, CBlockIndex*>::iterator, bool> CBlockIndexMap::insert(const uint256& hashBlock, CBlockIndex* pindex) {
    return mapBlockIndex.insert(std::make_pair(hashBlock, pindex));
}
