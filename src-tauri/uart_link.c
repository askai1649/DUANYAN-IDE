/* uart_link.c - ESP32-S3 ↔ duanyan-pi UART 主链路探针
 * UART1: TX=GPIO4 (信号15), RX=GPIO5 (信号15), 115200 8N1
 * 行为: 每秒经 UART1 发 "PI-HI <n>\r\n"; 收到字节即回显 "PI-ECHO:<byte>"
 *       同时把所有事件镜像到 UART0 (USB 串口监控口)
 * 寄存器核对: system_reg.h UART1_CLK_EN=bit5 (默认1);
 *             gpio_sig_map.h U1TXD_OUT_IDX=U1RXD_IN_IDX=15
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

/* UART1 (0x60010000), 布局与 UART0 相同 */
#define UART1_FIFO   (*(volatile unsigned int *)0x60010000)
#define UART1_CLKDIV (*(volatile unsigned int *)0x60010014)
#define UART1_STATUS (*(volatile unsigned int *)0x6001001C)
#define UART1_CONF0  (*(volatile unsigned int *)0x60010020)
#define UART1_CLK_CONF (*(volatile unsigned int *)0x60010078)

/* SYSTEM 时钟 (UART1 clk bit5, 默认开, 保险再置一次) */
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)

/* GPIO 矩阵 */
#define GPIO_BASE  0x60004000u
#define IOMUX_BASE 0x60009000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)  /* 官方 gpio_reg.h: FUNC0_IN_SEL_CFG=0x154 */
#define GPIO_ENABLE_W1TS       GPIO_REG(GPIO_BASE + 0x24)

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {   /* 有界等待, 绝不挂死 */
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_dec(unsigned int v)
{
    char buf[11]; int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n) uart_putc(buf[--n]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}

static void u1_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {   /* 有界等待, 绝不挂死 */
        if (((UART1_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART1_FIFO = (unsigned int)c;
}
static void u1_puts(const char *s) { while (*s) u1_putc(*s++); }

static void disable_wdts(void)
{
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF |= (1u << 31);
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0xFFFFFFFFu;
}

static void uart1_init(void)
{
    uart_puts("[U1] clk...");
    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);       /* UART1 时钟 (默认已开) */
    uart_puts("div...");
    UART1_CLKDIV = 40000000u / 115200u;      /* XTAL 40MHz / 347 ≈ 115273 */
    UART1_CLK_CONF = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25); /* XTAL源+全使能 */
    uart_puts("conf0...");
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);  /* BIT_NUM[3:2]=3(8位)+STOP[5:4]=2+FIFO复位 */
    delay_ms(1);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);  /* 释放复位; MEM_CLK_EN+CLK_EN 必须保留! */
    uart_puts("iomux...");
    /* IO_MUX: GPIO4/5 设 GPIO 功能 + 输入使能 + 上拉 */
    GPIO_REG(IOMUX_BASE + 4 + 4 * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8); /* GPIO4=+0x14 */
    GPIO_REG(IOMUX_BASE + 4 + 5 * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8); /* GPIO5=+0x18 */
    uart_puts("matrix...");
    /* GPIO 矩阵: GPIO4 输出 U1TXD(15); U1RXD(15) 输入取自 GPIO5 */
    GPIO_FUNC_OUT_SEL(4) = 15;
    GPIO_ENABLE_W1TS = (1u << 4);            /* 输出驱动使能 */
    GPIO_FUNC_IN_SEL(15) = 5 | (1u << 6);    /* bit6 = 输入选择使能 */
    uart_puts("ok\r\n");
}

void _start(void) __attribute__((section(".entry"), naked));
void _start(void)
{
    disable_wdts();
    delay_ms(3000);  /* 等监控串口打开, 避开盲区 */
    uart1_init();
    uart_puts("\r\n[UART-LINK] ESP32-S3 UART1 @ GPIO4(TX)/GPIO5(RX) 115200\r\n");

    unsigned int n = 0;
    unsigned int total_rx = 0;
    for (;;) {
        /* 1. 每秒向树莓派发一条问候 */
        u1_puts("PI-HI ");
        char buf[8]; int b = 0;
        unsigned int v = n;
        do { buf[b++] = '0' + (v % 10); v /= 10; } while (v);
        while (b) u1_putc(buf[--b]);
        u1_puts("\r\n");
        uart_puts("[TX->PI] PI-HI "); put_dec(n); uart_puts("\r\n");

        /* 2. 1 秒分 10 片轮询接收, 每片最多读 128 字节 (防 FIFO 计数异常死转) */
        for (unsigned int slice = 0; slice < 10; slice++) {
            unsigned int got = 0;
            while (got < 128u) {
                unsigned int cnt = UART1_STATUS & 0x3FFu;
                if (cnt == 0u) break;
                if (cnt > 128u) cnt = 1u;   /* 计数异常时只读 1 字节自恢复 */
                char c = (char)(UART1_FIFO & 0xFFu);
                total_rx++;
                got++;
                uart_puts("[RX<-PI] '"); uart_putc(c); uart_puts("' ");
                put_dec((unsigned int)(unsigned char)c); uart_puts("\r\n");
                u1_puts("PI-ECHO:"); u1_putc(c); u1_puts("\r\n");
            }
            delay_ms(100);
        }
        n++;
    }
}
