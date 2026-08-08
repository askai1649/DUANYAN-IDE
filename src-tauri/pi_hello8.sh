#!/bin/bash
# hello8 Pi 侧: 校准突发 U*8 (0x55 bit0=1) -> 2s -> HELLO-PI(8字节), 抓回显验 MATCH
pinctrl 14 a0 >/dev/null 2>&1
pinctrl 15 a0 >/dev/null 2>&1
stty -F /dev/ttyAMA0 9600 cs8 raw -echo
sleep 1
# 清空残余
timeout 1 cat /dev/ttyAMA0 > /dev/null 2>&1
printf 'UUUUUUUU' > /dev/ttyAMA0
sleep 2
printf 'HELLO-PI' > /dev/ttyAMA0
timeout 15 cat /dev/ttyAMA0 > /tmp/h8.bin
echo "H8_BYTES=$(wc -c < /tmp/h8.bin)"
od -c /tmp/h8.bin | head -3
if [ "$(cat /tmp/h8.bin)" = "HELLO-PI" ]; then echo "H8_MATCH=YES"; else echo "H8_MATCH=NO"; fi
echo H8_DONE
