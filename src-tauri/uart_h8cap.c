/* uart_h8cap.c - hello8 波形捕获探针
 * 目的: v8/v9 解码全 0x00 但环体反汇编验证无误 → 直接捕获负载窗
 *       GPIO5 边沿时间戳 (tick 度量), 查明采样点实际落在什么波形上
 * 协议: Pi 发 U*8 → 2s → HELLO-PI; ESP 校准 bitd 后进入纯 asm 紧环
 *       捕获 600 ticks 窗口内的全部边沿并打印相对时间戳
 * 控制台: UART0 (COM4)
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
#define GPIO_IN_ADDR       (volatile unsigned int *)(GPIO_BASE + 0x3C)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC   R(GPIO_BASE + 0x28)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_dec(unsigned int v)
{
    char b[12]; int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { b[n++] = '0' + v % 10; v /= 10; }
    while (n--) uart_putc(b[n]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}

/* 规范环 (与 hello8 完全一致) */
static unsigned int asm_ticks(volatile unsigned int *ain, unsigned int n,
                              unsigned int men, unsigned int xorv)
{
    unsigned int s = n, v, lvl, x, h;
    __asm__ volatile (
        "1:\n\t"
        "l32i %[v], %[a], 0\n\t"
        "extui %[lvl], %[v], 5, 1\n\t"
        "xor %[x], %[lvl], %[xv]\n\t"
        "and %[h], %[x], %[m]\n\t"
        "bnez %[h], 2f\n\t"
        "addi %[s], %[s], -1\n\t"
        "bnez %[s], 1b\n\t"
        "2:\n"
        : [s] "+r"(s), [v] "=&r"(v), [lvl] "=&r"(lvl),
          [x] "=&r"(x), [h] "=&r"(h)
        : [a] "r"(ain), [xv] "r"(xorv), [m] "r"(men)
        : "memory");
    return n - s;
}

/* 边沿捕获紧环: 返回捕获边沿数; ts[] 存相对起始 tick */
static unsigned int cap_edges(volatile unsigned int *ain, unsigned int total,
                              unsigned int *ts, unsigned int maxn)
{
    unsigned int cnt = 0, prev, consumed = 0;
    unsigned int v, lvl, x;
    __asm__ volatile (
        "l32i %[v], %[a], 0\n\t"
        "extui %[prev], %[v], 5, 1\n\t"
        "1:\n\t"
        "l32i %[v], %[a], 0\n\t"
        "extui %[lvl], %[v], 5, 1\n\t"
        "xor %[x], %[lvl], %[prev]\n\t"
        "beqz %[x], 2f\n\t"
        "mov %[prev], %[lvl]\n\t"
        "s32i %[t], %[p], 0\n\t"
        "addi %[p], %[p], 4\n\t"
        "addi %[c], %[c], 1\n\t"
        "beq %[c], %[mx], 3f\n\t"
        "2:\n\t"
        "addi %[t], %[t], 1\n\t"
        "bltu %[t], %[tt], 1b\n\t"
        "3:\n"
        : [c] "+r"(cnt), [t] "+r"(consumed), [p] "+r"(ts),
          [v] "=&r"(v), [lvl] "=&r"(lvl), [x] "=&r"(x), [prev] "=&r"(prev)
        : [a] "r"(ain), [tt] "r"(total), [mx] "r"(maxn)
        : "memory");
    return cnt;
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

    delay_ms(300);
    uart_puts("\r\n[H8CAP] edge capture probe\r\n");

    GPIO_ENABLE_W1TC = (1u << 5);
    IOMUX_PIN(5) |= (1u << 9);

    static unsigned int ts[96];
    for (int round = 0; round < 30; round++) {
        uart_puts("[CAP] await burst\r\n");
        if (asm_ticks(GPIO_IN_ADDR, 30000000u, 1u, 1u) >= 30000000u) {
            uart_puts("[CAP] timeout\r\n");
            continue;
        }
        unsigned int bitd = asm_ticks(GPIO_IN_ADDR, 5000000u, 1u, 0u);
        if (bitd == 0 || bitd >= 5000000u) {
            uart_puts("[CAP] calib noedge\r\n");
            continue;
        }
        uart_puts("[CAP] bitd="); put_dec(bitd); uart_puts("\r\n");

        /* 排空 U 突发 */
        for (;;) {
            if (asm_ticks(GPIO_IN_ADDR, bitd * 3u, 1u, 1u) >= bitd * 3u) break;
            asm_ticks(GPIO_IN_ADDR, bitd * 14u, 1u, 0u);
        }
        /* 捕获窗: 600 万 tick (~6s), 覆盖 2s 间隔 + 8.3ms 负载 */
        unsigned int n = cap_edges(GPIO_IN_ADDR, 6000000u, ts, 96);
        uart_puts("[CAP] edges="); put_dec(n); uart_puts(" ts:");
        unsigned int lim = n > 60 ? 60 : n;
        for (unsigned int i = 0; i < lim; i++) {
            uart_putc(' ');
            put_dec(ts[i]);
        }
        uart_puts("\r\n");
    }
    for (;;) { delay_ms(1000); }
}
