cmake_minimum_required(VERSION 3.16)
project(SerenityOS C CXX ASM)

if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "")
  message(FATAL_ERROR
    ": Don't use CMAKE_BUILD_TYPE when building serenity.\n"
    "The default build type is optimized with debug info and asserts enabled,\n"
    "and that's all there is.")
endif()

if (CMAKE_COMPILER_IS_GNUCXX AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10.2)
  message(FATAL_ERROR
      "A GCC version less than 10.2 was detected (${CMAKE_CXX_COMPILER_VERSION}), this is unsupported.\n"
      "Please re-read the build instructions documentation, and upgrade your host compiler.\n")
endif()

set(CMAKE_INSTALL_MESSAGE NEVER)

enable_testing()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SERENITY_ARCH "i686" CACHE STRING "Target architecture for SerenityOS.")

if("${SERENITY_ARCH}" STREQUAL "i686")
    set(SERENITY_CLANG_ARCH "i386")
else()
    set(SERENITY_CLANG_ARCH ${SERENITY_ARCH})
endif()

# Central location for all custom options used in the Serenity build.
option(ENABLE_ADDRESS_SANITIZER "Enable address sanitizer testing in gcc/clang" OFF)
option(ENABLE_KERNEL_ADDRESS_SANITIZER "Enable kernel address sanitizer testing in gcc/clang" OFF)
option(ENABLE_KERNEL_COVERAGE_COLLECTION "Enable KCOV and kernel coverage instrumentation in gcc/clang" OFF)
option(ENABLE_MEMORY_SANITIZER "Enable memory sanitizer testing in gcc/clang" OFF)
option(ENABLE_UNDEFINED_SANITIZER "Enable undefined behavior sanitizer testing in gcc/clang" OFF)
option(ENABLE_FUZZER_SANITIZER "Enable fuzzer sanitizer testing in clang" OFF)
option(ENABLE_EXTRA_KERNEL_DEBUG_SYMBOLS "Enable -Og and -ggdb3 options for Kernel code for easier debugging" OFF)
option(ENABLE_ALL_THE_DEBUG_MACROS "Enable all debug macros to validate they still compile" OFF)
option(ENABLE_ALL_DEBUG_FACILITIES "Enable all noisy debug symbols and options. Not recommended for normal developer use" OFF)
option(ENABLE_COMPILETIME_FORMAT_CHECK "Enable compiletime format string checks" ON)
option(ENABLE_PCI_IDS_DOWNLOAD "Enable download of the pci.ids database at build time" ON)
option(ENABLE_USB_IDS_DOWNLOAD "Enable download of the usb.ids database at build time" ON)
option(BUILD_LAGOM "Build parts of the system targeting the host OS for fuzzing/testing" OFF)
option(ENABLE_KERNEL_LTO "Build the kernel with link-time optimization" OFF)
option(USE_CLANG_TOOLCHAIN "Build the kernel with the experimental Clang toolchain" OFF)

# Meta target to run all code-gen steps in the build.
add_custom_target(all_generated)

add_custom_target(run
    COMMAND "${CMAKE_COMMAND}" -E env "SERENITY_ARCH=${SERENITY_ARCH}" "${SerenityOS_SOURCE_DIR}/Meta/run.sh"
    USES_TERMINAL
)

# This can currently only be implemented by ordered commands
# as cmake doesn't support inter dependency ordering, and we
# would like to avoid inject dependencies on the existing
# custom commands to allow people to run commands adhoc with
# out forcing re-builds when they might not want them.
add_custom_target(setup-and-run
    COMMAND ${CMAKE_MAKE_PROGRAM} install
    COMMAND ${CMAKE_MAKE_PROGRAM} image
    COMMAND ${CMAKE_MAKE_PROGRAM} run
    USES_TERMINAL
)

add_custom_target(image
    DEPENDS qemu-image
)

set(GCC_VERSION 11.2.0)
set(LLVM_VERSION 12.0.1)

