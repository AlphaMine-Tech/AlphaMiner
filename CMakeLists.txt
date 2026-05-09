cmake_minimum_required(VERSION 3.21)
project(AlphaMiner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

if(CMAKE_BUILD_TYPE MATCHES DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
endif()

option(ENABLE_AVX512 "Enable AVX512 instruction set" OFF)
option(ENABLE_CUDA "Enable CUDA GPU mining" OFF)

# Additional cmake flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mrdrnd -mbmi -march=native -mtune=native -funroll-loops")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mrdrnd -mbmi")
    if(ENABLE_AVX512)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx512f")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
else()
    message(FATAL_ERROR "Unsupported compiler.")
endif()

if(ENABLE_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    # Prefer native detection on the build host; allows modern cards beyond a single fixed arch.
    if(NOT CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES native)
    endif()

    add_executable(${PROJECT_NAME}
        src/AlphaMiner.cpp
        src/keyUtils.cpp
        src/cuda/mining_kernel.cu
        src/cuda/gpu_miner.cu
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_CUDA)
    target_include_directories(${PROJECT_NAME} PRIVATE src src/cuda)

    # Pass host compiler flags to CUDA
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr -Xcompiler=-mrdrnd,-mbmi>
    )
else()
    add_executable(${PROJECT_NAME} src/AlphaMiner.cpp src/keyUtils.cpp)
    include_directories(${PROJECT_NAME} ./src)
endif()

# Linking
if(WIN32)
    target_link_libraries(${PROJECT_NAME} wsock32 ws2_32)
else()
    target_link_libraries(${PROJECT_NAME} pthread)
endif()
