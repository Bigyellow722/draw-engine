#! /bin/bash

DEBUG_CONFIG=""

function configure-and-compile {
    if [[ -d "build" ]]; then
	rm -rf build
    fi

    meson setup build ${DEBUG_CONFIG}

    pushd build
    meson compile
    popd
}

while getopts "d" opt; do
    case $opt in
	d)
	    DEBUG_CONFIG="--buildtype=debug"
	    ;;
	\?)
	    ;;
    esac
done
configure-and-compile
