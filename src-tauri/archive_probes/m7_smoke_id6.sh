#!/usr/bin/env bash
# m7_smoke_id6.sh - ID6 禁动运动帧拦截验证 (在 Pi 上跑)
# 发一帧 ID6 运动指令 #006P0500T0500!, 目标脉宽=阶段1回读的 500 (安全垫: 拦截万一失效位移也趋近零)。
# 预期: m7 固件 zx_ascii_forbidden 命中 → status=1 BAD_PARAM, 帧不上总线, 舵机收不到。
set -u
DEV=/dev/ttyAMA0

echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
sudo -n stty -F "$DEV" 9600 raw -echo

echo "=== ID6 运动帧拦截验证: arm set 6 500 500 ==="
printf 'warm\narm set 6 500 500\narm read 6\nlink\nquit\n' \
  | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "ID6_SMOKE_DONE"
