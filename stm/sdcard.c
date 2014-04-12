/*-----------------------------------------------------------------------*/
/* MMC/SDSC/SDHC (in SPI mode) control module for STM32 Version 1.1.6    */
/* (C) Martin Thomas, 2010 - based on the AVR MMC module (C)ChaN, 2007   */
/*-----------------------------------------------------------------------*/

/* Copyright (c) 2010, Martin Thomas, ChaN
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in
 the documentation and/or other materials provided with the
 distribution.
 * Neither the name of the copyright holders nor the names of
 contributors may be used to endorse or promote products derived
 from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE. */

#include <stdbool.h>
#include <stm32f4xx.h>
#include <stm32f4xx_rcc.h>
#include <stm32f4xx_spi.h>
#include <stm32f4xx_gpio.h>
#include "ffconf.h"
#include "diskio.h"
#include "sdcard.h"

typedef bool BOOL;

#ifndef FALSE
#define FALSE false
#endif
#ifndef TRUE
#define TRUE true
#endif

#ifdef STM32_SD_USE_DMA
// #warning "Information only: using DMA"
#pragma message "*** Using DMA ***"
#endif

/* set to 1 to provide a disk_ioctrl */
#define STM32_SD_DISK_IOCTRL   0

#define CARD_SUPPLY_SWITCHABLE  1
#define SOCKET_WP_CONNECTED     0 /* write-protect socket-switch */
#define SOCKET_CP_CONNECTED     1 /* card-present socket-switch */
#define DMA_Channel_SPIx_RX     DMA1_Channel4
#define DMA_Channel_SPIx_TX     DMA1_Channel5
#define DMA_FLAG_SPIx_TC_RX     DMA1_FLAG_TC4
#define DMA_FLAG_SPIx_TC_TX     DMA1_FLAG_TC5

#define GPIO_SPI_AF         GPIO_AF_SPI2

#define GPIO_PIN_EN         GPIO_Pin_13
#define GPIO_PIN_CP         GPIO_Pin_0
#define GPIO_PIN_CS         GPIO_Pin_1
#define GPIO_PIN_SCLK       GPIO_Pin_10
#define GPIO_PIN_MISO       GPIO_Pin_2
#define GPIO_PIN_MOSI       GPIO_Pin_3

#define GPIO_SOURCE_MISO    GPIO_PinSource2
#define GPIO_SOURCE_MOSI    GPIO_PinSource3
#define GPIO_SOURCE_SCLK    GPIO_PinSource10

#define GPIO_PORT_EN        GPIOC
#define GPIO_PORT_CP        GPIOC
#define GPIO_PORT_CS        GPIOC
#define GPIO_PORT_SCLK      GPIOB
#define GPIO_PORT_MISO      GPIOC
#define GPIO_PORT_MOSI      GPIOC

#define GPIO_CLK_EN         RCC_AHB1Periph_GPIOC
#define GPIO_CLK_CP         RCC_AHB1Periph_GPIOC
#define GPIO_CLK_CS         RCC_AHB1Periph_GPIOC
#define GPIO_CLK_SCLK       RCC_AHB1Periph_GPIOB
#define GPIO_CLK_MISO       RCC_AHB1Periph_GPIOC
#define GPIO_CLK_MOSI       RCC_AHB1Periph_GPIOC

#define SPIx                SPI2
#define SPIx_CLK            RCC_APB1Periph_SPI2
#define SPIx_CLK_CMD        RCC_APB1PeriphClockCmd
#define GPIO_CLK_CMD        RCC_AHB1PeriphClockCmd

#define SPI_BaudRatePrescaler_SPIx  SPI_BaudRatePrescaler_4

