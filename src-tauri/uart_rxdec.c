/* uart_rxdec.c - RX 判决性探针 v1:
 * 1) 复刻 uart_loop MATCH 配置 (IOMUX4 FUN_IE+上拉, U1RXD<-GPIO4)
 * 2) 读 FSM_STATUS(0x6C): RX FSM 状态机实况 (0=idle)
 * 3) 三轮真帧环回 (U1 TX FSM 发 HELLO, 真实波形)
 * 4) 一轮伪帧 (FUNC_IN_SEL 常量翻转) 对照
 * 判据: 真帧 MATCH 而伪帧 0 → 伪帧时序问题, RX 核心活
 *       真帧也 0 → RX 核心真死, 转向 SYSCLK 修复
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

#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)

#define GPIO_BASE  0x60004000u
#define IOMUX_BASE 0x60009000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)
#define GPIO_ENABLE_W1TS       GPIO_REG(GPIO_BASE + 0x24)

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
    uart_puts("\r\n[RXDEC] ===== init (MATCH config) =====\r\n");
    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25); /* XTAL+SCLK_EN+TX/RX_SCLK_EN */
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);  /* FIFO_RST 脉冲 */
    delay_ms(1);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);  /* MEM_CLK_EN+CLK_EN */
    /* IOMUX GPIO4: MCU_SEL=1 + FUN_IE(输入使能) + DRV2 + FUN_PU(上拉) —— 与 MATCH 版一致 */
    GPIO_REG(IOMUX_BASE + 4 + 4 * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_OUT_SEL(4) = 15;             /* GPIO4 = U1TXD */
    GPIO_ENABLE_W1TS = (1u << 4);
    GPIO_FUNC_IN_SEL(15) = 4 | (1u << 6);  /* U1RXD <- GPIO4 pad */
    delay_ms(10);

    uart_puts("[RXDEC] fsm="); put_hex(U1(0x6C));
    uart_puts(" status="); put_hex(U1(0x1C)); uart_puts("\r\n");

    /* ---- 真帧环回 x3: U1 TX FSM 产生真实 115200 波形 ---- */
    for (int round = 0; round < 3; round++) {
        drain();
        const char *msg = "HELLO";
        for (int i = 0; i < 5; i++) u1_putc(msg[i]);
        delay_ms(20);
        unsigned int cnt = U1(0x1C) & 0x3FFu;
        uart_puts("[RXDEC] real r"); uart_putc('0' + round);
        uart_puts(" cnt="); put_hex(cnt);
        uart_puts(" d=");
        for (unsigned int i = 0; i < cnt && i < 8; i++) uart_putc((char)(U1(0x00) & 0xFFu));
        uart_puts(cnt == 5 ? " << MATCH\r\n" : " << MISS\r\n");
        delay_ms(100);
    }

    /* ---- 伪帧对照: 常量翻转, FSM 实况观察 ---- */
    drain();
    unsigned int f0 = U1(0x6C);
    GPIO_FUNC_IN_SEL(15) = (1u << 5);   /* 恒高 (idle) */
    delay_ms(50);
    GPIO_FUNC_IN_SEL(15) = 0;            /* 恒低 (start bit) */
    delay_ms(30);
    unsigned int f1 = U1(0x6C);          /* 低电平期间: FSM 应离开 idle */
    GPIO_FUNC_IN_SEL(15) = (1u << 5);   /* 恒高 (stop) */
    delay_ms(50);
    unsigned int f2 = U1(0x6C);
    unsigned int cnt = U1(0x1C) & 0x3FFu;
    uart_puts("[RXDEC] synth fsm:"); put_hex(f0); uart_puts("->");
    put_hex(f1); uart_puts("->"); put_hex(f2);
    uart_puts(" cnt="); put_hex(cnt); uart_puts("\r\n");
    if (cnt > 0 && cnt <= 128) {
        uart_puts("[RXDEC] synth frames:");
        for (unsigned int k = 0; k < cnt && k < 8; k++) put_hex(U1(0x00) & 0xFFu);
        uart_puts("\r\n");
    }
    /* FSM[3:0]=RX 状态: 若 f1==f0 则 RX FSM 根本没动 → 核心死 */
    uart_puts((f1 & 0xFu) != (f0 & 0xFu) ? "[RXDEC] FSM-MOVED\r\n" : "[RXDEC] FSM-FROZEN\r\n");
    uart_puts("[RXDEC] done\r\n");
    for (;;) { delay_ms(1000); }
}
