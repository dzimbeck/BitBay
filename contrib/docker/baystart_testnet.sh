#!/bin/sh
export

mkdir -p /data
touch /data/bitbay.conf
echo "rpcport=80" > /data/bitbay.conf
echo "rpcuser=${RPC_USER}" >> /data/bitbay.conf
echo "rpcpassword=${RPC_PASS}" >> /data/bitbay.conf
echo "rpcallowip=*" >> /data/bitbay.conf
cat /data/bitbay.conf

bitbayd -testnet -staking=0 -datadir=/data -logtimestamps -printtoconsole $*