add_custom_target(qemu-image
    COMMAND "${CMAKE_COMMAND}" -E env "SERENITY_SOURCE_DIR=${SerenityOS_SOURCE_DIR}" "SERENITY_ARCH=${SERENITY_ARCH}" "USE_CLANG_TOOLCHAIN=$<BOOL:${USE_CLANG_TOOLCHAIN}>" "LLVM_VERSION=${LLVM_VERSION}" "${SerenityOS_SOURCE_DIR}/Meta/build-image-qemu.sh"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/_disk_image"
    USES_TERMINAL
)
add_custom_target(grub-image
    COMMAND ${CMAKE_COMMAND} -E env "SERENITY_SOURCE_DIR=${SerenityOS_SOURCE_DIR}" "SERENITY_ARCH=${SERENITY_ARCH}" "USE_CLANG_TOOLCHAIN=$<BOOL:${USE_CLANG_TOOLCHAIN}>" "LLVM_VERSION=${LLVM_VERSION}" "${SerenityOS_SOURCE_DIR}/Meta/build-image-grub.sh"
    BYPRODUCTS ${CMAKE_BINARY_DIR}/grub_disk_image
    USES_TERMINAL
)
add_custom_target(extlinux-image
    COMMAND "${CMAKE_COMMAND}" -E env "SERENITY_SOURCE_DIR=${SerenityOS_SOURCE_DIR}" "SERENITY_ARCH=${SERENITY_ARCH}" "USE_CLANG_TOOLCHAIN=$<BOOL:${USE_CLANG_TOOLCHAIN}>" "LLVM_VERSION=${LLVM_VERSION}" "${SerenityOS_SOURCE_DIR}/Meta/build-image-extlinux.sh"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/extlinux_disk_image"
    USES_TERMINAL
)

add_custom_target(lint-shell-scripts
    COMMAND "${SerenityOS_SOURCE_DIR}/Meta/lint-shell-scripts.sh"
    USES_TERMINAL
)
add_custom_target(check-style
    COMMAND "${SerenityOS_SOURCE_DIR}/Meta/check-style.sh"
    USES_TERMINAL
)

add_custom_target(install-ports
    COMMAND "${CMAKE_COMMAND}" -E env "SERENITY_SOURCE_DIR=${SerenityOS_SOURCE_DIR}" "SERENITY_ARCH=${SERENITY_ARCH}" "${SerenityOS_SOURCE_DIR}/Meta/install-ports-tree.sh"
    USES_TERMINAL
)

add_custom_target(configure-components
    COMMAND "$<TARGET_FILE:ConfigureComponents>"
    USES_TERMINAL
)
add_dependencies(configure-components ConfigureComponents)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-fsized-deallocation)
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    add_compile_options(-Wno-literal-suffix)
elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang$")
    add_compile_options(-Wno-overloaded-virtual)
    add_compile_options(-Wno-user-defined-literals)
    add_compile_options(-fconstexpr-steps=16777216)
endif()

if (ENABLE_ALL_DEBUG_FACILITIES)
    set(ENABLE_ALL_THE_DEBUG_MACROS ON)
    set(ENABLE_EXTRA_KERNEL_DEBUG_SYMBOLS ON)

    # Immediately finds violations during boot, shouldn't be discoverable
    # by people who aren't working on fixing issues. Use this check to make
    # sure this code continues to build instead of all_debug_macros to avoid
    # people filing bugs.
    set(KMALLOC_VERIFY_NO_SPINLOCK_HELD ON)

    # Enables KCOV API and injects kernel coverage instrumentation via
    # -fsanitize-coverage=trace-pc. Mostly here to ensure that the CI catches
    # commits breaking this flag.
    set(ENABLE_KERNEL_COVERAGE_COLLECTION ON)
endif()

if (ENABLE_ALL_THE_DEBUG_MACROS)
    include("${SerenityOS_SOURCE_DIR}/Meta/CMake/all_the_debug_macros.cmake")
endif(ENABLE_ALL_THE_DEBUG_MACROS)

configure_file(AK/Debug.h.in AK/Debug.h @ONLY)
configure_file(Kernel/Debug.h.in Kernel/Debug.h @ONLY)

include_directories(Userland/Libraries)
include_directories(.)
include_directories(${CMAKE_BINARY_DIR})

add_subdirectory(Meta/Lagom)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

# FIXME: With cmake 3.18, we can change unzip/untar steps to use
#        file(ARCHIVE_EXTRACT) instead
find_program(UNZIP unzip REQUIRED)
find_program(TAR tar REQUIRED)

