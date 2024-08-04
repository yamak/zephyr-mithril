/*
 * Copyright (c) 2016 Jean-Paul Etienne <fractalclone@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Full C support initialization
 *
 *
 * Initialization of full C support: zero the .bss and call z_cstart().
 *
 * Stack is available in this module, but not the global data/bss until their
 * initialization is performed.
 */

#include <stddef.h>
#include <zephyr/toolchain.h>
#include <zephyr/kernel_structs.h>
#include <kernel_internal.h>

#if defined(CONFIG_RISCV_SOC_INTERRUPT_INIT)
void soc_interrupt_init(void);
#endif
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(PREP, CONFIG_LOG_DEFAULT_LEVEL);
#include "mithril_mfp.h"
void uart_out(char c) {
    while ((UART0->sr)&0x02);

    UART0->txr = c;
}

int m_putchar(int c) {
#ifdef ENABLE_SIM
    SIM_CTRL->out = (unsigned char)c;
#else
    if((unsigned char)c == '\n')
        uart_out('\r');
    uart_out((unsigned char)c);
#endif
    return c;
}

int m_puts(const char *str) {
    while (*str) {
        m_putchar(*str++);
    }

    return 0;
}
void DecimalToHex(uint8_t decimal, uint8_t *hex_string)
{
    uint8_t high_nibble = (decimal >> 4) & 0xF;
    uint8_t low_nibble = decimal & 0xF;

    hex_string[0] = (high_nibble < 10) ? high_nibble + '0' : high_nibble - 10 + 'A';
    hex_string[1] = (low_nibble < 10) ? low_nibble + '0' : low_nibble - 10 + 'A';
}

/**
 * Dumps the buffer content to a character array in hexadecimal format.
 *
 * @param buf pointer to the buffer to be dumped
 * @param len length of the buffer
 * @param output pointer to the character array for the hexadecimal output
 *
 * @return the length of the hexadecimal output
 *
 */
int DumpBuf(const unsigned char *buf, int len, uint8_t *output)
{
    int count=0;
    int total_len=0;
    while(count<len) {
        for(int i=0;i<64;i++) {
            DecimalToHex(buf[count], &output[total_len]);
            total_len+=2;
            count++;
            if(count == len)
                break;

        }
        output[total_len] = '\n';
        total_len++;
    }
    output[total_len] = 0;
    return total_len;
}
void calculate_digest() {
    uint8_t DumpData[1024];
    mfp_write_ctrl(MFP_CR_INIT);
    mfp_write_scan_mem_range((uint32_t)0x1000000, 0x100ba57);
    mfp_write_ctrl(MFP_CR_START | MFP_CR_MEM_ACCESS);
    mfp_wait_ready();
    mfp_write_ctrl(MFP_CR_FINALIZE);
    mfp_wait_digest();
    DumpBuf((unsigned char*)MFP->digest,32,DumpData);
    m_puts((const char*)DumpData);
}
/**
 *
 * @brief Prepare to and run C code
 *
 * This routine prepares for the execution of and runs C code.
 */

void z_prep_c(void)
{
 //   calculate_digest();
	z_bss_zero();
	z_data_copy();
#if defined(CONFIG_RISCV_SOC_INTERRUPT_INIT)
	soc_interrupt_init();
#endif
	z_cstart();
	CODE_UNREACHABLE;
}
