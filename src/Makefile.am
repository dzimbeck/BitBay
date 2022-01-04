AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS) $(HARDENED_LDFLAGS)
AM_CXXFLAGS = $(HARDENED_CXXFLAGS) $(ERROR_CXXFLAGS)
AM_CPPFLAGS = $(HARDENED_CPPFLAGS)
EXTRA_LIBRARIES =

BITCOIN_CONFIG_INCLUDES=-I$(builddir)/config
BITCOIN_INCLUDES=-I$(builddir) -I$(builddir)/build $(BDB_CPPFLAGS) $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS)

BITCOIN_INCLUDES += -I$(srcdir)
BITCOIN_INCLUDES += -I$(srcdir)/peg
BITCOIN_INCLUDES += -I$(srcdir)/rpc
BITCOIN_INCLUDES += -I$(srcdir)/wallet
BITCOIN_INCLUDES += -I$(srcdir)/leveldb/helpers

DEFS += -DENABLE_WALLET
DEFS += -DENABLE_EXCHANGE

LIBBITCOIN_SERVER=libbitcoin_server.a

# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES += \
 $(LIBBITCOIN_SERVER)

bin_PROGRAMS =
TESTS =
BENCHMARKS =

if BUILD_BITCOIND
  bin_PROGRAMS += bitbayd
endif

.PHONY: FORCE check-symbols check-security check-formatting
# bitcoin core #
BITCOIN_CORE_H = \
  alert.h \
  version.h \
  checkpoints.h \
  netbase.h \
  addrman.h \
  crypter.h \
  key.h \
  init.h \
  bitcoind.h \
  keystore.h \
  core.h \
  main.h \
  net.h \
  protocol.h \
  rpc/rpcclient.h \
  rpc/rpcprotocol.h \
  rpc/rpcserver.h \
  rpc/rpcmisc.h \
  rpc/rpcnet.h \
  rpc/rpcblockchain.h \
  rpc/rpcrawtransaction.h \
  timedata.h \
  script.h \
  sync.h \
  txdb-leveldb.h \
  txmempool.h \
  util.h \
  utilstrencodings.h \
  hash.h \
  noui.h \
  kernel.h \
  crypto/pbkdf2.h \
  crypto/scrypt.h \
  chainparams.h \
  wallet/db.h \
  wallet/miner.h \
  wallet/rpcdump.h \
  wallet/rpcmining.h \
  wallet/rpcwallet.h \
  wallet/wallet.h \
  wallet/walletdb.h \
  peg/peg.h \
  peg/pegstd.h \
  peg/pegdata.h \
  peg/pegdb-leveldb.h \
  peg/pegops.h \
  peg/pegopsp.h

build/build.h: FORCE
	@$(MKDIR_P) $(builddir)/build
	@$(top_srcdir)/share/genbuild.sh $(abs_top_builddir)/src/build/build.h \
	  $(abs_top_srcdir)
libbitcoin_server_a-version.$(OBJEXT): build/build.h

# server: shared between bitcoin
libbitcoin_server_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(MINIUPNPC_CPPFLAGS) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libbitcoin_server_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIC_FLAGS) $(PIE_FLAGS)
libbitcoin_server_a_SOURCES = \
  alert.cpp \
  version.cpp \
  checkpoints.cpp \
  netbase.cpp \
  addrman.cpp \
  crypter.cpp \
  key.cpp \
  init.cpp \
  bitcoind.cpp \
  keystore.cpp \
  core.cpp \
  main.cpp \
  net.cpp \
  protocol.cpp \
  rpc/rpcclient.cpp \
  rpc/rpcprotocol.cpp \
  rpc/rpcserver.cpp \
  rpc/rpcmisc.cpp \
  rpc/rpcnet.cpp \
  rpc/rpcblockchain.cpp \
  rpc/rpcrawtransaction.cpp \
  timedata.cpp \
  script.cpp \
  sync.cpp \
  txdb-leveldb.cpp \
  txmempool.cpp \
  util.cpp \
  utilstrencodings.cpp \
  hash.cpp \
  noui.cpp \
  kernel.cpp \
  crypto/pbkdf2.cpp \
  crypto/scrypt.cpp \
  chainparams.cpp \
  wallet/db.cpp \
  wallet/miner.cpp \
  wallet/rpcdump.cpp \
  wallet/rpcmining.cpp \
  wallet/rpcwallet.cpp \
  wallet/wallet.cpp \
  wallet/walletdb.cpp \
  peg/peg.cpp \
  peg/pegstd.cpp \
  peg/peg_compat.cpp \
  peg/pegdata.cpp \
  peg/pegdata_compat.cpp \
  peg/pegdb-leveldb.cpp \
  peg/pegfractions.cpp \
  peg/peglevel.cpp \
  peg/pegops.cpp \
  peg/pegopsp.cpp \
  exchange/rpcdeposit.cpp \
  exchange/rpcexchange.cpp \
  exchange/rpcexchange_util.cpp \
  exchange/rpcfaucet.cpp \
  exchange/rpcwithdraw.cpp \
  $(BITCOIN_CORE_H)

