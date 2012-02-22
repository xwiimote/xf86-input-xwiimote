#! /bin/sh

mkdir -p m4/
autoreconf -i || exit 1
./configure --prefix=/usr $*