/* Definitions for MMC/SDC command */
#define CMD0    (0x40+0)    /* GO_IDLE_STATE */
#define CMD1    (0x40+1)    /* SEND_OP_COND (MMC) */
#define ACMD41  (0xC0+41)   /* SEND_OP_COND (SDC) */
#define CMD8    (0x40+8)    /* SEND_IF_COND */
#define CMD9    (0x40+9)    /* SEND_CSD */
#define CMD10   (0x40+10)   /* SEND_CID */
#define CMD12   (0x40+12)   /* STOP_TRANSMISSION */
#define ACMD13  (0xC0+13)   /* SD_STATUS (SDC) */
#define CMD16   (0x40+16)   /* SET_BLOCKLEN */
#define CMD17   (0x40+17)   /* READ_SINGLE_BLOCK */
#define CMD18   (0x40+18)   /* READ_MULTIPLE_BLOCK */
#define CMD23   (0x40+23)   /* SET_BLOCK_COUNT (MMC) */
#define ACMD23  (0xC0+23)   /* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24   (0x40+24)   /* WRITE_BLOCK */
#define CMD25   (0x40+25)   /* WRITE_MULTIPLE_BLOCK */
#define CMD55   (0x40+55)   /* APP_CMD */
#define CMD58   (0x40+58)   /* READ_OCR */

/* Card-Select Controls  (Platform dependent) */
#define SELECT()        GPIO_ResetBits(GPIO_PORT_CS, GPIO_PIN_CS)    /* MMC CS = L */
#define DESELECT()      GPIO_SetBits(GPIO_PORT_CS, GPIO_PIN_CS)      /* MMC CS = H */
#define SD_TIMEOUT      (100000)

/*--------------------------------------------------------------------------

  Module Private Functions and Variables

  ---------------------------------------------------------------------------*/
static const DWORD socket_state_mask_cp = (1 << 0);
static const DWORD socket_state_mask_wp = (1 << 1);

static volatile DSTATUS Stat = STA_NOINIT;    /* Disk status */
static BYTE CardType;            /* Card type flags */

enum speed_setting { INTERFACE_SLOW, INTERFACE_FAST };

static void interface_speed( enum speed_setting speed )
{
    DWORD tmp;

    tmp = SPIx->CR1;
    if ( speed == INTERFACE_SLOW ) {
        /* Set slow clock (100k-400k) */
        tmp = ( tmp | SPI_BaudRatePrescaler_256 );
    } else {
        /* Set fast clock (depends on the CSD) */
        tmp = ( tmp & ~SPI_BaudRatePrescaler_256 ) | SPI_BaudRatePrescaler_SPIx;
    }
    SPIx->CR1 = tmp;
}

bool sdcard_is_present(void)
{
    return GPIO_ReadInputDataBit(GPIO_PORT_CP, GPIO_PIN_CP);
}

static void sdcard_power(BOOL on)        /* switch FET for card-socket VCC */
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(GPIO_CLK_EN, ENABLE);

    /* Configure I/O for Power FET */
    GPIO_InitStructure.GPIO_Pin   = GPIO_PIN_EN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIO_PORT_EN, &GPIO_InitStructure);

    if (on) {
        GPIO_SetBits(GPIO_PORT_EN, GPIO_PIN_EN);
    } else {
        GPIO_ResetBits(GPIO_PORT_EN, GPIO_PIN_EN);
    }
}

#if (STM32_SD_DISK_IOCTRL == 1)
static int chk_power(void)        /* Socket power state: 0=off, 1=on */
{
    return GPIO_ReadOutputDataBit(GPIO_PORT_EN, GPIO_PIN_EN);
}
#endif

/*-----------------------------------------------------------------------*/
/* Transmit/Receive a byte to MMC via SPI  (Platform dependent)          */
/*-----------------------------------------------------------------------*/
static BYTE stm32_spi_rw( BYTE out )
{
    /* Loop while DR register in not empty */
    /// not needed: while (SPI_I2S_GetFlagStatus(SPIx, SPI_I2S_FLAG_TXE) == RESET) { ; }

    /* Send byte through the SPI peripheral */
    SPI_I2S_SendData(SPIx, out);

    /* Wait to receive a byte */
    while (SPI_I2S_GetFlagStatus(SPIx, SPI_I2S_FLAG_RXNE) == RESET) { ; }

    /* Return the byte read from the SPI bus */
    return SPI_I2S_ReceiveData(SPIx);
}

/*-----------------------------------------------------------------------*/
/* Transmit a byte to MMC via SPI  (Platform dependent)                  */
/*-----------------------------------------------------------------------*/

#define xmit_spi(dat)  stm32_spi_rw(dat)

/*-----------------------------------------------------------------------*/
/* Receive a byte from MMC via SPI  (Platform dependent)                 */
/*-----------------------------------------------------------------------*/

