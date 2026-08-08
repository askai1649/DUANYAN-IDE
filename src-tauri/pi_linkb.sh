#!/bin/bash
# linkb Pi 侧: 每轮 U*8 校准 -> 2s -> 发协议帧, 抓 ESP 应答帧
# CRC8 poly=0x07 init=0, 覆盖 LEN,CMD,PAYLOAD
pinctrl 14 a0 >/dev/null 2>&1
pinctrl 15 a0 >/dev/null 2>&1
stty -F /dev/ttyAMA0 9600 cs8 raw -echo
sleep 1
timeout 1 cat /dev/ttyAMA0 > /dev/null 2>&1

round() {
  local name="$1" frame="$2"
  printf 'UUUUUUUU' > /dev/ttyAMA0
  sleep 2
  timeout 4 cat /dev/ttyAMA0 > /tmp/lb_${name}.bin &
  local cp=$!
  sleep 0.3
  printf "$frame" > /dev/ttyAMA0
  wait $cp
  echo "LB_${name}_BYTES=$(wc -c < /tmp/lb_${name}.bin)"
  echo "LB_${name}_HEX=$(od -An -tx1 /tmp/lb_${name}.bin | tr -d ' \n')"
}

# 轮1: CMD=0x12 查询 (LEN=0)      期望应答 AA 55 01 06 07 (ACK)
round qry  '\xAA\x55\x00\x12\x7E'
# 轮2: CMD=0x11 GPIO控制 pin=10 lvl=1  期望应答 ACK
round gpio '\xAA\x55\x02\x11\x0A\x01\x60'
# 轮3: CMD=0xEE 未知命令          期望应答 AA 55 01 15 7E (NACK)
round unk  '\xAA\x55\x00\xEE\x84'

ok=0
[ "$(od -An -tx1 /tmp/lb_qry.bin  | tr -d ' \n')" = "aa55010607" ] && ok=$((ok+1))
[ "$(od -An -tx1 /tmp/lb_gpio.bin | tr -d ' \n')" = "aa55010607" ] && ok=$((ok+1))
[ "$(od -An -tx1 /tmp/lb_unk.bin  | tr -d ' \n')" = "aa5501157e" ] && ok=$((ok+1))
echo "LB_PASS=$ok/3"
echo LB_ALL_DONE
