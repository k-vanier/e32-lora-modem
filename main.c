/*
 * Firmware modem série LoRa transparent pour Ebyte E32 (STM8L151 + SX1276), 868 MHz.
 * Ce qui entre sur l'UART part en radio, ce qui arrive par radio sort sur l'UART.
 *
 * Brochage (déduit du firmware QCZEK) :
 *   SPI1 : PB4 NSS, PB5 SCK, PB6 MOSI, PB7 MISO
 *   SX1276 RESET : PC3     ampli/alim RF : PC4 (haut)
 *   Commutateur antenne : PD2 / PD3
 *   UART1 remappé : PA2 TX, PA3 RX — 9600 8N1
 *   M0 : PC5 (LED d'activité)   M1 : PC6 (bouton bind)   AUX : PD0 (haut = prêt)
 *
 * Compilation : sdcc -mstm8 --opt-code-size main.c
 */
#include <stdint.h>

#define REG(a) (*(volatile uint8_t *)(a))

/* GPIO */
#define PA_ODR REG(0x5000)
#define PA_DDR REG(0x5002)
#define PA_CR1 REG(0x5003)
#define PB_ODR REG(0x5005)
#define PB_DDR REG(0x5007)
#define PB_CR1 REG(0x5008)
#define PB_CR2 REG(0x5009)
#define PC_ODR REG(0x500A)
#define LED_M0 0x20 /* PC5 = M0 */
#define BTN_M1 0x40 /* PC6 = M1 */
#define PC_DDR REG(0x500C)
#define PC_CR1 REG(0x500D)
#define PD_ODR REG(0x500F)
#define PD_DDR REG(0x5011)
#define PD_CR1 REG(0x5012)

/* Horloge / remappage */
#define CLK_CKDIVR    REG(0x50C0)
#define CLK_PCKENR1   REG(0x50C3)
#define SYSCFG_RMPCR1 REG(0x509E)

/* USART1 */
#define USART_SR   REG(0x5230)
#define USART_DR   REG(0x5231)
#define USART_BRR1 REG(0x5232)
#define USART_BRR2 REG(0x5233)
#define USART_CR2  REG(0x5235)

/* SPI1 */
#define SPI_CR1 REG(0x5200)
#define SPI_CR2 REG(0x5201)
#define SPI_SR  REG(0x5203)
#define SPI_DR  REG(0x5204)

/* TIM4 : tick 1 ms */
#define TIM4_CR1  REG(0x52E0)
#define TIM4_IER  REG(0x52E4)
#define TIM4_SR1  REG(0x52E5)
#define TIM4_PSCR REG(0x52E8)
#define TIM4_ARR  REG(0x52E9)

/* Registres SX1276 */
#define SX_FIFO        0x00
#define SX_OPMODE      0x01
#define SX_FRF_MSB     0x06
#define SX_PACONFIG    0x09
#define SX_OCP         0x0B
#define SX_LNA         0x0C
#define SX_FIFO_PTR    0x0D
#define SX_FIFO_TXBASE 0x0E
#define SX_FIFO_RXBASE 0x0F
#define SX_FIFO_RXCUR  0x10
#define SX_IRQFLAGS    0x12
#define SX_RXNB        0x13
#define SX_MODEMCFG1   0x1D
#define SX_MODEMCFG2   0x1E
#define SX_PREAMBLE_L  0x21
#define SX_PAYLOADLEN  0x22
#define SX_MODEMCFG3   0x26
#define SX_SYNCWORD    0x39
#define SX_VERSION     0x42

#define MODE_LORA_SLEEP 0x80
#define MODE_STDBY      0x81
#define MODE_TX         0x83
#define MODE_RXCONT     0x85

#define IRQ_RXDONE  0x40
#define IRQ_CRCERR  0x20
#define IRQ_TXDONE  0x08

#define RF_FRF      0xD9619AUL /* 869,525 MHz (sous-bande 10 %, 500 mW max) = Frf * 32 MHz / 2^19 */
#define PKT_MAX     120    /* taille max d'un paquet radio */
#define IDLE_GAP_MS 5      /* silence UART qui déclenche l'envoi */

