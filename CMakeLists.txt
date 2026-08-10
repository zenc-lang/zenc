cmake_minimum_required(VERSION 3.10)
project(zc C)
include(GNUInstallDirs)

# Set C standard. Override with: cmake -DCMAKE_C_STANDARD=11
set(CMAKE_C_STANDARD 23 CACHE STRING "C standard to use")
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Derive ZEN_VERSION from git (similar to Makefile)
set(ZEN_VERSION "dev")
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE GIT_DESCRIBE_RESULT
    OUTPUT_VARIABLE GIT_DESCRIBE_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(GIT_DESCRIBE_RESULT EQUAL 0 AND NOT "${GIT_DESCRIBE_OUTPUT}" STREQUAL "")
    set(ZEN_VERSION "${GIT_DESCRIBE_OUTPUT}")
else()
    # Fallback: read .version file, then "unknown"
    set(ZEN_VERSION "unknown")
    if(EXISTS "${CMAKE_SOURCE_DIR}/.version")
        file(READ "${CMAKE_SOURCE_DIR}/.version" VERSION_FILE)
        string(STRIP "${VERSION_FILE}" ZEN_VERSION)
    endif()
endif()

# Set ZEN_SHARE_DIR for runtime file lookups (similar intent as Makefile)
if(NOT DEFINED CMAKE_INSTALL_FULL_DATADIR)
    set(CMAKE_INSTALL_FULL_DATADIR "${CMAKE_INSTALL_PREFIX}/share")
endif()
set(ZEN_SHARE_DIR "${CMAKE_INSTALL_FULL_DATADIR}/zc")

# Provide these as compile definitions for all targets
add_compile_definitions(
    ZEN_VERSION="${ZEN_VERSION}"
    ZC_ALLOW_INTERNAL
    ZEN_SHARE_DIR="${ZEN_SHARE_DIR}"
)
# --- Compiler flags (set per compiler) ---
set(ZC_COMPILE_FLAGS "")
set(ZC_BASE_FLAGS -Wall -Wcast-align -Wconversion -Wdangling-else -Wdisabled-optimization -Wdouble-promotion -Wextra -Wfloat-equal -Wformat=2 -Wimplicit-fallthrough -Wmissing-field-initializers -Wmissing-prototypes -Wnull-dereference -Wpacked -Wredundant-decls -Wreturn-local-addr -Wshadow -Wshift-negative-value -Wsign-compare -Wstrict-prototypes -Wswitch-default -Wtautological-compare -Wtype-limits -Wundef -Wuninitialized -Wvla -g)
if(WIN32)
    if(MSVC)
        set(ZC_COMPILE_FLAGS "/W4 /Zi /D_CRT_SECURE_NO_WARNINGS")
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
        set(ZC_COMPILE_FLAGS ${ZC_BASE_FLAGS})
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(ZC_COMPILE_FLAGS ${ZC_BASE_FLAGS} -Wduplicated-cond -Wformat-signedness -Wlogical-op -Wsuggest-attribute=const -Wsuggest-attribute=noreturn -Wunsafe-loop-optimizations)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "TinyCC")
        set(ZC_COMPILE_FLAGS -Wall -g)
    endif()
else()
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
        set(ZC_COMPILE_FLAGS ${ZC_BASE_FLAGS} -Wno-format-nonliteral -Wassign-enum -Wcomma -Wsometimes-uninitialized -Wloop-analysis -Wsizeof-array-div)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(ZC_COMPILE_FLAGS ${ZC_BASE_FLAGS} -Wduplicated-cond -Wformat-signedness -Wlogical-op -Wsuggest-attribute=const -Wsuggest-attribute=noreturn -Wunsafe-loop-optimizations)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "TinyCC")
        set(ZC_COMPILE_FLAGS -Wall -g)
    endif()
endif()

# --- Find Required Packages (Modern CMake Way) ---
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/ast
    ${PROJECT_SOURCE_DIR}/src/parser
    ${PROJECT_SOURCE_DIR}/src/codegen
    ${PROJECT_SOURCE_DIR}/plugins
    ${PROJECT_SOURCE_DIR}/src/zen
    ${PROJECT_SOURCE_DIR}/src/utils
    ${PROJECT_SOURCE_DIR}/src/lexer
    ${PROJECT_SOURCE_DIR}/src/analysis
    ${PROJECT_SOURCE_DIR}/src/lsp
    ${PROJECT_SOURCE_DIR}/src/diagnostics
)

# Source files
# Source files read from src-sources.txt (single manifest shared by Makefile, CMakeLists.txt, build.bat)
file(STRINGS src-sources.txt SRCS)

# --- Find JIT Backend (LibTCC) ---
option(ZC_USE_JIT "Enable JIT support in REPL using LibTCC" ON)

if(ZC_USE_JIT)
    find_library(TCC_LIBRARY NAMES tcc libtcc)
    if(TCC_LIBRARY)
        add_compile_definitions(ZC_HAS_JIT)
        message(STATUS "LibTCC found: ${TCC_LIBRARY}. JIT enabled.")
    else()
        message(WARNING "libtcc not found. JIT support will be disabled. Fallback will be used.")
        set(ZC_USE_JIT OFF)
    endif()
endif()

# --- Feature Flags ---
option(ZC_NO_PLUGINS "Disable the plugin system entirely" OFF)
option(ZC_NO_LSP "Disable LSP server" OFF)
option(ZC_NO_REPL "Disable interactive REPL" OFF)
option(ZC_NO_BACKENDS "Disable non-C backends" OFF)

