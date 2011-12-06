#!/bin/bash

echo args should be: filename size_in_1KB_blocks

# creates a 32MB file system file

dd if=/dev/zero bs=1k count=$2 of=$1
mke2fs -F $1
tune2fs -c0 -i0 $1
mkdir -p mnt
sudo mount -o loop $1 mnt

echo "'sudo umount mnt'" to unmount