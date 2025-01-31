// Copyright (c) 2024 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef EVMTX_H
#define EVMTX_H

#include <string>
class CKey;

std::string MakeTxEvm(int          chainid,
					  int          nonce,
					  float        max_priority_fee_per_gas_gwei,
					  float        max_fee_per_gas_gwei,
					  int          gaslimit,
					  std::string  addr_to,
					  std::string  input_data_hex,
					  const CKey&  vchSecret,
					  std::string& txhash_hex);

#endif  // EVMTX_H