unset(CMAKE_SYSROOT)
set(CMAKE_STAGING_PREFIX ${CMAKE_BINARY_DIR}/Root)
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/Root)
set(CMAKE_INSTALL_DATAROOTDIR ${CMAKE_BINARY_DIR}/Root/res)


if (${CMAKE_HOST_SYSTEM_NAME} MATCHES SerenityOS)
    message("Good job on building cmake!")
elseif(USE_CLANG_TOOLCHAIN)
    set(TOOLCHAIN_ROOT ${CMAKE_SOURCE_DIR}/Toolchain/Local/clang/${SERENITY_ARCH}/)
    set(TOOLCHAIN_PATH ${TOOLCHAIN_ROOT}/bin)

    set(CMAKE_C_COMPILER ${TOOLCHAIN_PATH}/clang)
    set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PATH}/clang++)
    set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PATH}/clang)
    set(CMAKE_LINKER ${TOOLCHAIN_PATH}/ld.lld)
    set(CMAKE_RANLIB ${TOOLCHAIN_PATH}/llvm-ranlib)
    set(CMAKE_STRIP ${TOOLCHAIN_PATH}/llvm-strip)
    set(CMAKE_AR ${TOOLCHAIN_PATH}/llvm-ar)
    set(CMAKE_CXXFILT ${TOOLCHAIN_PATH}/llvm-cxxfilt)
    # FIXME: Persuade LLVM maintainers to add `--update-section` to llvm-objcopy, as it's required for the kernel symbol map.
    set(CMAKE_OBJCOPY ${TOOLCHAIN_ROOT}/binutils/bin/${SERENITY_ARCH}-pc-serenity-objcopy)
else()
    set(TOOLCHAIN_ROOT ${SerenityOS_SOURCE_DIR}/Toolchain/Local/${SERENITY_ARCH}/)
    set(TOOLCHAIN_PATH ${TOOLCHAIN_ROOT}/bin)
    set(TOOLCHAIN_PREFIX ${TOOLCHAIN_PATH}/${SERENITY_ARCH}-pc-serenity-)

    set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
    set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
    set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
    set(CMAKE_LINKER ${TOOLCHAIN_PREFIX}ld)
    set(CMAKE_RANLIB ${TOOLCHAIN_PREFIX}gcc-ranlib)
    set(CMAKE_STRIP ${TOOLCHAIN_PREFIX}strip)
    set(CMAKE_AR ${TOOLCHAIN_PREFIX}gcc-ar)
    set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
    set(CMAKE_CXXFILT ${TOOLCHAIN_PREFIX}c++filt)
endif()

foreach(lang ASM C CXX OBJC OBJCXX)
    unset(CMAKE_${lang}_OSX_COMPATIBILITY_VERSION_FLAG)
    unset(CMAKE_${lang}_OSX_CURRENT_VERSION_FLAG)
    unset(CMAKE_${lang}_LINK_FLAGS)
    unset(CMAKE_SHARED_LIBRARY_CREATE_${lang}_FLAGS)
    unset(CMAKE_SHARED_MODULE_CREATE_${lang}_FLAGS)
    unset(CMAKE_SHARED_MODULE_LOADER_${lang}_FLAG )
    unset(CMAKE_${lang}_OSX_DEPLOYMENT_TARGET_FLAG)
    unset(CMAKE_${lang}_SYSROOT_FLAG)
    if (CMAKE_SYSTEM_NAME MATCHES Darwin)
        ## macOS workaround. Use GNU ld flags for SONAMEs.
        set(CMAKE_${lang}_CREATE_SHARED_LIBRARY
            "<CMAKE_${lang}_COMPILER> <LANGUAGE_COMPILE_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_${lang}_FLAGS> <LINK_FLAGS> <SONAME_FLAG><TARGET_SONAME> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
        set(CMAKE_SHARED_LIBRARY_SONAME_${lang}_FLAG "-Wl,-soname,")
    endif()
endforeach()

set(CMAKE_INSTALL_NAME_TOOL "")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")
set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "-shared -Wl,--hash-style=gnu,-z,relro,-z,now,-z,noexecstack,-z,separate-code")
set(CMAKE_CXX_LINK_FLAGS "-Wl,--hash-style=gnu,-z,relro,-z,now,-z,noexecstack,-z,max-page-size=0x1000,-z,separate-code")