static BYTE rcvr_spi (void)
{
    return stm32_spi_rw(0xff);
}

/* Alternative macro to receive data fast */
#define rcvr_spi_m(dst)  *(dst)=stm32_spi_rw(0xff)

/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/
static BYTE wait_ready (void)
{
    BYTE res;
    volatile int timeout = SD_TIMEOUT;

    rcvr_spi();
    do
        res = rcvr_spi();
    while ((res != 0xFF) && timeout--);

    return res;
}

/*-----------------------------------------------------------------------*/
/* Deselect the card and release SPI bus                                 */
/*-----------------------------------------------------------------------*/
static void release_spi (void)
{
    DESELECT();
    rcvr_spi();
}

/*-----------------------------------------------------------------------*/
/* Power Control and interface-initialization (Platform dependent)       */
/*-----------------------------------------------------------------------*/
static void sdcard_hw_init(void)
{
    SPI_InitTypeDef  SPI_InitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;

    /* Enable SPI clock */
    SPIx_CLK_CMD(SPIx_CLK, ENABLE);

    /* Enable GPIO clocks */
    GPIO_CLK_CMD(GPIO_CLK_CP | GPIO_CLK_CS |
            GPIO_CLK_SCLK | GPIO_CLK_MISO | GPIO_CLK_MOSI, ENABLE);

    // TODO
    sdcard_power(1);

    sys_tick_delay_ms(250); /* Wait for 250ms */

    /* Configure I/O for Chip select */
    GPIO_InitStructure.GPIO_Pin   = GPIO_PIN_CS;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIO_PORT_CS, &GPIO_InitStructure);

    /* De-select the Card: Chip Select high */
    DESELECT();

    /* Configure I/O for Chip detect */
    GPIO_InitStructure.GPIO_Pin   = GPIO_PIN_CP;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIO_PORT_CP, &GPIO_InitStructure);

    /* Connect SPI pins to AF */
    GPIO_PinAFConfig(GPIO_PORT_SCLK, GPIO_SOURCE_SCLK, GPIO_SPI_AF);
    GPIO_PinAFConfig(GPIO_PORT_MISO, GPIO_SOURCE_MISO, GPIO_SPI_AF);
    GPIO_PinAFConfig(GPIO_PORT_MOSI, GPIO_SOURCE_MOSI, GPIO_SPI_AF);

    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    /* SPI  MISO pin configuration */
    GPIO_InitStructure.GPIO_Pin =  GPIO_PIN_MISO;
    GPIO_Init(GPIO_PORT_MISO, &GPIO_InitStructure);

    /* SPI  MOSI pin configuration */
    GPIO_InitStructure.GPIO_Pin =  GPIO_PIN_MOSI;
    GPIO_Init(GPIO_PORT_MOSI, &GPIO_InitStructure);

    /* SPI SCK pin configuration */
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;
    GPIO_InitStructure.GPIO_Pin = GPIO_PIN_SCLK;
    GPIO_Init(GPIO_PORT_SCLK, &GPIO_InitStructure);

    /* SPI configuration */
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_SPIx; // 72000kHz/256=281kHz < 400kHz
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;

    SPI_Init(SPIx, &SPI_InitStructure);
    SPI_CalculateCRC(SPIx, DISABLE);
    SPI_Cmd(SPIx, ENABLE);

    /* drain SPI */
    while (SPI_I2S_GetFlagStatus(SPIx, SPI_I2S_FLAG_TXE) == RESET) { ; }

    /* dummy read */
    SPI_I2S_ReceiveData(SPIx);

#ifdef STM32_SD_USE_DMA
    /* enable DMA clock */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
#endif
}

