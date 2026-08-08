/* uart_fsm2.c - FSM 判决 v2: 拆开 TX/RX/矩阵三段
 * 1) 读真 SYSCLK_CONF(+0x60, 上次 regdump 读错成 0x58) 与 0x58 对照
 * 2) U1 发 5 字节后延时读 TXFIFO_CNT: 排水=TX FSM 活, 积存=TX FSM 死
 * 3) U1RXD ← 常量1(sig60)/常量0(sig56) 合成帧 + 每步读 STATUS bit15(RXD)
 *    RXD 跟常量走 → 矩阵mux活; 恒0 → mux死
 *    常量伪帧入 FIFO → RX FSM 活, 锅在 pad 输入级
 * 4) 对照: U0RXD(sig6) ← 常量1/0 合成帧, 入 U0 FIFO → 判 RX FSM 是否 U1 专属问题
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
#define SYSCLK_58  R(0x600C0058u)   /* regdump 误读的地址 */
#define SYSCLK_60  R(0x600C0060u)   /* 真 SYSTEM_SYSCLK_CONF */
#define CPU_PER_08  R(0x600C0008u)   /* regdump 用的 */
#define CPU_PER_10  R(0x600C0010u)   /* 真 SYSTEM_CPU_PER_CONF */

#define GPIO_BASE  0x60004000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_FUNC_IN_SEL(sig)  GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)

#define U0(a) (*(volatile unsigned int *)(0x60000000u + (a)))
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))

#define CONST1 56u   /* GPIO_MATRIX_CONST_ONE_INPUT  = 0x38 */
#define CONST0 60u   /* GPIO_MATRIX_CONST_ZERO_INPUT = 0x3C */

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
static unsigned int u1_rxd(void) { return (U1(0x1C) >> 15) & 1u; }

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
    uart_puts("\r\n[FSM2] ===== sysclk truth =====\r\n");
    uart_puts("0x58="); put_hex(SYSCLK_58);
    uart_puts(" 0x60="); put_hex(SYSCLK_60); uart_puts("\r\n");
    uart_puts("cpu08="); put_hex(CPU_PER_08);
    uart_puts(" cpu10="); put_hex(CPU_PER_10); uart_puts("\r\n");

    /* UART1 初始化 (与 rxdec 一致) */
    R(0x600C0018u) |= (1u << 5);
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(1);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);

    uart_puts("[FSM2] ===== U1 TX drain test =====\r\n");
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);  /* 先挂常量1, 与 pad 无关 */
    for (int i = 0; i < 5; i++) {
        U1(0x00) = (unsigned int)"HELLO"[i];
    }
    uart_puts("txfifo+0="); put_hex((U1(0x1C) >> 16) & 0x3FFu); uart_puts("\r\n");
    delay_ms(50);
    unsigned int t2 = (U1(0x1C) >> 16) & 0x3FFu;
    uart_puts("txfifo+50ms="); put_hex(t2);
    uart_puts(t2 == 0 ? " << TX-ALIVE\r\n" : " << TX-DEAD\r\n");
    unsigned int fsm_t = U1(0x6C);
    uart_puts("fsm="); put_hex(fsm_t);
    uart_puts(" txd="); uart_putc('0' + ((U1(0x1C) >> 31) & 1u)); uart_puts("\r\n");

    uart_puts("[FSM2] ===== U1 RX const-synth =====\r\n");
    for (int i = 0; i < 130; i++) {
        if ((U1(0x1C) & 0x3FFu) == 0) break;
        (void)U1(0x00);
    }
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);   /* 恒高 */
    delay_ms(30);
    uart_puts("hi: rxd="); uart_putc('0' + u1_rxd());
    uart_puts(" fsm="); put_hex(U1(0x6C)); uart_puts("\r\n");
    GPIO_FUNC_IN_SEL(15) = CONST0 | (1u << 6);   /* 恒低 */
    delay_ms(30);
    uart_puts("lo: rxd="); uart_putc('0' + u1_rxd());
    uart_puts(" fsm="); put_hex(U1(0x6C)); uart_puts("\r\n");
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);   /* 恒高 */
    delay_ms(50);
    unsigned int c = U1(0x1C) & 0x3FFu;
    uart_puts("done: cnt="); put_hex(c);
    uart_puts(" rxd="); uart_putc('0' + u1_rxd());
    uart_puts(" fsm="); put_hex(U1(0x6C)); uart_puts("\r\n");
    if (c > 0 && c <= 128) {
        uart_puts("U1 synth frames:");
        for (unsigned int k = 0; k < c && k < 8; k++) put_hex(U1(0x00) & 0xFFu);
        uart_puts(" << U1RX-FSM-ALIVE\r\n");
    } else {
        uart_puts("<< U1RX-DEAD\r\n");
    }

    uart_puts("[FSM2] ===== U0 RX const-synth (对照) =====\r\n");
    /* U0 已被 ROM 配好 115200 (我们在用它打印), 只挂信号 + 合成 */
    unsigned int u0_before = U0(0x1C) & 0x3FFu;
    for (int i = 0; i < 130; i++) {
        if ((U0(0x1C) & 0x3FFu) == 0) break;
        (void)U0(0x00);
    }
    GPIO_FUNC_IN_SEL(6) = CONST1 | (1u << 6);    /* U0RXD = sig 6 */
    delay_ms(30);
    GPIO_FUNC_IN_SEL(6) = CONST0 | (1u << 6);
    delay_ms(30);
    GPIO_FUNC_IN_SEL(6) = CONST1 | (1u << 6);
    delay_ms(50);
    unsigned int c0 = U0(0x1C) & 0x3FFu;
    uart_puts("U0 synth cnt="); put_hex(c0);
    uart_puts(" rxd="); uart_putc('0' + ((U0(0x1C) >> 15) & 1u));
    uart_puts(" fsm="); put_hex(U0(0x6C)); uart_puts("\r\n");
    if (c0 > 0 && c0 <= 128) {
        uart_puts("U0 frames:");
        for (unsigned int k = 0; k < c0 && k < 8; k++) put_hex(U0(0x00) & 0xFFu);
        uart_puts(" << U0RX-FSM-ALIVE\r\n");
    } else {
        uart_puts("<< U0RX-DEAD\r\n");
    }
    uart_puts("u0_before="); put_hex(u0_before); uart_puts("\r\n");
    uart_puts("[FSM2] done\r\n");
    for (;;) { delay_ms(1000); }
}
