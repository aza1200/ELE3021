#!/bin/bash
make clean
make
make fs.img
# CPU =1  가정 (xv6 project 02 수행 )
qemu-system-i386 -nographic -serial mon:stdio -hdb fs.img xv6.img -smp 1 -m 512