/* Lu par SWIM pour le diagnostic : 0x12 = SX1276 détecté */
volatile uint8_t sx_version;

#define RX_BUF_SIZE 1024               /* tampon UART (puissance de 2) */
#define RX_BUF_MASK (RX_BUF_SIZE - 1)
#define RX_BUF_HIGH 768                /* au-delà : AUX bas, ralentir l'envoi */

static volatile uint16_t ms_ticks;
static volatile uint8_t rx_buf[RX_BUF_SIZE];

#define TX_BUF_SIZE 512                /* données radio en attente vers l'UART */
#define TX_BUF_MASK (TX_BUF_SIZE - 1)
static uint8_t tx_buf[TX_BUF_SIZE];
static uint16_t tx_head, tx_tail;
static volatile uint16_t rx_head, rx_tail;

void tim4_isr(void) __interrupt(25)
{
    TIM4_SR1 = 0;
    ms_ticks++;
}

void usart_rx_isr(void) __interrupt(28)
{
    uint8_t c = USART_DR;
    uint16_t next = (rx_head + 1) & RX_BUF_MASK;
    if (next != rx_tail) {
        rx_buf[rx_head] = c;
        rx_head = next;
    }
}

/* nombre d'octets en attente dans le tampon UART */
static uint16_t rx_count(void)
{
    uint16_t h;
    __asm__("sim");
    h = rx_head;
    __asm__("rim");
    return (h - rx_tail) & RX_BUF_MASK;
}

static uint16_t millis(void)
{
    uint16_t t;
    __asm__("sim");
    t = ms_ticks;
    __asm__("rim");
    return t;
}

static void delay_ms(uint16_t ms)
{
    uint16_t start = millis();
    while ((uint16_t)(millis() - start) < ms)
        ;
}

