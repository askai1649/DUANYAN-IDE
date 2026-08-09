/* uart_bbangrx.c v3 - UART0 TX FIFO 节拍定时的软件位啜 RX (定时与 CPU 速度彻底解耦)
 * 背景: g5mon 实锤 Pi(ttyAMA0 GPIO14)→ESP GPIO5 线通; FPGA 矩阵→UART RXD 死,
 *       RX 走 GPIO5 软件位啜, TX 走硬件 U1(GPIO4)@9600。
 * v2 教训: CPU 忙等轮速随缓存态漂移 25x, T0 分辨率 8us 太粗 → 解码不稳。
 * v3 方案: UART0 控制台 115200 实测精准 → 证明 UART 时钟准。将 UART0 TX
 *       改高速(~2Mbps), 用 TX FIFO 排空字节数当节拍 (1字节=1 tick≈5us),
 *       位宽用校准突发的波形捕获以 tick 度量, 解码按 tick 绝对锚点采样。
 * 协议: Pi 先发校准突发 XXXXXXXX, 睡 2s, 再发负载 HELLO-PI。
 *       ESP: 捕获校准→排空残余→解码 8 字节(静默)→经 U1(9600) 打印+回传。
 * 控制台: 启动早期走 UART0@115200 (主机 COM4); 重配后日志全走 U1 (Pi 可见)。
 */
#define UART0_FIFO   (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS (*(volatile unsigned int *)0x6000001C)

#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_WDTCONFIG0  (*(volatile unsigned int *)0x6001F048)
#define TIMG0_INT_CLR     (*(volatile unsigned int *)0x6001F07C)

#define R(a) (*(volatile unsigned int *)(a))
#define GPIO_BASE  0x60004000u
#define GPIO_IN            R(GPIO_BASE + 0x3C)
#define GPIO_ENABLE_W1TC   R(GPIO_BASE + 0x28)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_FUNC_OUT_SEL(pin) R(GPIO_BASE + 0x554 + (pin) * 4)
#define IOMUX_BASE 0x60009000u
#define IOMUX_PIN(p) R(IOMUX_BASE + 4 + (p) * 4)
#define PERIP_CLK_EN0      R(0x600C0018u)

#define U0(a) (*(volatile unsigned int *)(0x60000000u + (a)))
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))

/* ---- 早期控制台: UART0 @115200 (重配前) ---- */
static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

/* ---- U1 控制台/回传 @9600 (重配后所有日志走这里, Pi 可见) ---- */
static void u1_putc(char c)
{
    for (volatile int w = 0; w < 200000; w++) {
        if (((U1(0x1C) >> 16) & 0x3FF) < 127) break;
    }
    U1(0x00) = (unsigned int)c;
}
static void u1_puts(const char *s) { while (*s) u1_putc(*s++); }
static void u1_hex(unsigned int v)
{
    const char *h = "0123456789ABCDEF";
    u1_puts("0x");
    for (int i = 7; i >= 0; i--) u1_putc(h[(v >> (i * 4)) & 0xF]);
}
static void u1_dec(unsigned int v)
{
    char buf[12];
    int n = 0;
    if (v == 0) { u1_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n--) u1_putc(buf[n]);
}

static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}
static unsigned int g5(void) { return (GPIO_IN >> 5) & 1u; }

/* ---- UART0 TX FIFO 节拍引擎 ----
 * 每个排队字节按 UART0 波特率串行排出 = 1 tick (硬件节拍, 与 CPU 无关)。
 * stuffed 累计塞入字节数, 当前存量 = TXFIFO_CNT, 已过节拍 = stuffed-存量。
 * 保持存量 ≥96 即可连续计时 (topup 间隔必须 < 96 tick)。 */
static unsigned int stuffed = 0;
static unsigned int u0_txleft(void) { return (U0(0x1C) >> 16) & 0x3FFu; }
static void tick_topup(void)
{
    unsigned int left = u0_txleft();
    while (left < 96u) { U0(0x00) = 0xFFu; stuffed++; left++; }
}
static unsigned int now_ticks(void) { return stuffed - u0_txleft(); }

/* 等下降沿; timeout_ticks 超时返回 0; hb 非空时每 2s (40万 tick) 心跳(走U1) */
static unsigned int wait_edge_tick(unsigned int timeout_ticks, unsigned int *hb)
{
    tick_topup();
    unsigned int start = now_ticks();
    unsigned int last_hb = start;
    unsigned int slow = 0;
    for (;;) {
        if (g5() == 0) return now_ticks();
        if (++slow & 15u) continue;
        tick_topup();
        unsigned int now = now_ticks();
        if ((unsigned int)(now - start) >= timeout_ticks) return 0;
        if (hb && (unsigned int)(now - last_hb) >= 400000u) {
            last_hb = now;
            (*hb)++;
            u1_puts("[BBRX] hb"); u1_dec(*hb);
            u1_puts(" t="); u1_dec(now);
            u1_puts(" g5="); u1_putc('0' + g5()); u1_puts("\r\n");
        }
    }
}

