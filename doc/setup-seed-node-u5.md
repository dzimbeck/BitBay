# How-to: quick setup of seed nodes

Minimal requirements: 1GB RAM, looks like it is minimal that bitbayd can run fine.

## Clouds

1. Linode: instance type: Linode 1024, 20GB Storage, 1 CPU Core, 1TB XFER, $5/mo 
2. Vultr: instance type: 1 CPU, 1024 RAM, 25GB, $5/mo

## Policy

1. Multiple accounts (not to have all seed nodes in one account)
2. Multiple regions (to spread over)

## OS type

* Ubuntu 16.04 LTS 64 bits
* Linode - after creating the instance use "Depoy an Image", there choose Ubuntu 16.04 LTS, choose root password and then boot it.
* Vultr - choose Ubuntu 16.04 when create instance. also "enable ipv6"

## Boot, setup

1. boot
2. do ssh access to the root account:
```sh
ssh root@ip_address_of_instance
```
3. when logged in, run:
```sh
source <(curl -s https://raw.githubusercontent.com/bitbaymarket/bitbay-core/master/doc/setup-seed-node-u5.sh)
```
4. When script ends, can close the console (bitbayd keep running in *screen*)
