import subprocess, time

for baud in ("115200", "57600", "230400", "38400", "460800", "9600"):
    subprocess.run(["sudo", "stty", "-F", "/dev/ttyAMA0", baud, "raw", "-echo"],
                   check=False, stderr=subprocess.DEVNULL)
    time.sleep(0.2)
    # 抓 4 秒
    try:
        data = subprocess.run(
            ["timeout", "4", "dd", "if=/dev/ttyAMA0", "bs=512"],
            capture_output=True, timeout=8).stdout
    except Exception as e:
        data = b""
    printable = sum(1 for b in data if 32 <= b < 127 or b in (10, 13))
    print("baud=%s bytes=%d printable=%d%% sample=%r" % (
        baud, len(data),
        int(100 * printable / max(len(data), 1)),
        data[:48]))
