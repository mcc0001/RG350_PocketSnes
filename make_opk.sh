#!/bin/sh

make -f Makefile.rg350 $1

if [ $1 = "clean" ];then
    return 0;
fi

cd $(dirname $0)

OPK_NAME=PocketSNES.opk

echo Building ${OPK_NAME}...

# create opk
FLIST="dist/PocketSNES.dge opk/default.gcw0.desktop dist/backdrop.png opk/sfc.png"

rm -f ${OPK_NAME}
mksquashfs ${FLIST} ${OPK_NAME} -all-root -no-xattrs -noappend -no-exports

cat opk/default.gcw0.desktop

echo cp ${OPK_NAME} to /media/psf/opk
cp ${OPK_NAME} /media/psf/opk