static void power_off (void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    if (!(Stat & STA_NOINIT)) {
        SELECT();
        wait_ready();
        release_spi();
    }

    SPI_I2S_DeInit(SPIx);
    SPI_Cmd(SPIx, DISABLE);
    SPIx_CLK_CMD(SPIx_CLK, DISABLE);

    /* All SPI-Pins to input with weak internal pull-downs */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;

    /* SPI SCK pin configuration */
    GPIO_InitStructure.GPIO_Pin = GPIO_PIN_SCLK;
    GPIO_Init(GPIO_PORT_SCLK, &GPIO_InitStructure);

    /* SPI  MISO pin configuration */
    GPIO_InitStructure.GPIO_Pin =  GPIO_PIN_MISO;
    GPIO_Init(GPIO_PORT_MISO, &GPIO_InitStructure);

    /* SPI  MOSI pin configuration */
    GPIO_InitStructure.GPIO_Pin =  GPIO_PIN_MOSI;
    GPIO_Init(GPIO_PORT_MOSI, &GPIO_InitStructure);

    sdcard_power(0);

    Stat |= STA_NOINIT;        /* Set STA_NOINIT */
}


/*-----------------------------------------------------------------------*/
/* Receive a data packet from MMC                                        */
/*-----------------------------------------------------------------------*/

static BOOL rcvr_datablock (
        BYTE *buff,            /* Data buffer to store received data */
        UINT btr            /* Byte count (must be multiple of 4) */
        )
{
    BYTE token;
    volatile int timeout = SD_TIMEOUT;
    do {                            /* Wait for data packet in timeout of 100ms */
        token = rcvr_spi();
    } while ((token == 0xFF) && timeout--);
    if(token != 0xFE) return FALSE;    /* If not valid data token, return with error */

#ifdef STM32_SD_USE_DMA
    stm32_dma_transfer( TRUE, buff, btr );
#else
    do {                            /* Receive the data block into buffer */
        rcvr_spi_m(buff++);
        rcvr_spi_m(buff++);
        rcvr_spi_m(buff++);
        rcvr_spi_m(buff++);
    } while (btr -= 4);
#endif /* STM32_SD_USE_DMA */

    rcvr_spi();                        /* Discard CRC */
    rcvr_spi();

    return TRUE;                    /* Return with success */
}



/*-----------------------------------------------------------------------*/
/* Send a data packet to MMC                                             */
/*-----------------------------------------------------------------------*/

#if _FS_READONLY == 0
    static
BOOL xmit_datablock (
        const BYTE *buff,    /* 512 byte data block to be transmitted */
        BYTE token            /* Data/Stop token */
        )
{
    BYTE resp;
#ifndef STM32_SD_USE_DMA
    BYTE wc;
#endif

    if (wait_ready() != 0xFF) return FALSE;

    xmit_spi(token);                    /* transmit data token */
    if (token != 0xFD) {    /* Is data token */

#ifdef STM32_SD_USE_DMA
        stm32_dma_transfer( FALSE, buff, 512 );
#else
        wc = 0;
        do {                            /* transmit the 512 byte data block to MMC */
            xmit_spi(*buff++);
            xmit_spi(*buff++);
        } while (--wc);
#endif /* STM32_SD_USE_DMA */

        xmit_spi(0xFF);                    /* CRC (Dummy) */
        xmit_spi(0xFF);
        resp = rcvr_spi();                /* Receive data response */
        if ((resp & 0x1F) != 0x05)        /* If not accepted, return with error */
            return FALSE;
    }

    return TRUE;
}
#endif /* _READONLY */

static BYTE send_cmd (
        BYTE cmd,        /* Command byte */
        DWORD arg        /* Argument */
        )
{
    BYTE n, res;


    if (cmd & 0x80) {    /* ACMD<n> is the command sequence of CMD55-CMD<n> */
        cmd &= 0x7F;
        res = send_cmd(CMD55, 0);
        if (res > 1) return res;
    }

    /* Select the card and wait for ready */
    DESELECT();
    SELECT();
    if (wait_ready() != 0xFF) {
        return 0xFF;
    }

    /* Send command packet */
    xmit_spi(cmd);                        /* Start + Command index */
    xmit_spi((BYTE)(arg >> 24));        /* Argument[31..24] */
    xmit_spi((BYTE)(arg >> 16));        /* Argument[23..16] */
    xmit_spi((BYTE)(arg >> 8));            /* Argument[15..8] */
    xmit_spi((BYTE)arg);                /* Argument[7..0] */
    n = 0x01;                            /* Dummy CRC + Stop */
    if (cmd == CMD0) n = 0x95;            /* Valid CRC for CMD0(0) */
    if (cmd == CMD8) n = 0x87;            /* Valid CRC for CMD8(0x1AA) */
    xmit_spi(n);

    /* Receive command response */
    if (cmd == CMD12) rcvr_spi();        /* Skip a stuff byte when stop reading */

    n = 10;                                /* Wait for a valid response in timeout of 10 attempts */
    do
        res = rcvr_spi();
    while ((res & 0x80) && --n);

    return res;            /* Return with the response value */
}

