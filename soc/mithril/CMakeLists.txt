
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

# Landing Pad for forward-edge CFI (controlled by Kconfig)
if(CONFIG_RISCV_LPAD)
    zephyr_compile_options(-menable-experimental-extensions)
endif()
