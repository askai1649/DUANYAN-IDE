/* uart_regdump.c - 冻结输入根因排查: dump 关键寄存器
 * 1) RTC_CNTL 状态/选项寄存器 (HOLD_FORCE/STORED 等)
 * 2) SYSTEM CPU_PER_CONF (时钟源确认)
 * 3) GPIO.PIN4/PIN5 vs PIN43 对比 (PAD_HOLD/输入配置位)
 * 4) U1RXD 强制常量1 + 直接向 RX FIFO 压入测试, 排除接收核心问题
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

#define RTC_CNTL_BASE 0x60008000u
#define R(a) (*(volatile unsigned int *)(a))

#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define SYSTEM_CPU_PER_CONF  (*(volatile unsigned int *)0x600C0008)
#define SYSTEM_SYSCLK_CONF   (*(volatile unsigned int *)0x600C0058)

#define GPIO_BASE  0x60004000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_IN          GPIO_REG(GPIO_BASE + 0x3C)
#define GPIO_FUNC_IN_SEL(sig) GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)

#define UART1_FIFO   (*(volatile unsigned int *)0x60010000)
#define UART1_CLKDIV (*(volatile unsigned int *)0x60010014)
#define UART1_STATUS (*(volatile unsigned int *)0x6001001C)
#define UART1_CONF0  (*(volatile unsigned int *)0x60010020)
#define UART1_CLK_CONF (*(volatile unsigned int *)0x60010078)

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
static void dump(const char *name, unsigned int v)
{
    uart_puts(name); put_hex(v); uart_puts("\r\n");
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
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
    uart_puts("\r\n[REGDUMP] ===== clock & rtc =====\r\n");
    dump("SYSCLK_CONF =", SYSTEM_SYSCLK_CONF);
    dump("CPU_PER_CONF =", SYSTEM_CPU_PER_CONF);
    dump("PERIP_CLK_EN0 =", SYSTEM_PERIP_CLK_EN0);
    dump("RTC_OPTIONS0(0x00) =", R(RTC_CNTL_BASE + 0x00));
    dump("RTC_CNTL_TIME0(0x1C) =", R(RTC_CNTL_BASE + 0x1C));
    dump("RTC_DIG_ISO(0x84)? =", R(RTC_CNTL_BASE + 0x84));

    uart_puts("[REGDUMP] ===== gpio pin regs =====\r\n");
    dump("PIN4  =", GPIO_REG(GPIO_BASE + 0x74 + 4 * 4));
    dump("PIN5  =", GPIO_REG(GPIO_BASE + 0x74 + 5 * 4));
    dump("PIN43 =", GPIO_REG(GPIO_BASE + 0x74 + 43 * 4));
    dump("PIN44 =", GPIO_REG(GPIO_BASE + 0x74 + 44 * 4));
    dump("GPIO_IN =", GPIO_IN);

    uart_puts("[REGDUMP] ===== u1rxd synth-frame test =====\r\n");
    /* 先完整初始化 UART1 (与已验证的环回配置一致) */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);
    UART1_CLKDIV = (40000000u / 115200u);
    UART1_CLK_CONF = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(1);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    /* SEL=0+EN=0 → 输入恒 0; bit5 INV 反转 → 恒 1。
     * 先恒高(INV=1), 再拉低(INV=0)产生起始位, 再拉高 → 合成伪帧 0x00。
     * 若 FIFO 出帧 → RX 核心活, 问题在 pad 输入级 */
    GPIO_FUNC_IN_SEL(15) = (1u << 5);           /* 恒高 */
    delay_ms(100);
    /* 读空 */
    for (int i = 0; i < 130; i++) {
        if ((UART1_STATUS & 0x3FFu) == 0) break;
        (void)UART1_FIFO;
    }
    GPIO_FUNC_IN_SEL(15) = 0;                    /* 拉低: 起始位 */
    delay_ms(50);                                /* ≈360ms 慢钟 ≫ 1 bit(8.7us) */
    GPIO_FUNC_IN_SEL(15) = (1u << 5);            /* 拉高: 数据位全1+停止位 */
    delay_ms(100);
    unsigned int c3 = UART1_STATUS & 0x3FFu;
    uart_puts("synth fifo="); put_hex(c3); uart_puts("\r\n");
    if (c3 > 0 && c3 <= 128) {
        uart_puts("synth frames: ");
        for (unsigned int k = 0; k < c3 && k < 8; k++) put_hex(UART1_FIFO & 0xFFu);
        uart_puts(" << CORE-ALIVE\r\n");
    } else {
        uart_puts("<< CORE-SUSPECT (or timing)\r\n");
    }
    uart_puts("[REGDUMP] done\r\n");
    for (;;) { delay_ms(1000); }
}