bool sdcard_init()
{
    volatile int timeout = SD_TIMEOUT;
    BYTE n, cmd, ty, ocr[4];

    sdcard_hw_init();

    for (n = 10; n; n--) {
        /* 80 dummy clocks */
        rcvr_spi();
    }

    ty = 0;
    if (send_cmd(CMD0, 0) == 1) {            /* Enter Idle state */
        if (send_cmd(CMD8, 0x1AA) == 1) {    /* SDHC */
            for (n = 0; n < 4; n++) {
                ocr[n] = rcvr_spi();            /* Get trailing return value of R7 response */
            }
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {                 /* The card can work at VDD range of 2.7-3.6V */
                while (--timeout && send_cmd(ACMD41, 1UL << 30));   /* Wait for leaving idle state (ACMD41 with HCS bit) */
                if (timeout && send_cmd(CMD58, 0) == 0) {           /* Check CCS bit in the OCR */
                    for (n = 0; n < 4; n++) {
                        ocr[n] = rcvr_spi();
                    }
                    ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;
                }
            }
        } else {                            /* SDSC or MMC */
            if (send_cmd(ACMD41, 0) <= 1) {
                ty = CT_SD1; cmd = ACMD41;    /* SDSC */
            } else {
                ty = CT_MMC; cmd = CMD1;    /* MMC */
            }
            while (--timeout && send_cmd(cmd, 0));      /* Wait for leaving idle state */
            if (!timeout || send_cmd(CMD16, 512) != 0)  /* Set R/W block length to 512 */
                ty = 0;
        }
    }

    CardType = ty;
    release_spi();

    if (ty) {            /* Initialization succeeded */
        Stat &= ~STA_NOINIT;        /* Clear STA_NOINIT */
        interface_speed(INTERFACE_FAST);
        return true;
    } else {            /* Initialization failed */
        power_off();
        return false;
    }
}

bool sdcard_read(uint8_t *buff, uint32_t sector, uint32_t count)
{
    if (Stat & STA_NOINIT) {
        return false;
    }

    if (!(CardType & CT_BLOCK)) {
        sector *= 512;    /* Convert to byte address if needed */
    }

    if (count == 1) {    /* Single block read */
        if (send_cmd(CMD17, sector) == 0)    { /* READ_SINGLE_BLOCK */
            if (rcvr_datablock(buff, 512)) {
                count = 0;
            }
        }
    } else {            /* Multiple block read */
        if (send_cmd(CMD18, sector) == 0) {    /* READ_MULTIPLE_BLOCK */
            do {
                if (!rcvr_datablock(buff, 512)) {
                    break;
                }
                buff += 512;
            } while (--count);
            send_cmd(CMD12, 0);                /* STOP_TRANSMISSION */
        }
    }
    release_spi();

    return count ? false: true;
}

