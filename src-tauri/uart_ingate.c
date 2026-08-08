/* uart_ingate.c - GPIO 输入级判决 v1:
 * 事实: 矩阵常量通(U0 FSM 有动), pad 输入死(GPIO_IN 冻结) → 输入同步器疑无时钟
 * 1) 读 GPIO_CLOCK_GATE(+0x62C, 默认应为1) 与 GPIO_DATE(+0x6FC)
 * 2) 置1保险, 回读
 * 3) 自驱动跟随测试: 开 GPIO2 输出交替, 看 GPIO_IN bit2 是否跟随 (纯片内, 不依赖外线)
 * 4) 若输入活: U1RXD<-GPIO4 pad 真帧环回 x2
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
#define GPIO_IN          R(GPIO_BASE + 0x3C)
#define GPIO_OUT         R(GPIO_BASE + 0x04)
#define GPIO_ENABLE_W1TS R(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC R(GPIO_BASE + 0x28)
#define GPIO_ENABLE      R(GPIO_BASE + 0x20)
#define GPIO_CLOCK_GATE  R(GPIO_BASE + 0x62C)
#define GPIO_DATE        R(GPIO_BASE + 0x6FC)
#define GPIO_FUNC_OUT_SEL(pin) R(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  R(GPIO_BASE + 0x154 + (sig) * 4)
#define IOMUX_BASE 0x60009000u
#define IOMUX_PIN(p) R(IOMUX_BASE + 4 + (p) * 4)

#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_hex(unsigned int v)
{
    const char *h = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 7; i >= 0; i--) uart_putc(h[(v >> (i * 4)) & 0xF]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}
static void u1_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((U1(0x1C) >> 16) & 0x3FFu) < 127) break;
    }
    U1(0x00) = (unsigned int)c;
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

    delay_ms(1000);
    uart_puts("\r\n[INGATE] ===== gpio clock gate =====\r\n");
    uart_puts("gate="); put_hex(GPIO_CLOCK_GATE);
    uart_puts(" date="); put_hex(GPIO_DATE); uart_puts("\r\n");
    GPIO_CLOCK_GATE |= 1u;                    /* 保险: 保证输入同步器时钟开 */
    uart_puts("gate(set)="); put_hex(GPIO_CLOCK_GATE); uart_puts("\r\n");
    uart_puts("gpio_in0="); put_hex(GPIO_IN); uart_puts("\r\n");

    uart_puts("[INGATE] ===== self-drive follow (GPIO2) =====\r\n");
    /* GPIO2: 纯 GPIO 输出 (OUT_SEL=0x100 默认即 GPIO_OUT), 交替驱动, 看 IN bit2 跟随 */
    IOMUX_PIN(2) |= (1u << 9);              /* FUN_IE: pad 输入使能, 否则 GPIO_IN 收不到 */
    GPIO_ENABLE_W1TS = (1u << 2);
    for (int i = 0; i < 4; i++) {
        if (i & 1) GPIO_OUT |= (1u << 2); else GPIO_OUT &= ~(1u << 2);
        delay_ms(5);
        unsigned int in = GPIO_IN;
        uart_puts("drv="); uart_putc('0' + (i & 1));
        uart_puts(" in2="); uart_putc('0' + ((in >> 2) & 1u));
        uart_puts(" in="); put_hex(in); uart_puts("\r\n");
    }
    GPIO_ENABLE_W1TC = (1u << 2);

    uart_puts("[INGATE] ===== loopback re-test =====\r\n");
    R(0x600C0018u) |= (1u << 5);
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(1);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    IOMUX_PIN(4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8); /* MCU_SEL+FUN_IE+DRV2+PU */
    GPIO_FUNC_OUT_SEL(4) = 15;
    GPIO_ENABLE_W1TS = (1u << 4);
    GPIO_FUNC_IN_SEL(15) = 4 | (1u << 6);
    delay_ms(10);
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < 130; i++) {
            if ((U1(0x1C) & 0x3FFu) == 0) break;
            (void)U1(0x00);
        }
        const char *msg = "HELLO";
        for (int i = 0; i < 5; i++) u1_putc(msg[i]);
        delay_ms(20);
        unsigned int cnt = U1(0x1C) & 0x3FFu;
        uart_puts("[INGATE] r"); uart_putc('0' + round);
        uart_puts(" cnt="); put_hex(cnt); uart_puts(" d=");
        for (unsigned int i = 0; i < cnt && i < 8; i++) uart_putc((char)(U1(0x00) & 0xFFu));
        uart_puts(cnt == 5 ? " << MATCH\r\n" : " << MISS\r\n");
        delay_ms(100);
    }
    uart_puts("[INGATE] done\r\n");
    for (;;) { delay_ms(1000); }
}
