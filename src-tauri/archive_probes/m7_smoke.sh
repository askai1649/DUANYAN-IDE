#!/usr/bin/env bash
# m7_smoke.sh - m7 固件冒烟 (在 Pi 上跑)
# 阶段1: 纯只读 (arm read), 不发任何运动帧, 零物理风险。
# ID6 禁动的运动帧拦截验证放在阶段2 (m7_smoke_id6.sh), 用回读位置做目标, 即使拦截失效也不动。
set -u
DEV=/dev/ttyAMA0

echo "=== svc/flash 前置状态 ==="
echo "svc=$(systemctl is-active duanyan-edge)"
sudo -n systemctl stop duanyan-edge
sleep 1
echo "svc_after_stop=$(systemctl is-active duanyan-edge)"

echo "=== stty 配置 ($DEV 9600 raw) ==="
sudo -n stty -F "$DEV" 9600 raw -echo
stty -F "$DEV" -a 2>/dev/null | head -1

echo "=== REPL 冒烟: warm/link/arm read 1~6 ==="
# 不用 --auto: 避免传感查询刷屏, 输出干净。每帧由 daemon 内部 300ms 帧间隔 + ACK 重传等待节流。
printf 'warm\nlink\narm read 1\narm read 2\narm read 3\narm read 4\narm read 5\narm read 6\nlink\nquit\n' \
  | sudo -n ~/edge_daemon "$DEV" 2>&1

echo "=== 恢复服务 ==="
sudo -n systemctl start duanyan-edge
sleep 1
echo "svc_final=$(systemctl is-active duanyan-edge)"
echo "SMOKE_DONE"