# We disable it completely because it makes cmake very spammy.
# This will need to be revisited when the Loader supports RPATH/RUN_PATH.
set(CMAKE_SKIP_RPATH TRUE)

add_compile_options(-Wformat=2)
add_compile_options(-fdiagnostics-color=always)

if (NOT ${CMAKE_HOST_SYSTEM_NAME} MATCHES SerenityOS)
    # FIXME: Something makes this go crazy and flag unused variables that aren't flagged as such when building with the toolchain.
    #        Disable -Werror for now.
    add_compile_options(-Werror)
endif()

add_compile_options(-Wall)
add_compile_options(-Wextra)

# The following warnings are sorted by the "base" name (the part excluding the initial Wno or W).
add_compile_options(-Wno-address-of-packed-member)
add_compile_options(-Wcast-qual)
add_compile_options(-Wno-deprecated-copy)
add_compile_options(-Wduplicated-cond)
add_compile_options(-Wno-expansion-to-defined)
add_compile_options(-Wformat=2)
add_compile_options(-Wimplicit-fallthrough)
add_compile_options(-Wlogical-op)
add_compile_options(-Wmisleading-indentation)
add_compile_options(-Wmissing-declarations)
add_compile_options(-Wnon-virtual-dtor)
add_compile_options(-Wno-unknown-warning-option)
add_compile_options(-Wundef)
add_compile_options(-Wunused)
add_compile_options(-Wno-unused-private-field)
add_compile_options(-Wno-unused-const-variable)
add_compile_options(-Wno-unused-command-line-argument)
add_compile_options(-Wwrite-strings)
add_compile_options(-Wno-maybe-uninitialized)

add_compile_options(-ffile-prefix-map=${SerenityOS_SOURCE_DIR}=.)
add_compile_options(-fno-exceptions)
add_compile_options(-ftls-model=initial-exec)
add_compile_options(-fno-semantic-interposition)
add_compile_options(-fstack-clash-protection)
add_compile_options(-fstack-protector-strong)
add_compile_options(-g1)

if (USE_CLANG_TOOLCHAIN)
    add_compile_options(-Wno-atomic-alignment)
    add_compile_options(-Wno-c99-designator)
    add_compile_options(-Wno-implicit-const-int-float-conversion)
    add_compile_options(-Wno-inconsistent-missing-override)
    add_compile_options(-Wno-tautological-constant-out-of-range-compare)
    add_compile_options(-Wno-unneeded-internal-declaration)
    add_compile_options(-Wno-unused-function)
    add_compile_options(-Wno-user-defined-literals)
    # Without the 'SHELL' prefix, this would get removed through de-duplication with the flags set for the host compiler.
    # Then, that would come before '-Wextra', so it would not negate the '-Woverloaded-virtual' set by '-Wextra'.
    add_compile_options(SHELL:-Wno-overloaded-virtual)

    add_compile_options(--sysroot=${CMAKE_BINARY_DIR}/Root)
    add_compile_options(--target=${SERENITY_CLANG_ARCH}-pc-serenity)
    add_compile_options(-fno-aligned-allocation)
    add_compile_options(-fconstexpr-steps=16777216)
    add_compile_options(-gdwarf-4)

    # FIXME: Why is Clang not picking up this path?
    link_directories(${TOOLCHAIN_ROOT}/lib/clang/${LLVM_VERSION}/lib/serenity)

    add_link_options(LINKER:--allow-shlib-undefined)
else()
    add_compile_options(-Wcast-align)
    add_compile_options(-Wdouble-promotion)
endif()

add_link_options(LINKER:-z,text)

if("${SERENITY_ARCH}" STREQUAL "i686")
    add_compile_options(-march=i686)
elseif("${SERENITY_ARCH}" STREQUAL "x86_64")
    add_compile_options(-march=x86-64)
endif()

add_compile_definitions(SANITIZE_PTRS)
set(CMAKE_CXX_FLAGS_STATIC "${CMAKE_CXX_FLAGS} -static")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pie -fpic")

if (ENABLE_COMPILETIME_FORMAT_CHECK)
    add_compile_definitions(ENABLE_COMPILETIME_FORMAT_CHECK)
