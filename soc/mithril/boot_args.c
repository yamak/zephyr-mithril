/*
 * Copyright (c) 2025 Yusuf Yamak <yamakyusuf@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>

#ifdef CONFIG_BOOT_ARGS_SIZE
// Since BSS initialization is called after boot_args_get function,
// we initialize it to prevent it from being placed in BSS segment
char boot_args[CONFIG_BOOT_ARGS_SIZE] __attribute__((aligned(4))) = {0xFF}; 

/**
 * @brief Copies boot arguments from a source address to the boot_args array
 * Boot arguments's address is passed via x10 register, and size is passed via x11 register. 
 * @param boot_args_addr Pointer to source boot arguments
 * @param boot_args_size Size of boot arguments
 * 
 * This function copies boot_args_size bytes of data from the source address
 * boot_args_addr into the global boot_args array. The data is copied as 32-bit
 * words.
 */
void _boot_args_get(char* boot_args_addr, uint32_t boot_args_size)
{
    uint32_t size = boot_args_size > CONFIG_BOOT_ARGS_SIZE ? CONFIG_BOOT_ARGS_SIZE : boot_args_size;

    for (int i = 0; i < size; i++)
    {
        boot_args[i] = boot_args_addr[i];
    }
}
#else
/**
 * @brief Empty implementation of boot_args_get function
 * 
 * This function does nothing and is provided to prevent compilation errors
 * when CONFIG_BOOT_ARGS_SIZE is not defined.
 */
void _boot_args_get(char* boot_args_addr, uint32_t boot_args_size)
{

}
#endif