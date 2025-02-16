
/*
 * Copyright (c) 2025 Yusuf Yamak <yamakyusuf@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Vector table must be placed at the beginning of ROM memory.
 * RISC-V hardware expects to find the vector table (containing reset vector
 * and interrupt handlers) at address 0x0 after reset. This is achieved by:
 * 1. Using KEEP directive to prevent linker from optimizing out the section
 * 2. Placing it first in ROM using SORT_KEY 000vector_table in CMakeLists.txt
 */
KEEP(*(.vector_table.*))
KEEP(*(.reset_handler.*))