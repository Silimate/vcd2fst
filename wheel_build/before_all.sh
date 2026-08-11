#!/bin/sh
if command -v yum 2> /dev/null; then
    yum install -y zlib-devel elfutils-devel elfutils-libelf-devel libdwarf-devel
fi
if command -v apk 2> /dev/null; then
    apk add libdwarf-dev elfutils-dev zlib-dev
fi
cat test/basic.vcd.gz | gunzip > test/basic.vcd
