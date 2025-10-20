cmake_minimum_required(VERSION 4.0)
project(DynGen)
set(CMAKE_CXX_STANDARD 17)

# WDL/EEL2
set(WDL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/WDL/WDL")
set(EEL2_PATH "${WDL_PATH}/eel2")

include_directories(${WDL_PATH} ${EEL2_PATH})

set(EEL2_SOURCES
        ${EEL2_PATH}/nseel-caltab.c
        ${EEL2_PATH}/nseel-compiler.c
        ${EEL2_PATH}/nseel-eval.c
        ${EEL2_PATH}/nseel-lextab.c
        ${EEL2_PATH}/nseel-ram.c
        ${EEL2_PATH}/nseel-yylex.c
        ${EEL2_PATH}/nseel-cfunc.c
#        ${EEL2_PATH}/eel_fft.c
)

add_library(eel2 STATIC ${EEL2_SOURCES})
target_include_directories(eel2 PUBLIC ${EEL2_PATH} ${WDL_PATH})

target_compile_definitions(eel2 PRIVATE
        WDL_FFT_REALSIZE=8
        _FILE_OFFSET_BITS=64
)
target_compile_options(eel2 PRIVATE
        -Wall -Wno-unused-function -Wno-multichar
        -Wno-unused-result -Wshadow -Wtype-limits
        -fsigned-char
)
target_compile_features(eel2 PUBLIC cxx_std_17)

# UGen
if(APPLE OR WIN32)
    set(CMAKE_SHARED_MODULE_SUFFIX ".scx")
else()
    set(CMAKE_SHARED_MODULE_PREFIX "")
endif()

# static?
add_library(DynGen MODULE
        src/dyngen.h
        src/dyngen.cpp
        src/eel2_adapter.h
        src/eel2_adapter.cpp
)

target_link_libraries(DynGen PRIVATE eel2)

#set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")

target_include_directories(DynGen PUBLIC ${SC_SRC_PATH}/include/plugin_interface)
target_include_directories(DynGen PUBLIC ${SC_SRC_PATH}/include/common)
target_include_directories(DynGen PUBLIC ${SC_SRC_PATH}/common)

install(TARGETS DynGen LIBRARY DESTINATION ${PROJECT_NAME})

file(GLOB_RECURSE SC_FILES "${CMAKE_SOURCE_DIR}/src/Classes/*.sc")
install(FILES ${SC_FILES} DESTINATION ${PROJECT_NAME}/Classes)

file(GLOB_RECURSE SCHELP_FILES "${CMAKE_SOURCE_DIR}/src/HelpSource/*.schelp")
install(FILES ${SCHELP_FILES} DESTINATION ${PROJECT_NAME}/HelpSource)
