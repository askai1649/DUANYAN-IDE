/* uart_ilb.c - 内部环回判决 v4: 把 UART RX 与 GPIO 矩阵彻底解耦
 * 背景: sigscan 实锤——sig9..23 全挂 CONST0|EN, U0/U1 RXD 都恒 1,
 *       RXD 根本没接矩阵输出; 外设复位脉冲/嫌疑位清零均无效。
 * 判决: CONF0 bit14 (UART_LOOPBACK) 在 UART 内部把 TXD 直连 RXD,
 *       完全绕开 GPIO 矩阵。发 5 字节, 读 RXFIFO:
 *   MATCH → RX 解码器/FIFO/时钟全活 → 死段精确在 "矩阵输出→RXD" 连线
 *           (FPGA 布线缺陷), RX 侧到此无可软件修复, 需换策略
 *   MISS  → RX FSM 本体死 (时钟/复位级), 继续查 CONF0/CLK_CONF 回读
 * 附加: 回读 CONF0/CLK_CONF 确认真值; 试 CONF0 bit19 RXD_INV 自测。
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
#define GPIO_FUNC_IN_SEL(sig)  R(GPIO_BASE + 0x154 + (sig) * 4)
#define PERIP_CLK_EN0      R(0x600C0018u)

#define U0(a) (*(volatile unsigned int *)(0x60000000u + (a)))
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))
#define CONST1 56u
#define CONST0 60u
#define LOOPBACK_BIT (1u << 14)   /* UART_LOOPBACK: TXD 内部直连 RXD */

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
        if (((U1(0x1C) >> 16) & 0x3FF) < 127) break;
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
    uart_puts("\r\n[ILB] UART1 内部环回判决\r\n");

    /* 初始化 (不开 loopback) */
    PERIP_CLK_EN0 |= (1u << 5);
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(2);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    U1(0x18) = 0;   /* RX_FILT 清零 */

    uart_puts("[ILB] conf0="); put_hex(U1(0x20));
    uart_puts(" clkconf="); put_hex(U1(0x78));
    uart_puts(" clkdiv="); put_hex(U1(0x14));
    uart_puts(" status="); put_hex(U1(0x1C)); uart_puts("\r\n");

    /* 读空 RXFIFO */
    for (int i = 0; i < 128; i++) {
        if ((U1(0x1C) & 0x3FFu) == 0) break;
        (void)U1(0x00);
    }

    /* 开 loopback: RXD 内部取自 TXD, 矩阵被绕开 */
    U1(0x20) |= LOOPBACK_BIT;
    uart_puts("[ILB] loopback on, conf0="); put_hex(U1(0x20));
    uart_puts(" rxd="); uart_putc('0' + ((U1(0x1C) >> 15) & 1u)); uart_puts("\r\n");

    for (int round = 0; round < 3; round++) {
        u1_putc('H'); u1_putc('E'); u1_putc('L'); u1_putc('L'); u1_putc('O');
        delay_ms(20);   /* 5 字节 @115200 ≈ 0.43ms */
        unsigned int cnt = U1(0x1C) & 0x3FFu;
        uart_puts("[ILB] round="); uart_putc('0' + round);
        uart_puts(" rxfifo="); put_hex(cnt);
        uart_puts(" fsm="); put_hex(U1(0x6C));
        uart_puts(" data=");
        for (unsigned int i = 0; i < cnt && i < 8; i++) {
            uart_putc((char)(U1(0x00) & 0xFF));
        }
        uart_puts(cnt == 5 ? "  << MATCH\r\n" : "  << MISS\r\n");
    }

    U1(0x20) &= ~LOOPBACK_BIT;   /* 关 loopback */

    /* 附加: 无 loopback 时挂 CONST0|EN, 读 RXFSM/EDGE/pulses, 确认死段 */
    GPIO_FUNC_IN_SEL(15) = CONST0 | (1u << 6);
    delay_ms(5);
    uart_puts("[ILB] no-lb c0: rxd="); uart_putc('0' + ((U1(0x1C) >> 15) & 1u));
    uart_puts(" edge="); put_hex(U1(0x30));
    uart_puts(" fsm="); put_hex(U1(0x6C));
    uart_puts(" low="); put_hex(U1(0x28)); uart_puts("\r\n");
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);

    uart_puts("[ILB] done\r\n");
    for (;;) { delay_ms(1000); }
}
