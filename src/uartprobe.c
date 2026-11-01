/*
 * Firmware de test : cherche les broches UART reliées au connecteur de l'E32.
 * Envoie "MAPn" en boucle à 9600 bauds en passant par les 3 remappages possibles
 * de l'USART1 du STM8L151 : 0 = PC3/PC2, 1 = PA2/PA3, 2 = PC5/PC6.
 */
#include <stdint.h>

#define REG(a) (*(volatile uint8_t *)(a))

#define PA_DDR REG(0x5002)
#define PA_CR1 REG(0x5003)
#define PC_DDR REG(0x500C)
#define PC_CR1 REG(0x500D)
#define CLK_CKDIVR    REG(0x50C0)
#define CLK_PCKENR1   REG(0x50C3)
#define SYSCFG_RMPCR1 REG(0x509E)
#define USART_SR   REG(0x5230)
#define USART_DR   REG(0x5231)
#define USART_BRR1 REG(0x5232)
#define USART_BRR2 REG(0x5233)
#define USART_CR2  REG(0x5235)

static void delay(uint32_t n)
{
    while (n--)
        __asm__("nop");
}

static void putc_(uint8_t c)
{
    while (!(USART_SR & 0x80))
        ;
    USART_DR = c;
}

void main(void)
{
    uint8_t map;
    const char *s;

    CLK_CKDIVR = 0x00;
    CLK_PCKENR1 |= 0x20;
    USART_BRR2 = 0x03;
    USART_BRR1 = 0x68;

    for (;;) {
        for (map = 0; map < 3; map++) {
            USART_CR2 = 0;
            PA_DDR = 0; PA_CR1 = 0;
            PC_DDR = 0; PC_CR1 = 0;
            SYSCFG_RMPCR1 = (SYSCFG_RMPCR1 & ~0x30) | (map << 4);
            if (map == 0) { PC_DDR = 0x08; PC_CR1 = 0x0C; }
            if (map == 1) { PA_DDR = 0x04; PA_CR1 = 0x0C; }
            if (map == 2) { PC_DDR = 0x20; PC_CR1 = 0x60; }
            USART_CR2 = 0x08;
            for (s = "MAP"; *s; s++)
                putc_(*s);
            putc_('0' + map);
            putc_('\r');
            putc_('\n');
            while (!(USART_SR & 0x40))
                ;
            delay(300000);
        }
    }
}