if GLIBC_BACK_COMPAT
libbitcoin_server_a_SOURCES += compat/glibc_compat.cpp
endif

# bitcoin binary #
bitbayd_SOURCES = bitcoind.cpp
bitbayd_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS)
bitbayd_CXXFLAGS = $(AM_CXXFLAGS) $(PIC_FLAGS) $(PIE_FLAGS)
bitbayd_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

bitbayd_LDADD = \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBLEVELDB) \
  $(LIBLEVELDB_SSE42) \
  $(LIBMEMENV)

bitbayd_LDADD += $(BOOST_LIBS) $(BDB_LIBS) $(SSL_LIBS) $(CRYPTO_LIBS) $(MINIUPNPC_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS) -lz

#
CLEANFILES = $(EXTRA_LIBRARIES)

CTAES_DIST =  crypto/ctaes/bench.c
CTAES_DIST += crypto/ctaes/ctaes.c
CTAES_DIST += crypto/ctaes/ctaes.h
CTAES_DIST += crypto/ctaes/README.md
CTAES_DIST += crypto/ctaes/test.c

CLEANFILES += *.gcda *.gcno
CLEANFILES += compat/*.gcda compat/*.gcno
CLEANFILES += consensus/*.gcda consensus/*.gcno
CLEANFILES += crypto/*.gcda crypto/*.gcno
CLEANFILES += policy/*.gcda policy/*.gcno
CLEANFILES += primitives/*.gcda primitives/*.gcno
CLEANFILES += script/*.gcda script/*.gcno
CLEANFILES += support/*.gcda support/*.gcno
CLEANFILES += univalue/*.gcda univalue/*.gcno
CLEANFILES += wallet/*.gcda wallet/*.gcno
CLEANFILES += wallet/test/*.gcda wallet/test/*.gcno

DISTCLEANFILES = build/build.h

EXTRA_DIST = leveldb $(CTAES_DIST)

clean-local:
	-rm -f leveldb/*/*.gcda leveldb/*/*.gcno leveldb/helpers/memenv/*.gcda leveldb/helpers/memenv/*.gcno
	-rm -f config.h

.rc.o:
	@test -f $(WINDRES)
	$(AM_V_GEN) $(WINDRES) -DWINDRES_PREPROC -i $< -o $@

.mm.o:
	$(AM_V_CXX) $(OBJCXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(QT_INCLUDES) $(AM_CXXFLAGS) $(PIC_FLAGS) $(PIE_FLAGS) $(CXXFLAGS) -c -o $@ $<

check-symbols: $(bin_PROGRAMS)
if GLIBC_BACK_COMPAT
	@echo "Checking glibc back compat..."
	$(AM_V_at) READELF=$(READELF) CPPFILT=$(CPPFILT) $(top_srcdir)/contrib/devtools/symbol-check.py < $(bin_PROGRAMS)
endif

check-security: $(bin_PROGRAMS)
if HARDEN
	@echo "Checking binary security..."
	$(AM_V_at) READELF=$(READELF) OBJDUMP=$(OBJDUMP) $(top_srcdir)/contrib/devtools/security-check.py < $(bin_PROGRAMS)
endif


check-formatting:
	@echo "Checking source formatting style..."
	(cd $(top_srcdir)/src; ../contrib/devtools/clang-format.py check clang-format-3.8 `cat ./.formatted-files`)

%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(abspath $(<D) $<)

if EMBEDDED_LEVELDB
include Makefile.leveldb.include
endif

if ENABLE_TESTS
include Makefile.test.include
endif
