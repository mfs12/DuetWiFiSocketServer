#!/usr/bin/bash


BOOTLOADER=bootloader/bootloader.bin
PARTITION=partition_table/partition-table.bin
APP=dwss.bin
IMAGE=dwss-image.bin

BOOTLOADER_OFFSET=$((16#0))
PARTITION_OFFSET=$((16#8000))
APP_OFFSET=$((16#10000))

OFLAGS=append

if test -n "$1"; then
  BOOTLOADER=$1
fi
if test -n "$2"; then
  PARTITION=$2
fi

if test -n "$3"; then
  APP=$3
fi

if test -n "$4"; then
  IMAGE=$4
fi

dd if=$BOOTLOADER of=$IMAGE
dd if=$PARTITION of=$IMAGE oflag=$OFLAGS obs=$PARTITION_OFFSET seek=1
dd if=$APP of=$IMAGE oflag=$OFLAGS obs=$APP_OFFSET seek=1