/* 波形捕获 (tick 度量): 记录最多 24 次翻转时刻, 最小间隔 = 位宽(tick) */
static unsigned int cap_ticks(void)
{
    unsigned int ts[24];
    int n = 0;
    unsigned int prev = g5();
    tick_topup();
    ts[n++] = now_ticks();
    unsigned int start = ts[0];
    while ((unsigned int)(now_ticks() - start) < 500u && n < 24) {
        tick_topup();
        unsigned int v = g5();
        if (v != prev) { ts[n++] = now_ticks(); prev = v; }
    }
    u1_puts("[BBRX] cap n="); u1_dec(n);
    u1_puts(" gaps:");
    unsigned int mingap = 0xFFFFFFFFu;
    for (int k = 1; k < n; k++) {
        unsigned int d = ts[k] - ts[k - 1];
        u1_putc(' '); u1_dec(d);
        if (d > 0 && d < mingap) mingap = d;
    }
    u1_puts("\r\n");
    return mingap;
}

/* 位啜收一字节 (8N1): tick 绝对锚点, 每 bit 中心单次采样
 * (采样间隙只 topup+比较, 节拍由硬件排出, 无 CPU 速度依赖) */
static unsigned int bbang_rx_tick(unsigned int bit_ticks, unsigned int comp)
{
    unsigned int anchor = now_ticks();
    unsigned int byte = 0;
    for (int b = 0; b < 8; b++) {
        unsigned int off = ((2u * b + 3u) * bit_ticks) / 2u;
        if (off > comp) off -= comp; else off = 0;
        unsigned int target = anchor + off;
        do { tick_topup(); } while ((unsigned int)(now_ticks() - anchor) < off);
        (void)target;
        if (g5()) byte |= (1u << b);
    }
    return byte;
}

void _start(void) __attribute__((section(".entry"), naked));
void _start(void)
{
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF |= (1u << 31);
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0xFFFFFFFFu;

    delay_ms(500);
    uart_puts("\r\n[BBRX] bit-bang RX v3 (uart-tick timing)\r\n");

    /* GPIO5: 输入 */
    GPIO_ENABLE_W1TC = (1u << 5);
    IOMUX_PIN(5) |= (1u << 9);

    /* U1 TX (GPIO4) @9600 — 日志+回传通道 (UART 外设时钟实测精准) */
    PERIP_CLK_EN0 |= (1u << 5);
    U1(0x14) = (40000000u / 9600u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(2);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    IOMUX_PIN(4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_OUT_SEL(4) = 15;
    GPIO_ENABLE_W1TS = (1u << 4);
    u1_puts("[BBRX] u1 console live @9600\r\n");

    /* UART0 重配为节拍源: 按现有 CLKDIV 等比缩放到 ~2Mbps (1 tick≈5us)。
     * 缩放不依赖时钟源假设; 位宽由波形捕获以 tick 实测, 波特值无需精确。 */
    {
        unsigned int d0 = U0(0x14) & 0xFFFFFu;
        unsigned int d1 = (d0 * 1152u) / 20000u;     /* 115200 → 2000000 */
        if (d1 < 4) d1 = 4;
        U0(0x14) = d1;
        uart_puts("[BBRX] u0 retimed, div "); /* 趁 FIFO 尚按旧速排完前发 */
        delay_ms(20);
    }
    tick_topup();
    u1_puts("[BBRX] tick engine on\r\n");

    /* === 两阶段协议 === */
    u1_puts("[BBRX] phase1: await calib burst\r\n");
    unsigned int hb = 0;
    unsigned int bit_ticks = 0;
    if (!wait_edge_tick(12000000u, &hb)) { u1_puts("[BBRX] calib timeout\r\n"); goto done; }
    {
        unsigned int gap = cap_ticks();
        if (gap == 0 || gap > 100) gap = 21;
        bit_ticks = gap;
        u1_puts("[BBRX] calib bit_ticks="); u1_dec(bit_ticks); u1_puts("\r\n");
    }
    /* 排空校准包残余: 短窗(40位宽)内还有沿就丢 */
    while (wait_edge_tick(bit_ticks * 40u, 0)) {}
    u1_puts("[BBRX] phase2: payload decode x8 (silent)\r\n");
    {
        unsigned char buf[8];
        int got = 0;
        for (int i = 0; i < 8; i++) {
            unsigned int e = wait_edge_tick(12000000u, &hb);
            if (!e) break;
            buf[i] = (unsigned char)bbang_rx_tick(bit_ticks, 2u);
            got++;
        }
        for (int i = 0; i < got; i++) {
            u1_puts("[BBRX] rx="); u1_hex(buf[i]);
            u1_puts(" '"); u1_putc((buf[i] >= 0x20 && buf[i] < 0x7F) ? (char)buf[i] : '.');
            u1_puts("'\r\n");
        }
        u1_puts("[BBRX] echo:");
        for (int i = 0; i < got; i++) u1_putc((char)buf[i]);   /* 原样回传 Pi */
        u1_puts("\r\n");
    }
done:
    u1_puts("[BBRX] done\r\n");
    for (;;) { delay_ms(1000); }
}
