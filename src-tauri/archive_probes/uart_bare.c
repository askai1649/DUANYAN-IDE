/* uart_bare.c - 极简启动探针: _start 后不碰任何外设寄存器, 立即打印
 * 若此探针也静默 → 挂死在取指/启动本身 (镜像/链接问题)
 * 若此探针能打印 → 挂死在某条外设寄存器访问上
 */
#define UART0_FIFO   (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS (*(volatile unsigned int *)0x6000001C)

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {   /* 有界等待, 绝不无限循环 */
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

void _start(void) __attribute__((section(".entry"), naked));
void _start(void)
{
    uart_puts("\r\n[BARE] alive-no-periph\r\n");
    uart_puts("[BARE] step1 printed without any reg writes\r\n");
    for (;;) {
        for (volatile unsigned int n = 0; n < 16000000u; n++) {}
        uart_puts("[BARE] tick\r\n");
    }
}