static void uart_putc(uint8_t c)
{
    while (!(USART_SR & 0x80))
        ;
    USART_DR = c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void uart_puthex(uint8_t v)
{
    const char *h = "0123456789ABCDEF";
    uart_putc(h[v >> 4]);
    uart_putc(h[v & 15]);
}

static uint8_t spi_xfer(uint8_t v)
{
    SPI_DR = v;
    while (!(SPI_SR & 0x01))
        ;
    return SPI_DR;
}

#define NSS_LOW()  (PB_ODR &= ~0x10)
#define NSS_HIGH() (PB_ODR |= 0x10)

static uint8_t sx_read(uint8_t reg)
{
    uint8_t v;
    NSS_LOW();
    spi_xfer(reg & 0x7F);
    v = spi_xfer(0);
    NSS_HIGH();
    return v;
}

static void sx_write(uint8_t reg, uint8_t v)
{
    NSS_LOW();
    spi_xfer(reg | 0x80);
    spi_xfer(v);
    NSS_HIGH();
}

/* Commutateur d'antenne : à confirmer par essai (voir README) */
static void rf_switch_tx(void)
{
    PD_ODR |= 0x04;
    PD_ODR &= ~0x08;
}

static void rf_switch_rx(void)
{
    PD_ODR &= ~0x04;
    PD_ODR |= 0x08;
}

static void sx_start_rx(void)
{
    rf_switch_rx();
    sx_write(SX_OPMODE, MODE_STDBY);
    sx_write(SX_FIFO_PTR, 0x00);
    sx_write(SX_IRQFLAGS, 0xFF);
    sx_write(SX_OPMODE, MODE_RXCONT);
}

static void sx_init(void)
{
    uint32_t frf = RF_FRF;

    PC_ODR &= ~0x08;            /* reset SX1276 */
    delay_ms(2);
    PC_ODR |= 0x08;
    delay_ms(10);

    sx_version = sx_read(SX_VERSION);

    sx_write(SX_OPMODE, MODE_LORA_SLEEP);
    delay_ms(10);
    sx_write(SX_FRF_MSB,     (uint8_t)(frf >> 16));
    sx_write(SX_FRF_MSB + 1, (uint8_t)(frf >> 8));
    sx_write(SX_FRF_MSB + 2, (uint8_t)frf);
    sx_write(SX_FIFO_TXBASE, 0x00);
    sx_write(SX_FIFO_RXBASE, 0x00);
    sx_write(SX_LNA, 0x23);           /* gain max, boost LNA */
    sx_write(SX_PACONFIG, 0x8C);      /* PA_BOOST 14 dBm -> ~27 dBm (500 mW) après l'ampli du T30 */
    sx_write(SX_OCP, 0x2B);           /* limite courant 100 mA */
    sx_write(SX_MODEMCFG1, 0x82);     /* BW 250 kHz, CR 4/5, en-tête explicite */
    sx_write(SX_MODEMCFG2, 0x74);     /* SF7, CRC activé : ~11 kbit/s */
    sx_write(SX_MODEMCFG3, 0x04);     /* AGC auto */
    sx_write(SX_PREAMBLE_L, 0x08);
    sx_write(SX_SYNCWORD, 0x12);      /* réseau privé */
    sx_write(SX_OPMODE, MODE_STDBY);
}

#define SX_MODEMSTAT 0x18

static uint16_t rng = 0xACE1;

static uint8_t rand8(void)
{
    rng = rng * 25173 + 13849;
    return rng >> 8;
}

/* vrai si un paquet est en cours de réception (synchro ou en-tête détecté) ;
 * le bit « signal détecté » seul se déclenche sur du bruit, on l'ignore */
static uint8_t sx_channel_busy(void)
{
    return (sx_read(SX_MODEMSTAT) & 0x0A) != 0;
}

static void sx_send(const uint8_t *data, uint8_t len)
{
    uint8_t i;
    uint16_t start;

    sx_write(SX_OPMODE, MODE_STDBY);
    sx_write(SX_FIFO_PTR, 0x00);
    for (i = 0; i < len; i++)
        sx_write(SX_FIFO, data[i]);
    sx_write(SX_PAYLOADLEN, len);
    sx_write(SX_IRQFLAGS, 0xFF);
    rf_switch_tx();
    sx_write(SX_OPMODE, MODE_TX);

    start = millis();
    while (!(sx_read(SX_IRQFLAGS) & IRQ_TXDONE) && (uint16_t)(millis() - start) < 2000)
        ;
    sx_start_rx();
}

static void hw_init(void)
{
    CLK_CKDIVR = 0x00;                /* HSI 16 MHz */
    CLK_PCKENR1 |= 0x04 | 0x10 | 0x20; /* TIM4, SPI1, USART1 */

    /* SPI : NSS, SCK, MOSI en sortie push-pull */
    PB_ODR |= 0x10;
    PB_DDR |= 0x70;
    PB_CR1 |= 0x70;
    PB_CR2 |= 0x70;
    SPI_CR2 = 0x03;                   /* NSS logiciel */
    SPI_CR1 = 0x40 | 0x10 | 0x04;     /* SPE, fmaster/8, maître */

    /* PC3 reset SX, PC4 alim RF, PC5 LED M0 */
    PC_ODR |= 0x18;
    PC_DDR |= 0x38;
    PC_CR1 |= 0x38;

    /* PD0 AUX, PD2/PD3 commutateur antenne */
    PD_ODR &= ~0x0D;
    PD_DDR |= 0x0D;
    PD_CR1 |= 0x0D;

    /* UART1 sur PA2 (TX) / PA3 (RX) */
    SYSCFG_RMPCR1 = (SYSCFG_RMPCR1 & ~0x30) | 0x10;
    PA_ODR |= 0x04;
    PA_DDR |= 0x04;
    PA_CR1 |= 0x0C;
    USART_BRR2 = 0x03;                /* 16 MHz / 9600 = 0x0683 */
    USART_BRR1 = 0x68;
    USART_CR2 = 0x08 | 0x04 | 0x20;   /* TEN, REN, RIEN */

    /* TIM4 : 16 MHz / 128 / 125 = 1 kHz */
    TIM4_PSCR = 7;
    TIM4_ARR = 124;
    TIM4_IER = 0x01;
    TIM4_CR1 = 0x01;

    __asm__("rim");
}

/* ---- Appairage (bind) ---------------------------------------------------- */

#define PC_IDR     REG(0x500B)
#define FLASH_DUKR  REG(0x5053)
#define FLASH_IAPSR REG(0x5054)
#define UNIQUE_ID   ((volatile uint8_t *)0x4926)   /* 12 octets */
#define EE_BASE     ((volatile uint8_t *)0x1000)   /* EEPROM données */
#define EE_MAGIC    0xB1
#define BIND_BLINK_MS 80               /* demi-période du clignotement en mode bind */

static uint16_t net_id;

static void ee_write(uint8_t off, uint8_t v)
{
    if (!(FLASH_IAPSR & 0x08)) {
        FLASH_DUKR = 0xAE;
        FLASH_DUKR = 0x56;
        while (!(FLASH_IAPSR & 0x08))
            ;
    }
    EE_BASE[off] = v;
    while (!(FLASH_IAPSR & 0x04))
        ;
}

static void net_load(void)
{
    if (EE_BASE[0] == EE_MAGIC)
        net_id = ((uint16_t)EE_BASE[1] << 8) | EE_BASE[2];
    else
        net_id = 0;                   /* jamais appairé : réseau par défaut */
}

static void net_save(uint16_t id)
{
    ee_write(1, id >> 8);
    ee_write(2, id & 0xFF);
    ee_write(0, EE_MAGIC);
    FLASH_IAPSR &= ~0x08;             /* reverrouille l'EEPROM */
    net_id = id;
}

static uint8_t m1_is_low(void)
{
    PC_DDR &= ~BTN_M1;                /* PC6 = M1 en entrée pull-up */
    PC_CR1 |= BTN_M1;
    delay_ms(5);
    return !(PC_IDR & BTN_M1);
}

/* Les deux modules s'échangent leur identifiant (dérivé du n° de série du STM8),
 * puis calculent le même identifiant de réseau : idA ^ idB. */
static void bind_mode(void)
{
    uint8_t my[4] = {0, 0, 0, 0};
    uint8_t other[4];
    uint8_t got = 0, peer_got = 0, i;
    uint16_t next_tx = 0, led_t = 0, got_t = 0;
    uint16_t rnd;
    uint8_t buf[7];

    for (i = 0; i < 12; i++)
        my[i & 3] ^= UNIQUE_ID[i];
    rnd = ((uint16_t)my[0] << 8) | my[1];

    uart_puts("BIND : en attente de l'autre module...\r\n");
    sx_start_rx();

    while (!(got && (peer_got || (uint16_t)(millis() - got_t) > 3000))) {
        uint16_t now = millis();

        if ((uint16_t)(now - led_t) >= BIND_BLINK_MS) {  /* clignotement rapide */
            PC_ODR ^= LED_M0;
            led_t = now;
        }

        if ((int16_t)(now - next_tx) >= 0) {
            buf[0] = 'B';
            buf[1] = 'D';
            for (i = 0; i < 4; i++)
                buf[2 + i] = my[i];
            buf[6] = got;
            sx_send(buf, 7);
            rnd = rnd * 25173 + 13849;              /* délai pseudo-aléatoire */
            next_tx = millis() + 150 + (rnd & 0xFF);
        }

        {
            uint8_t irq = sx_read(SX_IRQFLAGS);
            if (irq & IRQ_RXDONE) {
                sx_write(SX_IRQFLAGS, 0xFF);
                if (!(irq & IRQ_CRCERR) && sx_read(SX_RXNB) == 7) {
                    sx_write(SX_FIFO_PTR, sx_read(SX_FIFO_RXCUR));
                    for (i = 0; i < 7; i++)
                        buf[i] = sx_read(SX_FIFO);
                    if (buf[0] == 'B' && buf[1] == 'D' &&
                        (buf[2] != my[0] || buf[3] != my[1] || buf[4] != my[2] || buf[5] != my[3])) {
                        for (i = 0; i < 4; i++)
                            other[i] = buf[2 + i];
                        if (!got)
                            got_t = millis();
                        got = 1;
                        if (buf[6])
                            peer_got = 1;
                    }
                }
            }
        }
    }

    /* un dernier message pour que l'autre sache qu'on l'a reçu */
    buf[0] = 'B'; buf[1] = 'D';
    for (i = 0; i < 4; i++)
        buf[2 + i] = my[i];
    buf[6] = 1;
    sx_send(buf, 7);

    {
        uint16_t id = ((uint16_t)(my[0] ^ other[0] ^ my[2] ^ other[2]) << 8) |
                      (my[1] ^ other[1] ^ my[3] ^ other[3]);
        if (id == 0 || id == 0xFFFF)
            id = 0x1234;
        net_save(id);
    }

    uart_puts("BIND OK - NET ");
    uart_puthex(net_id >> 8);
    uart_puthex(net_id & 0xFF);
    uart_puts(" - redemarrer le module\r\n");

    /* LED fixe jusqu'au prochain redémarrage */
    sx_write(SX_OPMODE, MODE_STDBY);
    PC_ODR |= LED_M0;
    for (;;)
        ;
}

/* Flash d'activité : 40 ms allumée, au moins 40 ms éteinte entre deux flashs,
 * pour que chaque paquet reste visible même quand ils s'enchaînent. */
#define LED_FLASH_MS 40
static uint16_t led_off, led_next;

static void led_pulse(void)
{
    uint16_t now = millis();
    if (!(PC_ODR & LED_M0) && (int16_t)(now - led_next) >= 0) {
        PC_ODR |= LED_M0;
        led_off = now + LED_FLASH_MS;
        led_next = now + 2 * LED_FLASH_MS;
    }
}

/* En-tête de paquet : bit 7 = accusé de réception, bits 0-6 = numéro de séquence */
#define HDR_ACK        0x80
#define HDR_SEQ        0x7F
#define ACK_TIMEOUT_MS 200    /* accusé (~25 ms) + écoute + marge */
#define MAX_RETRIES    8

void main(void)
{
    uint8_t pkt[PKT_MAX + 3];         /* réseau (2) + en-tête (1) + données */
    uint8_t ack[3];
    uint8_t n = 0;
    uint16_t last_rx = 0;
    uint16_t tx_at = 0, ack_deadline = 0;
    uint8_t tx_armed = 0, waiting_ack = 0, retries = 0;
    uint8_t seq, last_rx_seq = 0xFF;
    uint8_t bind;
    uint8_t busy_waits = 0;

    hw_init();
    sx_init();
    net_load();
    bind = m1_is_low();

    /* laisse le temps au PC de réouvrir le port USB-série après un branchement ;
     * en mode bind (M1 à la masse à la mise sous tension), la LED clignote déjà vite */
    {
        uint16_t start = millis();
        uint8_t k = 0;
        while ((uint16_t)(millis() - start) < 5000) {
            if (bind) {
                PC_ODR ^= LED_M0;             /* bind : clignotement rapide */
                delay_ms(BIND_BLINK_MS);
            } else {
                if (k < 8)                    /* démarrage normal : 4 clignotements lents */
                    PC_ODR ^= LED_M0;
                k++;
                delay_ms(400);
            }
        }
        PC_ODR &= ~LED_M0;
    }
    uart_puts("\r\nYolo TMTC lora 868 RX/TX 500mW 9600 - SX127x v0x");
    uart_puthex(sx_version);
    uart_puts(sx_version == 0x12 ? " OK" : " ERREUR");
    uart_puts(" - NET ");
    uart_puthex(net_id >> 8);
    uart_puthex(net_id & 0xFF);
    uart_puts("\r\n");

    if (bind)
        bind_mode();

    {
        uint8_t i;
        for (i = 0; i < 12; i++)                  /* graine aléatoire propre à chaque module */
            rng = (rng << 1) ^ UNIQUE_ID[i] ^ (rng >> 15);
    }

    seq = rand8() & 0x7F;

    sx_start_rx();

    for (;;) {
        uint16_t now = millis();

        /* ---- réception radio ---- */
        {
            uint8_t irq = sx_read(SX_IRQFLAGS);
            if (irq & IRQ_RXDONE) {
                sx_write(SX_IRQFLAGS, 0xFF);
                if (!(irq & IRQ_CRCERR)) {
                    uint8_t len = sx_read(SX_RXNB);
                    uint8_t i, hdr;
                    sx_write(SX_FIFO_PTR, sx_read(SX_FIFO_RXCUR));
                    if (len >= 3 && sx_read(SX_FIFO) == (net_id >> 8) &&
                        sx_read(SX_FIFO) == (net_id & 0xFF)) {
                        hdr = sx_read(SX_FIFO);
                        if (hdr & HDR_ACK) {
                            /* accusé de réception de notre paquet en attente */
                            if (waiting_ack && (hdr & HDR_SEQ) == seq) {
                                waiting_ack = 0;
                                n = 0;
                                seq = (seq + 1) & HDR_SEQ;
                            }
                        } else {
                            /* paquet de données : on le passe à l'UART sauf si c'est
                             * une réémission déjà reçue, et on l'acquitte dans tous les cas */
                            if (hdr != last_rx_seq) {
                                /* pas la place vers l'UART : pas d'accusé, l'autre réémettra */
                                if ((uint16_t)(TX_BUF_SIZE - 1 - ((tx_head - tx_tail) & TX_BUF_MASK)) < len - 3)
                                    goto rx_done;
                                for (i = 3; i < len; i++) {
                                    tx_buf[tx_head] = sx_read(SX_FIFO);
                                    tx_head = (tx_head + 1) & TX_BUF_MASK;
                                }
                                last_rx_seq = hdr;
                                led_pulse();
                            }
                            ack[0] = net_id >> 8;
                            ack[1] = net_id & 0xFF;
                            ack[2] = HDR_ACK | hdr;
                            sx_send(ack, 3);
                        }
                    }
                }
            rx_done:
                sx_write(SX_FIFO_PTR, 0x00);
            }
        }

        /* ---- données reçues vers l'UART, sans bloquer ---- */
        if (tx_head != tx_tail && (USART_SR & 0x80)) {
            USART_DR = tx_buf[tx_tail];
            tx_tail = (tx_tail + 1) & TX_BUF_MASK;
        }

        /* ---- pas d'accusé reçu à temps : on réémet ---- */
        if (waiting_ack && (int16_t)(now - ack_deadline) >= 0) {
            waiting_ack = 0;
            if (++retries > MAX_RETRIES) {
                n = 0;                        /* abandon du paquet */
                seq = (seq + 1) & HDR_SEQ;
            } else {
                tx_at = now + 10 + (rand8() & (retries < 3 ? 63 : 127));
                tx_armed = 1;
            }
        }

        /* ---- UART -> paquet (le paquet en attente d'accusé n'est pas modifié) ---- */
        if (!waiting_ack && !tx_armed) {
            while (rx_count() && n < PKT_MAX) {
                pkt[3 + n++] = rx_buf[rx_tail];
                rx_tail = (rx_tail + 1) & RX_BUF_MASK;
                last_rx = now;
            }
            if (n && (n == PKT_MAX || (uint16_t)(now - last_rx) >= IDLE_GAP_MS)) {
                /* délai aléatoire avant chaque émission : évite que les deux
                 * modules émettent pile en même temps */
                tx_at = now + (rand8() & 31);
                tx_armed = 1;
                retries = 0;
            }
        }

        /* ---- émission (écoute avant d'émettre) ---- */
        if (tx_armed && (int16_t)(now - tx_at) >= 0) {
            if (busy_waits < 6 && sx_channel_busy()) {
                busy_waits++;
                /* l'autre module émet : on attend la fin de son paquet */
                tx_at = now + 20 + (rand8() & 63);
            } else {
                PD_ODR &= ~0x01;
                pkt[0] = net_id >> 8;
                pkt[1] = net_id & 0xFF;
                pkt[2] = seq;
                sx_send(pkt, n + 3);
                tx_armed = 0;
                busy_waits = 0;
                waiting_ack = 1;
                ack_deadline = millis() + ACK_TIMEOUT_MS;
                led_pulse();
            }
        }

        /* AUX haut = prêt ; bas si le tampon UART est presque plein */
        if (rx_count() >= RX_BUF_HIGH)
            PD_ODR &= ~0x01;
        else
            PD_ODR |= 0x01;

        if ((PC_ODR & LED_M0) && (int16_t)(millis() - led_off) >= 0)
            PC_ODR &= ~LED_M0;
    }
}
