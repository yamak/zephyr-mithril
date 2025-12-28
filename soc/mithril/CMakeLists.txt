
zephyr_sources(
   reset.S
   boot_args.c
)
zephyr_include_directories(.)

set(SOC_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/linker.ld CACHE INTERNAL "")

# PAC return address protection (controlled by Kconfig)
if(CONFIG_RISCV_XPAC_RET)
    zephyr_compile_options(-mxpac-ret)
endif()
