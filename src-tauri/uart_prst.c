/* uart_prst.c - RXD钉死判决 v2: 嫌疑位清零 + 外设复位脉冲 (IDF标准步骤, 从未做过)
 * 背景: muxchk 实锤——FUNC_IN_SEL 回读正确, 挂 CONST0|EN 时 RXD 仍恒 1,
 *       RXD_EDGE_CNT 恒 0, RX FSM 恒 0; TX 侧全活。
 * 嫌疑 A: FLOW_CONF(0x34)/RS485_CONF(0x4C) 被 ROM/boot 置位 → 先回读再强制清零
 * 嫌疑 B: UART1 自 boot 从未做过外设复位, RX 数据通路冻在复位态
 *         → PERIP_RST_EN0(0x600C0020) bit5 脉冲, 然后完整重初始化
 * 判决: 复位后挂 CONST0|EN → RXD 变 0 → 根因锁定; 再用 GPIO2 自驱过矩阵验证输入通路
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
#define GPIO_OUT_REG       R(GPIO_BASE + 0x04)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_FUNC_IN_SEL(sig)  R(GPIO_BASE + 0x154 + (sig) * 4)
#define IOMUX_PIN(n)       R(0x60009044u + (n) * 4)
#define PERIP_CLK_EN0      R(0x600C0018u)
#define PERIP_RST_EN0      R(0x600C0020u)

#define U0(a) (*(volatile unsigned int *)(0x60000000u + (a)))
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))
#define CONST1 56u
#define CONST0 60u

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

static void dump_susp(const char *tag, int is_u1)
{
    unsigned int b = is_u1 ? 0x60010000u : 0x60000000u;
    uart_puts(tag);
    uart_puts(" flow="); put_hex(R(b + 0x34));
    uart_puts(" sleep="); put_hex(R(b + 0x38));
    uart_puts(" rs485="); put_hex(R(b + 0x4C));
    uart_puts(" conf1="); put_hex(R(b + 0x24));
    uart_puts("\r\n");
}

/* 常量挂 sig15 → 回读 + RXD */
static void chk(const char *tag, unsigned int val)
{
    GPIO_FUNC_IN_SEL(15) = val;
    delay_ms(2);
    unsigned int rb = GPIO_FUNC_IN_SEL(15);
    unsigned int st = U1(0x1C);
    uart_puts(tag);
    uart_puts(" rb="); put_hex(rb);
    uart_puts(" rxd="); uart_putc('0' + ((st >> 15) & 1u));
    uart_puts("\r\n");
}

/* UART1 标准初始化 (与 IDF 对齐) */
static void u1_init(void)
{
    U1(0x14) = (40000000u / 115200u);                          /* CLKDIV=347 */
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25); /* XTAL+SCLK_EN+TX/RX_SCLK_EN */
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);  /* FIFO_RST 脉冲 */
    delay_ms(2);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);  /* MEM_CLK_EN+CLK_EN */
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
    uart_puts("\r\n[PRST] step1 嫌疑位回读(复位前)\r\n");
    dump_susp("[PRST] u1:", 1);
    dump_susp("[PRST] u0:", 0);
    /* 强制清零嫌疑位 */
    U1(0x34) = 0; U1(0x4C) = 0;
    U0(0x34) = 0; U0(0x4C) = 0;

    uart_puts("[PRST] step2 复位脉冲前 u1 const0 基线\r\n");
    PERIP_CLK_EN0 |= (1u << 5);
    u1_init();
    chk("[PRST] pre c0:", CONST0 | (1u << 6));
    chk("[PRST] pre c1:", CONST1 | (1u << 6));

    uart_puts("[PRST] step3 UART1 外设复位脉冲\r\n");
    PERIP_RST_EN0 |= (1u << 5);    /* UART1_RST=1 保持复位 */
    delay_ms(10);
    PERIP_RST_EN0 &= ~(1u << 5);   /* 解除复位 */
    delay_ms(10);
    uart_puts("[PRST] rst_en0="); put_hex(PERIP_RST_EN0); uart_puts("\r\n");

    uart_puts("[PRST] step4 复位后重初始化+常量判决\r\n");
    u1_init();
    dump_susp("[PRST] u1 after:", 1);
    chk("[PRST] post c0:", CONST0 | (1u << 6));   /* ★期望 rxd=0 */
    chk("[PRST] post c1:", CONST1 | (1u << 6));   /* 期望 rxd=1 */
    chk("[PRST] post c0b:", CONST0 | (1u << 6));

    uart_puts("[PRST] step5 GPIO2 自驱过矩阵(输入通路活体检测)\r\n");
    IOMUX_PIN(2) |= (1u << 9);          /* FUN_IE: pad 输入进 GPIO_IN */
    GPIO_FUNC_IN_SEL(15) = 2 | (1u << 6);
    delay_ms(2);
    GPIO_ENABLE_W1TS = (1u << 2);       /* GPIO2 输出使能 */
    for (int i = 0; i < 4; i++) {
        if (i & 1) GPIO_OUT_REG &= ~(1u << 2);
        else       GPIO_OUT_REG |= (1u << 2);
        delay_ms(5);
        unsigned int st = U1(0x1C);
        uart_puts("[PRST] g2 drv="); uart_putc('0' + (i & 1));
        uart_puts(" in2="); uart_putc('0' + ((GPIO_IN >> 2) & 1u));
        uart_puts(" rxd="); uart_putc('0' + ((st >> 15) & 1u));
        uart_puts("\r\n");
    }
    uart_puts("[PRST] edge="); put_hex(U1(0x30)); uart_puts("\r\n");

    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);
    uart_puts("[PRST] done\r\n");
    for (;;) { delay_ms(1000); }
}
