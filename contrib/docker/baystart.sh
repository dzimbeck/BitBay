#!/bin/sh
export

mkdir -p /data
touch /data/bitbay.conf
echo "rpcport=80" > /data/bitbay.conf
echo "rpcuser=${RPC_USER}" >> /data/bitbay.conf
echo "rpcpassword=${RPC_PASS}" >> /data/bitbay.conf
echo "rpcallowip=*" >> /data/bitbay.conf
cat /data/bitbay.conf

/usr/bin/bitbayd -datadir=/data -logtimestamps -printtoconsole $*

