cmake_minimum_required(VERSION 3.15)
project(DynGen)
set(CMAKE_CXX_STANDARD 17)

#--------------------- options ---------------------------#

# SuperCollider API:
set(SC_PATH "" CACHE PATH "Path to SuperCollider source code")
if (SC_PATH)
    message(STATUS "SC_PATH: ${SC_PATH}")
else()
    message(WARNING "SC_PATH not set")
endif()

# build Supernova version?
option(SUPERNOVA "Build plugins for supernova" OFF)
message(STATUS "SUPERNOVA: ${SUPERNOVA}")

# default installation path
if (WIN32)
    set(SC_INSTALL_DIR "$ENV{LOCALAPPDATA}/SuperCollider/Extensions/" CACHE PATH "Installation directoy")
elseif(APPLE)
    set(SC_INSTALL_DIR "~/Library/Application Support/SuperCollider/Extensions/" CACHE PATH "Installation directoy")
else()
    set(SC_INSTALL_DIR "~/.local/share/SuperCollider/Extensions/" CACHE PATH "Installation directoy")
endif()
message(STATUS "SC_INSTALL_DIR: ${SC_INSTALL_DIR}")

#--------------------- WDL/EEL2 ---------------------------#

set(WDL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/WDL/WDL")
set(EEL2_PATH "${WDL_PATH}/eel2")

add_library(eel2 STATIC
        "${EEL2_PATH}/nseel-caltab.c"
        "${EEL2_PATH}/nseel-compiler.c"
        "${EEL2_PATH}/nseel-eval.c"
        "${EEL2_PATH}/nseel-lextab.c"
        "${EEL2_PATH}/nseel-ram.c"
        "${EEL2_PATH}/nseel-yylex.c"
        "${EEL2_PATH}/nseel-cfunc.c"
        "${WDL_PATH}/fft.c"
)

target_include_directories(eel2 PUBLIC ${EEL2_PATH} ${WDL_PATH})

target_compile_definitions(eel2 PRIVATE
        WDL_FFT_REALSIZE=8
        _FILE_OFFSET_BITS=64
)

if(NOT MSVC)
    target_compile_options(eel2 PRIVATE
        -Wall
        -Wno-unused-function
        -Wno-multichar
        -Wno-unused-result
        -Wshadow -Wtype-limits
        -fsigned-char
        -fPIC
    )
endif()

target_compile_features(eel2 PUBLIC cxx_std_17)

# link with eel2 asm
if(APPLE)
    # we already have the necessary .o file (universal binary)
    target_link_libraries(eel2 PRIVATE "${EEL2_PATH}/asm-nseel-multi-macho.o")
elseif(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR
       CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
        # we already have the necessary .obj file
        target_link_libraries(eel2 PRIVATE "${EEL2_PATH}/asm-nseel-x64.obj")
    else()
        message(FATAL_ERROR "Current OS and architecture combination is not supported")
    endif()
elseif(LINUX)
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        # compile asm with nasm
        set(EEL2_ASM_SRC "${EEL2_PATH}/asm-nseel-x64-sse.asm")
        set(EEL2_ASM_OBJ "${CMAKE_BINARY_DIR}/asm-nseel-x64-sse.o")
        add_custom_command(
            OUTPUT ${EEL2_ASM_OBJ}
            DEPENDS ${EEL2_ASM_SRC}
            COMMAND nasm -D AMD64ABI -f elf64 "${EEL2_ASM_SRC}" -o "${EEL2_ASM_OBJ}"
            COMMENT "Converting EEL2 asm to object using nasm"
        )
        # use target_sources() to create a dependency on the .o file
        target_sources(eel2 PRIVATE "${EEL2_ASM_OBJ}")
    else()
        message(FATAL_ERROR "Current OS and architecture combination is not supported")
    endif()
else()
    message(FATAL_ERROR "This OS is not supported")
endif()

#--------------------- UGen ---------------------------#

# plugin extension
set(CMAKE_SHARED_MODULE_PREFIX "")
if(APPLE OR WIN32)
    set(CMAKE_SHARED_MODULE_SUFFIX ".scx")
endif()

# common properties for scsynth and Supernova UGen
add_library(DynGen_common INTERFACE)

target_sources(DynGen_common INTERFACE
        src/dyngen.h src/dyngen.cpp
        src/eel2_adapter.h src/eel2_adapter.cpp
        src/library.h src/library.cpp
)

target_include_directories(DynGen_common INTERFACE "${SC_PATH}/include/plugin_interface")
target_include_directories(DynGen_common INTERFACE "${SC_PATH}/include/common")

if(MSVC)
    target_compile_definitions(DynGen_common INTERFACE NOMINMAX)
endif()

target_compile_features(DynGen_common INTERFACE cxx_std_17)

target_link_libraries(DynGen_common INTERFACE eel2)

if(MINGW)
    # link with static runtime libraries
    target_link_options(DynGen_common INTERFACE
            -static-libstdc++
            -static-libgcc
    )
endif()

# the scsynth UGen
add_library(DynGen MODULE)

target_link_libraries(DynGen PRIVATE DynGen_common)

# the Supernova UGen
if (SUPERNOVA)
    add_library(DynGen_supernova MODULE)

    target_link_libraries(DynGen_supernova PRIVATE DynGen_common)

    set_target_properties(DynGen_supernova PROPERTIES
            OUTPUT_NAME "DynGen_supernova")
endif()

#--------------------- installation ---------------------------#

set(INSTALL_DIR "${SC_INSTALL_DIR}/${PROJECT_NAME}")

install(TARGETS DynGen LIBRARY DESTINATION "${INSTALL_DIR}/plugins")
if (MSVC)
    # MSVC debug symbols
    install(FILES $<TARGET_PDB_FILE:DynGen> OPTIONAL DESTINATION "${INSTALL_DIR}/plugins")
endif()

if (SUPERNOVA)
    install(TARGETS DynGen_supernova DESTINATION "${INSTALL_DIR}/plugins")
    if (MSVC)
        # MSVC debug symbols
        install(FILES $<TARGET_PDB_FILE:DynGen_supernova> OPTIONAL DESTINATION "${INSTALL_DIR}/plugins")
    endif()
endif()

install(DIRECTORY "src/Classes" DESTINATION "${INSTALL_DIR}")
install(DIRECTORY "src/HelpSource" DESTINATION "${INSTALL_DIR}")
install(FILES "README.md" "LICENSE" DESTINATION "${INSTALL_DIR}")
