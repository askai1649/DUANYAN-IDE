/* uart_h8diag.c - hello8 RX 采样诊断探针
 * 波形捕获已证实线上信号正确 → 本固件复现 rx_byte 时序,
 * 但把每个采样点的电平+距下降沿的 tick 偏移汇出打印,
 * 直接看采样点落在波形的哪个位置
 * 协议: U*8 → 2s → HELLO-PI; 只诊断首个抓到的下降沿后 120 个采样点
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

/* 延时 d tick (规范环 men=0) */
static void asm_delay(volatile unsigned int *ain, unsigned int d)
{
    asm_ticks(ain, d, 0u, 0u);
}
/* 单次读 GPIO5 电平 */
static unsigned int rd5(volatile unsigned int *ain)
{
    unsigned int v;
    __asm__ volatile ("l32i %[v], %[a], 0\n\t" : [v] "=r"(v) : [a] "r"(ain));
    return (v >> 5) & 1u;
}

/* 诊断采样: 下降沿后按 rx_byte 相同时序取 120 点
 * offs[i]=距沿 tick 数, lvl[i]=电平。返回采样点数 */
static unsigned int diag_sample(volatile unsigned int *ain, unsigned int bitd,
                                unsigned int *offs, unsigned char *lvls,
                                unsigned int maxn)
{
    unsigned int n = 0, i = 0;
    unsigned int s = bitd + bitd / 2u;   /* 首采样点: 1.5 位宽 (同 rx_byte) */
    while (n < maxn && i < 120u) {
        asm_delay(ain, s);
        offs[n] = (i == 0) ? s : s;       /* 每段延时 (首段 1.5bd, 其后 bd) */
        lvls[n] = (unsigned char)rd5(ain);
        n++;
        i++;
        s = bitd;
    }
    return n;
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
    uart_puts("\r\n[H8DIAG] rx sampling diagnostic\r\n");

    GPIO_ENABLE_W1TC = (1u << 5);
    IOMUX_PIN(5) |= (1u << 9);

    static unsigned int offs[128];
    static unsigned char lvls[128];

    for (int round = 0; round < 30; round++) {
        uart_puts("[DIAG] await burst\r\n");
        if (asm_ticks(GPIO_IN_ADDR, 30000000u, 1u, 1u) >= 30000000u) {
            uart_puts("[DIAG] timeout\r\n");
            continue;
        }
        unsigned int bitd = asm_ticks(GPIO_IN_ADDR, 5000000u, 1u, 0u);
        if (bitd == 0 || bitd >= 5000000u) {
            uart_puts("[DIAG] calib noedge\r\n");
            continue;
        }
        uart_puts("[DIAG] bitd="); put_dec(bitd); uart_puts("\r\n");

        /* 排空 U 突发 (同 hello8) */
        for (;;) {
            if (asm_ticks(GPIO_IN_ADDR, bitd * 3u, 1u, 1u) >= bitd * 3u) break;
            asm_ticks(GPIO_IN_ADDR, bitd * 14u, 1u, 0u);
        }
        uart_puts("[DIAG] drained, await payload fall\r\n");
        /* 等负载首个下降沿 */
        if (asm_ticks(GPIO_IN_ADDR, 15000000u, 1u, 1u) >= 15000000u) {
            uart_puts("[DIAG] payload timeout\r\n");
            continue;
        }
        /* 复现 rx_byte 采样时序, 汇出电平 */
        unsigned int n = diag_sample(GPIO_IN_ADDR, bitd, offs, lvls, 120);
        unsigned int acc = 0;
        uart_puts("[DIAG] n="); put_dec(n); uart_puts(" seq:");
        for (unsigned int k = 0; k < n; k++) {
            acc += offs[k];
            uart_putc(' ');
            put_dec(acc);
            uart_putc(lvls[k] ? 'H' : 'L');
        }
        uart_puts("\r\n");
    }
    for (;;) { delay_ms(1000); }
}
