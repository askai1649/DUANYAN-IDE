/* cal_probe.c - 校准诊断: 验证 ticks_per_us 校准是否可靠
 *
 * 方法: 发送 1000 个字符, 等 FIFO 排空, 测量 timer ticks。
 * UART 真实波特率未知, 但串口监视器按 115200 显示我们的输出。
 * 每 5 秒打印一次 "CALIB ticks=NNNN tpu=N", 用一元 '#' 打印 tpu 值,
 * 可在任意波特率下数出个数。
 */

#define UART0_FIFO    (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS  (*(volatile unsigned int *)0x6000001C)

#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)

#define TIMG0_T0CONFIG  (*(volatile unsigned int *)0x6001F000)
#define TIMG0_T0LO      (*(volatile unsigned int *)0x6001F004)
#define TIMG0_T0UPDATE  (*(volatile unsigned int *)0x6001F00C)
#define TIMG0_WDTCONFIG0 (*(volatile unsigned int *)0x6001F048)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_INT_CLR   (*(volatile unsigned int *)0x6001F07C)

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

static void uart_num(unsigned int v)
{
    char tmp[12]; int t = 0;
    do { tmp[t++] = (char)('0' + (v % 10)); v /= 10; } while (v > 0);
    while (t > 0) uart_putc(tmp[--t]);
}

static unsigned int timer_now(void)
{
    TIMG0_T0UPDATE = 0;
    return TIMG0_T0LO;
}

static void disable_watchdogs(void)
{
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_WDTWPROTECT = 0;
    RTC_CNTL_SWD_WPROT = 0x8F1D312A;
    RTC_CNTL_SWD_CONF |= (1u << 30);
    RTC_CNTL_SWD_WPROT = 0;
    TIMG0_WDTWPROTECT = 0x50D83AA1;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0x1;
    TIMG0_WDTWPROTECT = 0;
}

void _start(void) __attribute__((section(".entry"), used));

void _start(void)
{
    disable_watchdogs();
    TIMG0_T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);

    for (;;) {
        /* 发 1000 字符并等 FIFO 排空, 测 ticks */
        unsigned int t0 = timer_now();
        for (int i = 0; i < 1000; i++) uart_putc('~');
        while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
        unsigned int ticks = timer_now() - t0;

        uart_puts("\r\nCALIB ticks=");
        uart_num(ticks);
        uart_puts(" tpu=");
        unsigned int tpu = ticks / 86800;   /* 1000 字符 @115200 = 86.8ms */
        uart_num(tpu);
        uart_puts(" #");
        for (unsigned int i = 0; i < tpu && i < 40; i++) uart_putc('#');
        uart_puts("#\r\n");

        /* 粗延时 ~5 秒 (不依赖校准: 空转计数) */
        volatile unsigned int spin = 0;
        for (unsigned int i = 0; i < 3000000; i++) spin++;
    }
}
