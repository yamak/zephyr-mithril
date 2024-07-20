
zephyr_sources(
   vector_table.S
)
zephyr_include_directories(.)
zephyr_linker_sources(ROM_START SORT_KEY 000vector_table vector.ld)
set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/linker.ld CACHE INTERNAL "")
#set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/riscv/common/linker.ld CACHE INTERNAL "")
