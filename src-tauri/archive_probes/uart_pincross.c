/* uart_pincross.c - 交叉判决 v1:
 * GPIO2 刚证实双向活 (自驱跟随完美)。U1RXD 改挂 GPIO2:
 *  A) U1TX(GPIO4) 发 HELLO → U1RX(GPIO2) 收: 中则 GPIO4 输入坏, 改用脚
 *  B) GPIO2 自驱伪帧 (输出交替, pad 自环回输入) → 收: 中则 U1RX 全活
 * 同时监视 GPIO_IN bit4 (GPIO4 pad 实际电平)
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
static void drain(void)
{
    for (int i = 0; i < 130; i++) {
        if ((U1(0x1C) & 0x3FFu) == 0) break;
        (void)U1(0x00);
    }
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
    uart_puts("\r\n[PINCROSS] init\r\n");
    R(0x600C0018u) |= (1u << 5);
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(1);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);

    /* TX 侧: GPIO4 = U1TXD */
    IOMUX_PIN(4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_OUT_SEL(4) = 15;
    GPIO_ENABLE_W1TS = (1u << 4);
    /* RX 侧: GPIO2 输入使能, U1RXD <- GPIO2 */
    IOMUX_PIN(2) |= (1u << 9) | (1u << 8);   /* FUN_IE + 上拉 */
    GPIO_FUNC_IN_SEL(15) = 2 | (1u << 6);
    delay_ms(10);

    uart_puts("[PINCROSS] in4="); uart_putc('0' + ((GPIO_IN >> 4) & 1u));
    uart_puts(" in2="); uart_putc('0' + ((GPIO_IN >> 2) & 1u)); uart_puts("\r\n");

    /* ---- A) TX(GPIO4 悬空不接2) → RX(GPIO2 靠上拉恒高): 应无数据, 判 RX 安静 ---- */
    drain();
    delay_ms(50);
    uart_puts("[PINCROSS] quiet cnt="); put_hex(U1(0x1C) & 0x3FFu); uart_puts("\r\n");

    /* ---- B) GPIO2 自驱伪帧: 输出 0→1 方波, 自己 pad 回读自己 ---- */
    drain();
    GPIO_ENABLE_W1TS = (1u << 2);
    GPIO_OUT &= ~(1u << 2);           /* 低: start */
    delay_ms(2);
    GPIO_OUT |= (1u << 2);            /* 高: stop */
    delay_ms(2);
    GPIO_OUT &= ~(1u << 2);           /* 低 */
    delay_ms(2);
    GPIO_OUT |= (1u << 2);            /* 高 */
    delay_ms(50);
    unsigned int cb = U1(0x1C) & 0x3FFu;
    uart_puts("[PINCROSS] selfdrv cnt="); put_hex(cb);
    uart_puts(" d=");
    for (unsigned int i = 0; i < cb && i < 8; i++) put_hex(U1(0x00) & 0xFFu);
    uart_puts(cb ? " << B-RX-ALIVE\r\n" : " << B-DEAD\r\n");
    /* 释放 GPIO2 输出, 恢复纯输入+上拉 */
    R(GPIO_BASE + 0x28) = (1u << 2);

    /* ---- C) 把 GPIO4 与 GPIO2 内部短接: OUT_SEL(2)=U1TXD, GPIO2 既出又入 ---- */
    drain();
    GPIO_FUNC_OUT_SEL(2) = 15;         /* GPIO2 也输出 U1TXD */
    GPIO_ENABLE_W1TS = (1u << 2);
    delay_ms(5);
    const char *msg = "HELLO";
    for (int i = 0; i < 5; i++) u1_putc(msg[i]);
    delay_ms(30);
    unsigned int cc = U1(0x1C) & 0x3FFu;
    uart_puts("[PINCROSS] wire2 cnt="); put_hex(cc);
    uart_puts(" d=");
    for (unsigned int i = 0; i < cc && i < 8; i++) uart_putc((char)(U1(0x00) & 0xFFu));
    uart_puts(cc == 5 ? " << C-MATCH\r\n" : " << C-MISS\r\n");
    uart_puts("[PINCROSS] done\r\n");
    for (;;) { delay_ms(1000); }
}
