/* DUANYAN 闭环验证固件 - ESP32-S3 裸机 UART 输出
 * 烧录后通过 UART0 (GPIO43) 输出 "[DUANYAN-OK]"
 *
 * 关键洞察: ROM 启动 banner 在 115200 baud 下可读,
 * 说明 ROM 已把 UART0 clkdiv 配好 —— 绝不重写 CLKDIV, 直接发字符。
 *
 * 入口: void _start(void) __attribute__((section(".entry"), used));
 */

/* UART0 寄存器 (ESP32-S3) */
#define UART0_BASE      0x60000000UL
#define UART0_FIFO      (*(volatile unsigned int *)(UART0_BASE + 0x00))
#define UART0_STATUS    (*(volatile unsigned int *)(UART0_BASE + 0x1C))

/* 看门狗寄存器 (防止 ROM 留下的 WDT 周期复位)
 * 偏移已对照 esp-idf v5.2.2 esp32s3 官方头文件核实 */
#define RTC_CNTL_BASE   0x60008000UL
#define RTC_CNTL_WDTCONFIG0 (*(volatile unsigned int *)(RTC_CNTL_BASE + 0x98))
#define RTC_CNTL_WDTFEED    (*(volatile unsigned int *)(RTC_CNTL_BASE + 0xAC))
#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)(RTC_CNTL_BASE + 0xB0))
/* SWD 超级看门狗 (rst:0x12 SUPER_WDT_RST 就是它!) */
#define RTC_CNTL_SWD_CONF     (*(volatile unsigned int *)(RTC_CNTL_BASE + 0xB4))
#define RTC_CNTL_SWD_WPROTECT (*(volatile unsigned int *)(RTC_CNTL_BASE + 0xB8))
#define RTC_CNTL_SWD_DISABLE_BIT (1u << 30)
#define SWD_WKEY_VALUE    0x8F1D312AUL
#define TIMG0_BASE      0x6001F000UL
#define TIMG0_WDTCONFIG0  (*(volatile unsigned int *)(TIMG0_BASE + 0x48))
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)(TIMG0_BASE + 0x64))
#define TIMG0_INT_CLR_TIMERS (*(volatile unsigned int *)(TIMG0_BASE + 0x7C))
#define WDT_WKEY_VALUE  0x50D83AA1UL

static void delay(volatile unsigned int count);
static void uart_putc(char c);
static void uart_puts(const char *s);
static void uart_num(unsigned int v);

/* 入口函数 _start: .entry 段被链接脚本放在代码段起始,
 * ENTRY(_start) 使 ELF e_entry = 本函数真实地址 */
void _start(void) __attribute__((section(".entry"), used));
void _start(void) {
    /* 0. 立刻输出一行: 即使看门狗在毫秒后复位, 也留下运行证据 */
    uart_puts("[DUANYAN-OK] boot!\r\n");

    /* 1. 关看门狗
     * 注意: RTC WDT 寄存器有写保护, 必须先写 WDTWPROTECT 解锁! */
    RTC_CNTL_WDTWPROTECT = WDT_WKEY_VALUE;   /* 解锁 RTC WDT */
    RTC_CNTL_WDTFEED = 1;                    /* 先喂一口, 争取时间 */
    RTC_CNTL_WDTCONFIG0 = 0;                 /* RTC WDT off */
    RTC_CNTL_WDTWPROTECT = 0;                /* 重新上锁 */
    RTC_CNTL_SWD_WPROTECT = SWD_WKEY_VALUE;  /* 解锁 SWD */
    RTC_CNTL_SWD_CONF = RTC_CNTL_SWD_DISABLE_BIT; /* SWD off */
    RTC_CNTL_SWD_WPROTECT = 0;
    TIMG0_WDTWPROTECT = WDT_WKEY_VALUE;      /* 解锁 TIMG0 WDT */
    TIMG0_WDTCONFIG0 = 0;                    /* TIMG0 WDT off */
    TIMG0_WDTWPROTECT = 0;
    TIMG0_INT_CLR_TIMERS = 0xFFFFFFFF;       /* 清中断标志 */

    /* 2. 不动 CLKDIV! ROM 已为 115200 配好 (banner 可读是证据) */
    for (unsigned int round = 0; ; round++) {
        uart_puts("[DUANYAN-OK] bare-metal hello from ESP32-S3! round=");
        uart_num(round);
        uart_puts("\r\n");
        delay(20000000);
    }
}

static void delay(volatile unsigned int count) {
    while (count--) {
        __asm__ volatile("nop");
    }
}

static void uart_putc(char c) {
    /* S3 的 TXFIFO_CNT 在 STATUS 寄存器 bit[25:16] (与老ESP32的bit20不同!),
     * 等待 FIFO 有空位 */
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

static void uart_num(unsigned int v) {
    char tmp[10];
    int t = 0;
    do { tmp[t++] = (char)('0' + (v % 10)); v /= 10; } while (v > 0);
    while (t > 0) { uart_putc(tmp[--t]); }
}
