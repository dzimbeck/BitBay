[![Codacy Badge](https://api.codacy.com/project/badge/Grade/1bac5bbdf2f64cfeb67092bef3e50d6f)](https://www.codacy.com/app/yshurik/bitbay-core?utm_source=github.com&utm_medium=referral&utm_content=bitbaymarket/bitbay-core&utm_campaign=badger)
[![Build Status](https://travis-ci.org/bitbaymarket/bitbay-core.svg?branch=master)](https://travis-ci.org/bitbaymarket/bitbay-core)
[![Build status](https://ci.appveyor.com/api/projects/status/qdy7pilwdtxehqhw?svg=true)](https://ci.appveyor.com/project/yshurik/bitbay-core)
[![Open Source Love](https://badges.frapsoft.com/os/mit/mit.svg?v=102)](https://github.com/bitbaymarket/bitbay-core/blob/master/COPYING)


BitBay development tree

BitBay is a PoS-based cryptocurrency.

BitBay
===========================

BitBay is the world's first fully-functional decentralized marketplace since 2014 with working contracts even before Ethereum. Using innovative technology, BitBay enables you to buy and sell goods and services securely and without the need for third parties. The marketplace is built free and open source. The markets are based on Bitmessage and use double deposit escrow. This is a two party escrow that eliminates the middle man and removes the incentive for default due to both parties having a deposit in the joint account. They either win together or they lose together, without being able to profit off of fraud. This makes theft and fraud impossible in these contracts for the first time in history without the need of an escrow agent. 

BitBay also is the only coin in the world with a truly variable supply. Since other proposals of variable supply see no shift in equity they have zero impact on controlling the economy. BitBay is different because users hold equity over what is moved into reserve(sort of like a negative interest rate moving a users funds into a savings account). Therefore during inflation only users who hold reserve will see their coins released into circulation. If the price drops, deflation can protect the economy by moving some of the funds of users back into reserve. BitBay has been able to prove that this concept works and even under extreme duress and volatility with little buy support it was able to hold it's price and subsequently, any price the community wanted it to hold.

The target price of this dynamic supply(also called dynamic peg) is determined by the users of the coin. So unlike a stablecoin, collateral is not required and BitBay can change(and even possibly increase) in price or it can hold a desired price range. This is purely determined by the stakers/users who also protect the network. So the decision to increase or decrease supply is purely decentralized and based on user consensus. Typically users choose an algorithm to vote on supply for them to target peaks or fulfill other economic goals. Therefore, BitBay is not a stablecoin whatsoever. BitBay is actually the first cryptocurrency that emulates how a modern economy works.

This variable supply is accomplished because all user balances are arrays and liquid and reserve balances are determined based on the supply index of the entire economy. When moving liquid funds, users pull coins from each array column based on it's ratio. When moving reserve funds the coins that are less than the supply index are moved and subject to a one month time delay similar to a bond or long term savings. Therefore a user has two balances, liquid and reserve. BitBay also supplies code for central exchanges that wish to implement this peg so that they can handle the arrays on their order books. There was also a mock exchange set up for over a year to demonstrate the process. Although ultimately, BitBay is best suited for decentralized exchanges as it can be considered safer and better suited for the technical challenge of exchange implementation.

Development process
===========================

Developers work in their own trees, then submit pull requests when
they think their feature or bug fix is ready.

The patch will be accepted if there is broad consensus that it is a
good thing.  Developers should expect to rework and resubmit patches
if they don't match the project's coding conventions (see coding.txt)
or are controversial.

The master branch is regularly built and tested, but is not guaranteed
to be completely stable. Tags are regularly created to indicate new
stable release versions of BitBay.

Feature branches are created when there are major new features being
worked on by several people.

From time to time a pull request will become outdated. If this occurs, and
the pull is no longer automatically mergeable; a comment on the pull will
be used to issue a warning of closure. The pull will be closed 15 days
after the warning if action is not taken by the author. Pull requests closed
in this manner will have their corresponding issue labeled 'stagnant'.

Issues with no commits will be given a similar warning, and closed after
15 days from their last activity. Issues closed in this manner will be
labeled 'stale'.

Useful information
===========================

To better understand how BitBay works, you can see a visual explainer here:
https://github.com/bitbaymarket/website/blob/cc9cddf386bbdf8da9e97af4917f406bc16794b2/bitbay-dynamic-peg-visual-reference.pdf

Full documentation for integration of BitBay to exchanges can be found here: https://github.com/bitbaymarket/bitbay-doc-exchange

In the contrib folder there is source code for running dockers for full nodes with transaction index, exchange/server nodes, etc.

https://github.com/bitbaymarket/bitbay-core/releases

At the release link you can download the dockers including those for full explorers. The QT also has a setting for the full index.

The BitBay Halo wallets(which has the double deposit markets) is hosted at a different github at:

https://github.com/bithalo/bitbay-halo

Lastly there is also web based markets for Ethereum/Polygon under the name "Ethalo" which can be found through BitBays main website.

For any other project information please visit: https://bitbay.market


BitBay BUILD Readme
======

The world's first decentralised currency designed for mass adoption
With its unique system of adaptive supply control, BitBay is creating a reliable currency that is truly independent.
The revolutionary 'Dynamic Peg' creates both a store of value and a medium of exchange.

License
-------

BitBay is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Compilation
-----------

**Local**

Compiling *bitbayd* on unix host:

```
autoreconf --install --force
./configure
make
```

**Using docker images of builders**

1. Building Linux64 static binary of BitBay Qt Wallet:
Ref builder image repo: [builder-linux64](https://https://github.com/bitbaymarket/builder-linux64)
```sh
#!/bin/sh
set -x
set -e
rm -rf .qmake.stash
rm -rf build bitbay-wallet-qt
cd src
git clean -f -d -x .
cd ..
docker pull bitbayofficial/builder-linux64:alpine
rm -rf bitbay-qt-local.pri
echo "CONFIG += silent" >> bitbay-qt-local.pri
echo "LIBS += -static" > bitbay-qt-local.pri
echo "DEFINES += CURL_STATICLIB" >> bitbay-qt-local.pri
echo "DEFINES += SECP256K1_STATIC" >> bitbay-qt-local.pri
echo "LIBS_CURL_DEPS += -lssh2 -lnghttp2" >> bitbay-qt-local.pri
mkdir -p build
/bin/sh share/genbuild.sh build/build.h
docker run --rm \
	-v $(pwd):/mnt \
	-u $(stat -c %u:%g .) \
	bitbayofficial/builder-linux64:alpine \
	/bin/sh -c "qmake-qt5 -v && \
		cd /mnt && \
		ls -al && \
		qmake-qt5 \
		CICD=travis_x64 \
		\"USE_TESTNET=0\" \
		\"USE_DBUS=0\" \
		\"USE_QRCODE=0\" \
		\"BOOST_LIB_SUFFIX=-mt\" \
		bitbay-qt.pro && \
		sed -i 's/\/usr\/lib\/libssl.so/-lssl/' Makefile &&
		sed -i 's/\/usr\/lib\/libcrypto.so/-lcrypto/' Makefile &&
		sed -i s:sys/fcntl.h:fcntl.h: src/compat.h &&
		make -j32";
tar -zcf mainnet-qt-wallet-lin64.tgz  bitbay-wallet-qt
```

2. Building Linux64 static binary of bitbayd (without a wallet):
Ref builder image repo: [builder-linux64](https://https://github.com/bitbaymarket/builder-linux64)
```sh
#!/bin/sh
set -e
set -x
rm -rf .qmake.stash
rm -rf build bitbayd
cd src
git clean -f -d -x .
cd ..
docker pull bitbayofficial/builder-linux64:alpine
rm -rf bitbayd-local.pri
echo "CONFIG += silent" >> bitbayd-local.pri
echo "LIBS += -static" > bitbayd-local.pri
echo "DEFINES += CURL_STATICLIB" >> bitbayd-local.pri
echo "DEFINES += SECP256K1_STATIC" >> bitbayd-local.pri
echo "LIBS_CURL_DEPS += -lssh2 -lnghttp2" >> bitbayd-local.pri
mkdir -p build
/bin/sh share/genbuild.sh build/build.h
docker run --rm \
	-v $(pwd):/mnt \
	-u $(stat -c %u:%g .) \
	bitbayofficial/builder-linux64:alpine \
	/bin/sh -c "qmake-qt5 -v && \
		cd /mnt && \
		ls -al && \
		qmake-qt5 \
		CICD=travis_x64 \
		\"USE_DBUS=0\" \
		\"USE_QRCODE=0\" \
		\"USE_WALLET=0\" \
		\"USE_TESTNET=0\" \
		\"BOOST_LIB_SUFFIX=-mt\" \
		bitbayd.pro && \
		sed -i 's/\/usr\/lib\/libssl.so/-lssl/' Makefile &&
		sed -i 's/\/usr\/lib\/libcrypto.so/-lcrypto/' Makefile &&
		sed -i s:sys/fcntl.h:fcntl.h: src/compat.h &&
		make -j32"
```

3. Building Windows64 static binary of BitBay Qt Wallet:
Ref builder image repo: [builder-windows64](https://https://github.com/bitbaymarket/builder-windows64)
```sh
#!/bin/sh
set -x
set -e
rm -rf .qmake.stash
rm -rf build bitbay-wallet-qt.exe
cd src
git clean -f -d -x .
cd ..
docker pull bitbayofficial/builder-windows64:qt
rm -rf bitbay-qt-local.pri
echo "CONFIG += silent" >> bitbay-qt-local.pri
echo "DEFINES += CURL_STATICLIB" >> bitbay-qt-local.pri
echo "DEFINES += SECP256K1_STATIC" >> bitbay-qt-local.pri
echo "LIBS_CURL_DEPS += -lidn2 -lunistring -liconv -lcharset -lssh2 -lssh2 -lz -lgcrypt -lgpg-error -lintl -liconv -lws2_32 -lnettle -lgnutls -lhogweed -lnettle -lidn2 -lz -lws2_32 -lcrypt32 -lgmp -lunistring -liconv -lcharset -lwldap32 -lz -lws2_32 -lpthread" >> bitbay-qt-local.pri
mkdir -p build
/bin/sh share/genbuild.sh build/build.h
docker run --rm \
	-v $(pwd):/mnt \
	-u $(stat -c %u:%g .) \
	bitbayofficial/builder-windows64:qt \
	/bin/bash -c "cd /mnt && qmake -v && \
		qmake \
		CICD=travis_x64 \
		QMAKE_LRELEASE=lrelease \
		\"USE_TESTNET=0\" \
		\"USE_QRCODE=1\" \
		bitbay-qt.pro && \
		mv Makefile.Release Makefile.tmp && ( \
		cat Makefile.tmp | \
		sed -e 's/bin.lrelease\.exe/bin\/lrelease/m' | \
		sed -e 's/boost_thread-mt/boost_thread_win32-mt/m' > Makefile.Release \
		) && \
		make -j32";
mv release/bitbay-wallet-qt.exe .           &&
file bitbay-wallet-qt.exe                   &&
zip mainnet-qt-wallet-win64.zip bitbay-wallet-qt.exe;
```