endif()

add_link_options(--sysroot ${CMAKE_BINARY_DIR}/Root)
add_link_options(-Wno-unused-command-line-argument)

include_directories(.)
include_directories(Userland/Libraries)
include_directories(Userland/Libraries/LibC)
include_directories(Userland/Libraries/LibCrypt)
include_directories(Userland/Libraries/LibM)
include_directories(Userland/Libraries/LibPthread)
include_directories(Userland/Libraries/LibSystem)
include_directories(Userland/Services)
include_directories(Userland)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR}/Userland/Services)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/Userland/Libraries)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/Userland)

# FIXME: vptr sanitizing requires.. intense ABI wrangling of std::type_info
#     And would be better served by porting ubsan_type_hash_itanium.cpp from compiler-rt
if (ENABLE_UNDEFINED_SANITIZER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined -fno-sanitize=vptr")
endif()

include(Meta/CMake/wasm_spec_tests.cmake)

add_custom_target(components ALL)
option(BUILD_EVERYTHING "Build all optional components" ON)

include(Meta/CMake/utils.cmake)

serenity_component(
    Tests
    RECOMMENDED
)

add_subdirectory(AK)
add_subdirectory(Kernel)
if(NOT "${SERENITY_ARCH}" STREQUAL "aarch64")
    add_subdirectory(Userland)
    add_subdirectory(Tests)
endif()

export_components("${CMAKE_BINARY_DIR}/components.ini")

set(PCI_IDS_GZ_URL https://pci-ids.ucw.cz/v2.2/pci.ids.gz)
set(PCI_IDS_GZ_PATH ${CMAKE_BINARY_DIR}/pci.ids.gz)
set(PCI_IDS_PATH ${CMAKE_BINARY_DIR}/pci.ids)
set(PCI_IDS_INSTALL_PATH ${CMAKE_INSTALL_DATAROOTDIR}/pci.ids)

if(ENABLE_PCI_IDS_DOWNLOAD AND NOT EXISTS ${PCI_IDS_GZ_PATH})
    message(STATUS "Downloading PCI ID database from ${PCI_IDS_GZ_URL}...")
    file(DOWNLOAD ${PCI_IDS_GZ_URL} ${PCI_IDS_GZ_PATH} INACTIVITY_TIMEOUT 10)
endif()

if(EXISTS ${PCI_IDS_GZ_PATH} AND NOT EXISTS ${PCI_IDS_INSTALL_PATH})
    message(STATUS "Extracting PCI ID database from ${PCI_IDS_GZ_PATH}...")
    execute_process(COMMAND gzip -k -d ${PCI_IDS_GZ_PATH})
    file(MAKE_DIRECTORY ${CMAKE_INSTALL_DATAROOTDIR})
    file(RENAME ${PCI_IDS_PATH} ${PCI_IDS_INSTALL_PATH})
endif()

set(USB_IDS_GZ_URL http://www.linux-usb.org/usb.ids.gz)
set(USB_IDS_GZ_PATH ${CMAKE_BINARY_DIR}/usb.ids.gz)
set(USB_IDS_PATH ${CMAKE_BINARY_DIR}/usb.ids)
set(USB_IDS_INSTALL_PATH ${CMAKE_INSTALL_DATAROOTDIR}/usb.ids)

if(ENABLE_USB_IDS_DOWNLOAD AND NOT EXISTS ${USB_IDS_GZ_PATH})
    message(STATUS "Downloading USB ID database from ${USB_IDS_GZ_URL}...")
    file(DOWNLOAD ${USB_IDS_GZ_URL} ${USB_IDS_GZ_PATH} INACTIVITY_TIMEOUT 10)
endif()

if(EXISTS ${USB_IDS_GZ_PATH} AND NOT EXISTS ${USB_IDS_INSTALL_PATH})
    message(STATUS "Extracting USB ID database from ${USB_IDS_GZ_PATH}...")
    execute_process(COMMAND gzip -k -d ${USB_IDS_GZ_PATH})
    file(MAKE_DIRECTORY ${CMAKE_INSTALL_DATAROOTDIR})
    file(RENAME ${USB_IDS_PATH} ${USB_IDS_INSTALL_PATH})
endif()
