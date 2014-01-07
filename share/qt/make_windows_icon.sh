#!/bin/bash
# create multiresolution windows icon
ICON_SRC=../../src/qt/res/icons/dobbscoin.png
ICON_DST=../../src/qt/res/icons/dobbscoin.ico
convert ${ICON_SRC} -resize 16x16 dobbscoin-16.png
convert ${ICON_SRC} -resize 32x32 dobbscoin-32.png
convert ${ICON_SRC} -resize 48x48 dobbscoin-48.png
convert dobbscoin-16.png dobbscoin-32.png dobbscoin-48.png ${ICON_DST}

