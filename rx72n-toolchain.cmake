# ─── GCC-RX cross-toolchain for RX72M (Renesas GNURX / rx-elf) ──────────────
#
# Usage:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=rx72n-toolchain.cmake
#
# Requires rx-elf-gcc (Renesas GCC for RX) on PATH.
# Tested with GCC 14.2.0.202505-GNURX (rx72m not available; rx72t + misa=v3 used).

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR rx)

# ─── Toolchain prefix ────────────────────────────────────────────────────────
set(CROSS rx-elf)

find_program(CMAKE_C_COMPILER   ${CROSS}-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER ${CROSS}-g++ REQUIRED)
# Use gcc as the assembler driver so .S files get C-preprocessor pass first
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})

find_program(CMAKE_OBJCOPY ${CROSS}-objcopy)
find_program(CMAKE_OBJDUMP ${CROSS}-objdump)
find_program(CMAKE_SIZE    ${CROSS}-size)

# ─── Sysroot search policy ───────────────────────────────────────────────────
# Never search host paths for programs that CMake runs at build-configure time,
# but DO search only the cross-compiler's sysroot for libraries and headers.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# ─── Don't try to link an executable during compiler feature-test ─────────────
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ─── CPU / ISA flags ─────────────────────────────────────────────────────────
# -mcpu=rx72t : closest GCC-RX cpu target for RX72M (rx72m not listed in this GCC build)
# -misa=v3    : RX ISA v3 (DSP + FPU instructions, required for RX72M peripherals)
# little-endian-data is the default; no flag needed
set(_RX_CPU_FLAGS "-mcpu=rx72t -misa=v3")

# ─── Common compile flags ────────────────────────────────────────────────────
set(_COMMON "-Os -ffunction-sections -fdata-sections -Wall -Wextra")

set(CMAKE_C_FLAGS_INIT
    "${_RX_CPU_FLAGS} ${_COMMON}"
    CACHE STRING "" FORCE)

set(CMAKE_CXX_FLAGS_INIT
    "${_RX_CPU_FLAGS} ${_COMMON} -fno-exceptions -fno-rtti -fno-use-cxa-atexit"
    CACHE STRING "" FORCE)

# -x assembler-with-cpp : force preprocessor pass on .S files
set(CMAKE_ASM_FLAGS_INIT
    "${_RX_CPU_FLAGS} -x assembler-with-cpp"
    CACHE STRING "" FORCE)

# ─── Executable linker flags ─────────────────────────────────────────────────
# -nostartfiles  : BSP supplies its own startup (reset_program.S / resetprg.c)
# --gc-sections  : remove dead code/data (requires -ffunction/data-sections above)
# -u _printf_float : pull float formatting into newlib vsnprintf (for log_printf)
# -lc -lgcc      : newlib C library + GCC compiler-support routines
#
# The linker script path and -L search path are set per-target in CMakeLists.txt
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-nostartfiles -Wl,--gc-sections -u _printf_float"
    CACHE STRING "" FORCE)
