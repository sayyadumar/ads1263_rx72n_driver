cmake_minimum_required(VERSION 3.20)

# ─── Project ─────────────────────────────────────────────────────────────────
# Languages: C, CXX, ASM (for smc_gen/r_bsp/mcu/all/reset_program.S)
project(ads1263_rx72n
    LANGUAGES C CXX ASM
    VERSION   1.0.0
)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

# ─── Source files ────────────────────────────────────────────────────────────

# Application
set(APP_SOURCES
    main.cpp
    ads1263.cpp
    log.c
    bsp_cxx_init.c      # _CALL_INIT shim: GNURX C++ constructor init (BSP_CFG_CPLUSPLUS=1)
)

# BSP: core startup and runtime
set(BSP_SOURCES
    # Startup (assembler + C)
    smc_gen/r_bsp/mcu/all/reset_program.S
    smc_gen/r_bsp/mcu/all/resetprg.c
    smc_gen/r_bsp/mcu/all/r_bsp_mcu_startup.c

    # MCU-level BSP
    smc_gen/r_bsp/mcu/all/dbsct.c
    smc_gen/r_bsp/mcu/all/mcu_locks.c
    smc_gen/r_bsp/mcu/all/r_bsp_common.c
    smc_gen/r_bsp/mcu/all/r_bsp_cpu.c
    smc_gen/r_bsp/mcu/all/r_bsp_interrupts.c
    smc_gen/r_bsp/mcu/all/r_bsp_locking.c
    smc_gen/r_bsp/mcu/all/r_bsp_software_interrupt.c
    smc_gen/r_bsp/mcu/all/r_rx_intrinsic_functions.c
    smc_gen/r_bsp/mcu/all/sbrk.c
    smc_gen/r_bsp/mcu/all/lowlvl.c
    smc_gen/r_bsp/mcu/all/lowsrc.c

    # RX72M-specific BSP
    smc_gen/r_bsp/mcu/rx72m/mcu_clocks.c
    smc_gen/r_bsp/mcu/rx72m/mcu_init.c
    smc_gen/r_bsp/mcu/rx72m/mcu_interrupts.c
    smc_gen/r_bsp/mcu/rx72m/mcu_mapped_interrupts.c
    smc_gen/r_bsp/mcu/rx72m/vecttbl.c

    # Board-level BSP
    smc_gen/r_bsp/board/generic_rx72m/hwsetup.c

    # Smart Configurator generated
    smc_gen/general/r_cg_hardware_setup.c
    smc_gen/general/r_smc_cgc.c
    smc_gen/general/r_smc_cgc_user.c
    smc_gen/general/r_smc_interrupt.c
)

# SCI FIT module
set(SCI_SOURCES
    smc_gen/r_byteq/src/r_byteq.c
    smc_gen/r_pincfg/Pin.c
    smc_gen/r_pincfg/r_sci_rx_pinset.c
    smc_gen/r_sci_rx/src/r_sci_rx.c
    smc_gen/r_sci_rx/src/r_sci_rx_dmaca.c   # compiled-out by SCI_CFG_DMACA_ENABLE=0
    smc_gen/r_sci_rx/src/r_sci_rx_dtc.c     # compiled-out by SCI_CFG_DTC_ENABLE=0
    smc_gen/r_sci_rx/src/targets/rx72m/r_sci_rx72m.c
    smc_gen/r_sci_rx/src/targets/rx72m/r_sci_rx72m_data.c
)

# ─── Executable ──────────────────────────────────────────────────────────────
add_executable(ads1263_app
    ${APP_SOURCES}
    ${BSP_SOURCES}
    ${SCI_SOURCES}
)

# ─── Include paths ───────────────────────────────────────────────────────────
target_include_directories(ads1263_app PRIVATE
    # Project root — ads1263.hpp, log.h
    ${PROJECT_SOURCE_DIR}

    # BSP top-level (platform.h entry point)
    ${PROJECT_SOURCE_DIR}/smc_gen/r_bsp

    # Active board header (generic_rx72m/r_bsp.h)
    ${PROJECT_SOURCE_DIR}/smc_gen/r_bsp/board/generic_rx72m

    # MCU-specific headers (mcu_info.h, vecttbl.h, iodefine.h, …)
    ${PROJECT_SOURCE_DIR}/smc_gen/r_bsp/mcu/rx72m
    ${PROJECT_SOURCE_DIR}/smc_gen/r_bsp/mcu/rx72m/register_access/gnuc
    ${PROJECT_SOURCE_DIR}/smc_gen/r_bsp/mcu/all

    # User-overridden BSP/FIT config files (r_bsp_config.h, r_sci_rx_config.h)
    ${PROJECT_SOURCE_DIR}/smc_gen/r_config

    # Smart Configurator generated (r_cg_macrodriver.h, r_cg_userdefine.h)
    ${PROJECT_SOURCE_DIR}/smc_gen/general

    # SCI FIT module public and private headers
    ${PROJECT_SOURCE_DIR}/smc_gen/r_sci_rx
    ${PROJECT_SOURCE_DIR}/smc_gen/r_sci_rx/src
    ${PROJECT_SOURCE_DIR}/smc_gen/r_sci_rx/src/targets/rx72m

    # BYTEQ (TX/RX ring queue used internally by r_sci_rx)
    ${PROJECT_SOURCE_DIR}/smc_gen/r_byteq
    ${PROJECT_SOURCE_DIR}/smc_gen/r_byteq/src

    # Pin configuration headers (r_sci_rx_pinset.h)
    ${PROJECT_SOURCE_DIR}/smc_gen/r_pincfg
)

# ─── Linker configuration ────────────────────────────────────────────────────
target_link_options(ads1263_app PRIVATE
    # Custom linker script — must come before -l flags
    -T ${PROJECT_SOURCE_DIR}/linker_script.ld

    # Add project root to ld's search path so the INCLUDE directive inside
    # linker_script.ld can resolve:
    #   INCLUDE smc_gen/r_bsp/mcu/all/linker_script_rvectors.inc
    -L ${PROJECT_SOURCE_DIR}

    # Link map for post-build inspection
    -Wl,-Map=${PROJECT_BINARY_DIR}/ads1263_app.map,--cref

    # C library and compiler-support (after all object files)
    -lc
    -lgcc
)

# ─── Post-build: generate HEX and BIN ────────────────────────────────────────
if(CMAKE_OBJCOPY)
    add_custom_command(TARGET ads1263_app POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex
                $<TARGET_FILE:ads1263_app>
                ${PROJECT_BINARY_DIR}/ads1263_app.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:ads1263_app>
                ${PROJECT_BINARY_DIR}/ads1263_app.bin
        COMMENT "Generating ads1263_app.hex and ads1263_app.bin"
    )
endif()

# ─── Post-build: print memory usage ──────────────────────────────────────────
if(CMAKE_SIZE)
    add_custom_command(TARGET ads1263_app POST_BUILD
        COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:ads1263_app>
        COMMENT "Memory usage"
    )
endif()

# ─── Build instructions (printed once at configure time) ─────────────────────
message(STATUS "")
message(STATUS "Build with:")
message(STATUS "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=rx72n-toolchain.cmake")
message(STATUS "  cmake --build build")
message(STATUS "")
message(STATUS "Outputs in build/:")
message(STATUS "  ads1263_app      (ELF — load with GDB/e2 debugger)")
message(STATUS "  ads1263_app.hex  (Intel HEX — flash with Renesas Flash Programmer)")
message(STATUS "  ads1263_app.bin  (raw binary)")
message(STATUS "  ads1263_app.map  (linker map)")
message(STATUS "")
