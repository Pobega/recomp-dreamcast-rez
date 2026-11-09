# Cross-build for PortMaster handhelds (aarch64 Cortex-A53) with the host's clang and lld,
# against the sysroot and GCC runtime in build/toolchain.
# DREAMCOMP_TOOLCHAIN_ROOT names the toolchain root holding aarch64-nextui-linux-gnu/
# (tools/toolchain.sh extracts it into build/toolchain).
#   cmake -S . -B <build> -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-portmaster.cmake \
#         -DDREAMCOMP_TOOLCHAIN_ROOT=<root> ...
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
if(NOT DREAMCOMP_TOOLCHAIN_ROOT AND DEFINED ENV{DREAMCOMP_TOOLCHAIN_ROOT})
  set(DREAMCOMP_TOOLCHAIN_ROOT $ENV{DREAMCOMP_TOOLCHAIN_ROOT})
elseif(NOT DREAMCOMP_TOOLCHAIN_ROOT AND DEFINED DREAMCOMP_TG5040_ROOT)
  set(DREAMCOMP_TOOLCHAIN_ROOT ${DREAMCOMP_TG5040_ROOT})
elseif(NOT DREAMCOMP_TOOLCHAIN_ROOT AND DEFINED ENV{DREAMCOMP_TG5040_ROOT})
  set(DREAMCOMP_TOOLCHAIN_ROOT $ENV{DREAMCOMP_TG5040_ROOT})
endif()
if(NOT EXISTS "${DREAMCOMP_TOOLCHAIN_ROOT}/aarch64-nextui-linux-gnu/libc")
  message(FATAL_ERROR "portmaster: DREAMCOMP_TOOLCHAIN_ROOT (${DREAMCOMP_TOOLCHAIN_ROOT}) is not the toolchain's root")
endif()
# try_compile projects get the toolchain file again; pass the root on to them.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES DREAMCOMP_TOOLCHAIN_ROOT)
set(CMAKE_SYSROOT "${DREAMCOMP_TOOLCHAIN_ROOT}/aarch64-nextui-linux-gnu/libc")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-nextui-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-nextui-linux-gnu)
# Assembly too (libchdr's LZMA decoder has an arm64 one); without a target clang assembles for the host.
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_ASM_COMPILER_TARGET aarch64-nextui-linux-gnu)
set(_portmaster_flags "--gcc-toolchain=${DREAMCOMP_TOOLCHAIN_ROOT} -mcpu=cortex-a53")
set(CMAKE_C_FLAGS_INIT "${_portmaster_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_portmaster_flags}")
set(CMAKE_ASM_FLAGS_INIT "${_portmaster_flags}")
# The device's libstdc++ is older than the toolchain's headers may assume: link the C++ runtime in.
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_portmaster_flags} -fuse-ld=lld -static-libstdc++ -static-libgcc")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_portmaster_flags} -fuse-ld=lld -static-libstdc++ -static-libgcc")
# GCC 8's std::filesystem lives in a library of its own, and glibc 2.28's threads too (before 2.34).
set(CMAKE_CXX_STANDARD_LIBRARIES "-lstdc++fs -lpthread")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
