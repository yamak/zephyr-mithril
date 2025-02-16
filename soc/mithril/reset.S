/*
 * Copyright (c) 2025 Yusuf Yamak <yamakyusuf@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>

/* imports */
GTEXT(__start)
GTEXT(_boot_args_get)
GTEXT(_isr_wrapper)
GTEXT(_irq_vector_table)

SECTION_FUNC(reset_handler, _reset_handler)
jal x1, _boot_args_get
j __start

SECTION_FUNC(vector_table, _irq_vector_table)
.option norvc;
  .rept 32
  j _isr_wrapper
  .endr
  // reset vector
  j _reset_handler