bool sdcard_write(const uint8_t *buff, uint32_t sector, uint32_t count)
{
    if (Stat & STA_NOINIT) {
        return false;
    }

    if (!(CardType & CT_BLOCK)) {
        sector *= 512;    /* Convert to byte address if needed */
    }

    if (count == 1) {    /* Single block write */
        if ((send_cmd(CMD24, sector) == 0)    /* WRITE_BLOCK */
                && xmit_datablock(buff, 0xFE))
            count = 0;
    } else {            /* Multiple block write */
        if (CardType & CT_SDC) send_cmd(ACMD23, count);
        if (send_cmd(CMD25, sector) == 0) {    /* WRITE_MULTIPLE_BLOCK */
            do {
                if (!xmit_datablock(buff, 0xFC)) break;
                buff += 512;
            } while (--count);
            if (!xmit_datablock(0, 0xFD))    /* STOP_TRAN token */
                count = 1;
        }
    }
    release_spi();

    return count ? false: true;
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

#if (STM32_SD_DISK_IOCTRL == 1)
DRESULT disk_ioctl (
        BYTE drv,        /* Physical drive number (0) */
        BYTE ctrl,        /* Control code */
        void *buff        /* Buffer to send/receive control data */
        )
{
    DRESULT res;
    BYTE n, csd[16], *ptr = buff;
    WORD csize;

    if (drv) return RES_PARERR;

    res = RES_ERROR;

    if (ctrl == CTRL_POWER) {
        switch (*ptr) {
            case 0:        /* Sub control code == 0 (POWER_OFF) */
                if (chk_power())
                    power_off();        /* Power off */
                res = RES_OK;
                break;
            case 1:        /* Sub control code == 1 (POWER_ON) */
                power_on();                /* Power on */
                res = RES_OK;
                break;
            case 2:        /* Sub control code == 2 (POWER_GET) */
                *(ptr+1) = (BYTE)chk_power();
                res = RES_OK;
                break;
            default :
                res = RES_PARERR;
        }
    }
    else {
        if (Stat & STA_NOINIT) return RES_NOTRDY;

        switch (ctrl) {
            case CTRL_SYNC :        /* Make sure that no pending write process */
                SELECT();
                if (wait_ready() == 0xFF)
                    res = RES_OK;
                break;

            case GET_SECTOR_COUNT :    /* Get number of sectors on the disk (DWORD) */
                if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
                    if ((csd[0] >> 6) == 1) {    /* SDC version 2.00 */
                        csize = csd[9] + ((WORD)csd[8] << 8) + 1;
                        *(DWORD*)buff = (DWORD)csize << 10;
                    } else {                    /* SDC version 1.XX or MMC*/
                        n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
                        csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
                        *(DWORD*)buff = (DWORD)csize << (n - 9);
                    }
                    res = RES_OK;
                }
                break;

            case GET_SECTOR_SIZE :    /* Get R/W sector size (WORD) */
                *(WORD*)buff = 512;
                res = RES_OK;
                break;

            case GET_BLOCK_SIZE :    /* Get erase block size in unit of sector (DWORD) */
                if (CardType & CT_SD2) {    /* SDC version 2.00 */
                    if (send_cmd(ACMD13, 0) == 0) {    /* Read SD status */
                        rcvr_spi();
                        if (rcvr_datablock(csd, 16)) {                /* Read partial block */
                            for (n = 64 - 16; n; n--) rcvr_spi();    /* Purge trailing data */
                            *(DWORD*)buff = 16UL << (csd[10] >> 4);
                            res = RES_OK;
                        }
                    }
                } else {                    /* SDC version 1.XX or MMC */
                    if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {    /* Read CSD */
                        if (CardType & CT_SD1) {    /* SDC version 1.XX */
                            *(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
                        } else {                    /* MMC */
                            *(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
                        }
                        res = RES_OK;
                    }
                }
                break;

            case MMC_GET_TYPE :        /* Get card type flags (1 byte) */
                *ptr = CardType;
                res = RES_OK;
                break;

            case MMC_GET_CSD :        /* Receive CSD as a data block (16 bytes) */
                if (send_cmd(CMD9, 0) == 0        /* READ_CSD */
                        && rcvr_datablock(ptr, 16))
                    res = RES_OK;
                break;

            case MMC_GET_CID :        /* Receive CID as a data block (16 bytes) */
                if (send_cmd(CMD10, 0) == 0        /* READ_CID */
                        && rcvr_datablock(ptr, 16))
                    res = RES_OK;
                break;

            case MMC_GET_OCR :        /* Receive OCR as an R3 resp (4 bytes) */
                if (send_cmd(CMD58, 0) == 0) {    /* READ_OCR */
                    for (n = 4; n; n--) *ptr++ = rcvr_spi();
                    res = RES_OK;
                }
                break;

            case MMC_GET_SDSTAT :    /* Receive SD status as a data block (64 bytes) */
                if (send_cmd(ACMD13, 0) == 0) {    /* SD_STATUS */
                    rcvr_spi();
                    if (rcvr_datablock(ptr, 64))
                        res = RES_OK;
                }
                break;

            default:
                res = RES_PARERR;
        }

        release_spi();
    }

    return res;
}
#endif /* _USE_IOCTL != 0 */
