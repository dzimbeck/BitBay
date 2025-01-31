#ifndef ETHC_ACCOUNT_H
#define ETHC_ACCOUNT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ethc-common.h"
#include <ethc/ecdsa.h>
#include <stddef.h>
#include <stdint.h>

struct eth_account {
  uint8_t address[20];
  uint8_t privkey[32];
  uint8_t pubkey[64];
};

ETHC_EXPORT int eth_account_create(struct eth_account *dest,
                                   const uint8_t *entropy);
ETHC_EXPORT int eth_account_from_privkey(struct eth_account *dest,
                                         const uint8_t *privkey);
ETHC_EXPORT int eth_account_address_get(char *dest,
                                        const struct eth_account *src);
ETHC_EXPORT int eth_account_privkey_get(char *dest,
                                        const struct eth_account *src);
ETHC_EXPORT int eth_account_pubkey_get(char *dest,
                                       const struct eth_account *src);
ETHC_EXPORT int eth_account_sign(struct eth_signed *dest,
                                 const struct eth_account *account,
                                 const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