add_compile_definitions(ZC_HAS_LSP=$<NOT:$<BOOL:${ZC_NO_LSP}>>)
add_compile_definitions(ZC_HAS_REPL=$<NOT:$<BOOL:${ZC_NO_REPL}>>)
add_compile_definitions(ZC_HAS_PLUGINS=$<NOT:$<BOOL:${ZC_NO_PLUGINS}>>)
add_compile_definitions(ZC_HAS_CPP_BACKEND=$<NOT:$<BOOL:${ZC_NO_BACKENDS}>>)
add_compile_definitions(ZC_HAS_CUDA_BACKEND=$<NOT:$<BOOL:${ZC_NO_BACKENDS}>>)
add_compile_definitions(ZC_HAS_OBJC_BACKEND=$<NOT:$<BOOL:${ZC_NO_BACKENDS}>>)
add_compile_definitions(ZC_HAS_JSON_BACKEND=$<NOT:$<BOOL:${ZC_NO_BACKENDS}>>)
add_compile_definitions(ZC_HAS_LISP_BACKEND=$<NOT:$<BOOL:${ZC_NO_BACKENDS}>>)
add_compile_definitions(ZC_HAS_DOT_BACKEND=$<NOT:$<BOOL:${ZC_NO_BACKENDS}>>)
add_compile_definitions(ZC_HAS_ASTDUMP_BACKEND=$<NOT:$<BOOL:${ZC_NO_BACKENDS}>>)

if(ZC_NO_PLUGINS)
    add_compile_definitions(ZC_NO_PLUGINS)
    message(STATUS "Plugin system disabled.")
endif()

# Platform-specific library logic
set(PLATFORM_LIBS Threads::Threads)
if(ZC_USE_JIT)
    list(APPEND PLATFORM_LIBS ${TCC_LIBRARY})
endif()
if(WIN32)
    # Winsock 2 for Networking, ws2_32 is required for our socket abstraction
    list(APPEND PLATFORM_LIBS ws2_32)
    set(PLUGIN_EXT ".dll")
    set(BINARY_EXT ".exe")
    set(SHARED_FLAGS "")
elseif(APPLE)
    # macOS requires CoreFoundation for _NSGetExecutablePath
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    list(APPEND PLATFORM_LIBS ${CMAKE_DL_LIBS} ${COREFOUNDATION_FRAMEWORK})
    set(SHARED_FLAGS "-fPIC")
    set(BINARY_EXT "")
    set(PLUGIN_EXT ".dylib")
else()
    # Linux/BSD: Link with libdl for zc_dlopen and libm for math
    if(NOT ZC_NO_PLUGINS)
        list(APPEND PLATFORM_LIBS ${CMAKE_DL_LIBS})
    endif()
    list(APPEND PLATFORM_LIBS m)
    set(SHARED_FLAGS "-fPIC")
    set(BINARY_EXT "")
    set(PLUGIN_EXT ".so")
endif()

# Add executable
add_executable(zc ${SRCS})
target_link_libraries(zc PRIVATE ${PLATFORM_LIBS})
if(NOT "${ZC_COMPILE_FLAGS}" STREQUAL "")
    target_compile_options(zc PRIVATE ${ZC_COMPILE_FLAGS})
endif()

# Build plugins using our newly built zc compiler
set(PLUGIN_NAMES befunge brainfuck forth lisp sql)
foreach(plugin ${PLUGIN_NAMES})
    set(PLUGIN_OUTPUT "${CMAKE_BINARY_DIR}/plugins/${plugin}${PLUGIN_EXT}")
    
    add_custom_command(
        OUTPUT "${PLUGIN_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/plugins"
        COMMAND $<TARGET_FILE:zc> build "${PROJECT_SOURCE_DIR}/plugins/${plugin}.zc" -shared -o "${PLUGIN_OUTPUT}"
        DEPENDS zc "${PROJECT_SOURCE_DIR}/plugins/${plugin}.zc"
        COMMENT "Building native plugin ${plugin}"
        VERBATIM
    )
    
    add_custom_target(${plugin}_plugin ALL DEPENDS "${PLUGIN_OUTPUT}")
endforeach()

# --- Install Rules ---
install(TARGETS zc DESTINATION bin)
install(FILES std.zc DESTINATION share/zenc)
install(DIRECTORY std DESTINATION share/zenc)
install(FILES src/repl/docs.json DESTINATION share/zenc)
install(FILES plugins/zprep_plugin.h DESTINATION include/zenc)
install(DIRECTORY ${CMAKE_BINARY_DIR}/plugins/ DESTINATION lib/zenc/plugins 
        FILES_MATCHING PATTERN "*.${PLUGIN_EXT}")

if(UNIX AND NOT WIN32)
    install(FILES man/zc.1 DESTINATION share/man/man1)
    install(FILES man/zc.5 DESTINATION share/man/man5)
    install(FILES man/zc.7 DESTINATION share/man/man7)
endif()
# Test target (supports: cmake --build . --target test -- -DTEST_FILES="tests/std/test_hash.zc")
set(TEST_FILES "" CACHE STRING "Space-separated list of specific test files to run")
add_custom_target(test
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/run_tests.sh -- ${TEST_FILES}
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/run_codegen_tests.sh ${TEST_FILES}
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/run_example_transpile.sh ${TEST_FILES}
    DEPENDS zc
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(test-misra
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/run_misra_tests.sh
    DEPENDS zc
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(test-tcc
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/run_tests.sh --cc tcc
    DEPENDS zc
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Clean target
add_custom_target(distclean
    COMMAND ${CMAKE_COMMAND} --build . --target clean
    COMMAND ${CMAKE_COMMAND} -E rm -f out.c
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/plugins
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)