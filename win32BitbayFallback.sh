#!/bin/bash

set -o errexit  # Exit on error
set -o nounset  # Trigger error when expanding unset variables

function realpath
{
    [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
}

SCRIPT_PATH="$(realpath ""$0"")"
PATCH_PATH="${SCRIPT_PATH/sh/patch}"
BASE_DIR="$HOME/bitbay"

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
    sudo apt-get update || force "apt-get update failed"
    sudo apt-get install -y autoconf automake autopoint bash bison bzip2 flex gettext git g++ gperf intltool libffi-dev libgdk-pixbuf2.0-dev libtool libltdl-dev libssl-dev libxml-parser-perl make openssl p7zip-full patch perl pkg-config python ruby scons sed unzip wget xz-utils g++-multilib libc6-dev-i386 libtool-bin || force "apt-get install failed"

    cd "$HOME"
    [[ -d mxe ]] || git clone https://github.com/mxe/mxe.git || force "clone mxe failed"
    cd mxe
    git checkout d9c3273ed526549c80b58ace8843dd3037698139
    make gcc boost miniupnpc openssl db zlib libpng

    rm -rf "$BASE_DIR"
    mkdir -p "$BASE_DIR"
    cd "$BASE_DIR"

    git clone https://github.com/dzimbeck/bitbay
    cd bitbay
    
    git am "$PATCH_PATH"
    
    #cd src/leveldb
    #chmod 755 build_detect_platform
    #make
    #make libmemenv.a
    cp LevelDB/libleveldb.a src/leveldb/libleveldb.a
    cp LevelDB/libmemenv.a src/leveldb/libmemenv.a
}

function build
{
    cd "$HOME/bitbay/bitbay/src"
    
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
