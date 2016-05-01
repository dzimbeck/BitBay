#!/bin/bash

set -o errexit  # Exit on error
set -o nounset  # Trigger error when expanding unset variables

function realpath
{
    [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
}

SCRIPT_PATH="$(realpath ""$0"")"
PATCH_PATH="${SCRIPT_PATH/sh/patch}"
BASE_DIR="$HOME/src2"

function force
{
    echo "Check failed: $1"
    if [[ -z "$DO_FORCE" ]]
    then
        echo "Use --force to ignore checks"
        exit 1
    fi
}

function check
{
    [[ $(cat /etc/debian_version) == 8.* && -d "/usr/lib/x86_64-linux-gnu" ]] || force "Debian 8 (jessie) x86_64 required"

    [[ -x "$(which git)" ]] || force "git not found. Please check that it is istalled and in PATH"

    [[ -f "$PATCH_PATH" ]] || force "Patch file not found"
}

function reset
{
    echo "deb http://pkg.mxe.cc/repos/apt/debian wheezy main" | sudo tee /etc/apt/sources.list.d/mxeapt.list || force "add repository url failed"
    
    sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys D43A795B73B16ABE9643FE1AFD8FFF16DB45C6AB || force "add repository key failed"
    
    sudo apt-get update || force "apt-get update failed"

    sudo apt-get install -y mxe-i686-w64-mingw32.static-boost mxe-i686-w64-mingw32.static-miniupnpc mxe-i686-w64-mingw32.static-openssl mxe-i686-w64-mingw32.static-db mxe-i686-w64-mingw32.static-zlib mxe-i686-w64-mingw32.static-libpng || force "apt-get install failed"

    rm -rf "$BASE_DIR"
    mkdir -p "$BASE_DIR"
    cd "$BASE_DIR"

    git clone https://github.com/dzimbeck/bitbay

    cd bitbay

    git config --global user.name "Your Name"
    git config --global user.email "you@example.com"    
    git am "$PATCH_PATH"

    cd src/leveldb
    chmod 755 build_detect_platform
    make
    make libmemenv.a
}

function build
{
    cd "$HOME/src2/bitbay/src"
    
    make -f makefile.linux-mingw
}

function help
{
    echo "Use:"
    echo "$0 [ --help ] [ --force ] [ --reset ] [ --build ]"
}

DO_HELP=
DO_FORCE=
DO_RESET=
DO_BUILD=

for arg in "$@"
do
    if [[ "$arg" == "--help" ]]
    then
        DO_HELP=1
    elif [[ "$arg" == "--force" ]]
    then
        DO_FORCE=1
    elif [[ "$arg" == "--reset" ]]
    then
        DO_RESET=1
    elif [[ "$arg" == "--build" ]]
    then
        DO_BUILD=1
    else
        echo "Unknown argument: $arg"
        help
        exit 1
    fi
done

if [[ -z "$DO_RESET" && -z "$DO_BUILD" || -n "$DO_HELP" ]]
then
    help
    exit 0
fi

check

if [[ -n "$DO_RESET" ]]
then
    reset
fi

if [[ -n "$DO_BUILD" ]]
then
    build
fi

if [[ -z "$DO_FORCE" ]]
then
    echo "Success!"
fi
