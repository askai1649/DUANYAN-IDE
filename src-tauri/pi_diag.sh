#!/bin/bash
# Pi 侧串口诊断: 实际波特率 + 占用情况 + 115200 重测
echo "=== current ttyAMA0 settings ==="
stty -F /dev/ttyAMA0 -a | head -2
echo "=== who holds ttyAMA0 ==="
sudo -n fuser -v /dev/ttyAMA0 2>&1 | head -5
ps aux | grep -iE 'serial|uart|duanyan|screen|minicom' | grep -v grep | head -10
echo "=== set 115200 and report ==="
stty -F /dev/ttyAMA0 115200 cs8 raw
stty -F /dev/ttyAMA0 -a | head -1
echo "=== send U*1000 @115200 ==="
python3 -c "open('/dev/ttyAMA0','wb').write(b'U'*1000)"
sleep 1
echo PI_DIAG_DONE
