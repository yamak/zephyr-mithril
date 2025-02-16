
zephyr_sources(
   reset.S
   boot_args.c
)
zephyr_include_directories(.)
# Place vector table at the start of ROM using low sort key (000).
# This ensures the vector table is at address 0x0 where RISC-V CPU
# expects to find it after reset.
zephyr_linker_sources(ROM_START SORT_KEY 000reset reset.ld)
set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/linker.ld CACHE INTERNAL "")
#set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/riscv/common/linker.ld CACHE INTERNAL "")
