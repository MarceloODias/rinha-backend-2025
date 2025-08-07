cmake_minimum_required(VERSION 3.22)
project(payments-service)

# Set the default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Print the build type
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set compiler flags for release builds
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto -DNDEBUG")

# Add stripping to release builds
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")

# Enable Link-Time Optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT supported OUTPUT output)
    if(supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "LTO not supported: ${output}")
    endif()
endif()

include_directories(include)
link_directories(/usr/local/lib)
include_directories(/usr/local/include)
include_directories(/usr/local/bin)

if(DEFINED ENV{DOCKER_ENV})
    message(STATUS "Configuring for Docker environment")

else()
    message(STATUS "Configuring for local environment")
    # Include Restbed headers and libraries for local development
    include_directories(/Users/admin/C/restbed/distribution/include)
    link_directories(/Users/admin/C/restbed/distribution/library)
endif()


add_executable(payments-service src/main.cpp)

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

target_link_libraries(payments-service PRIVATE
        restbed
        rocksdb
        z
        bz2
        snappy
        lz4
        zstd
        gflags
        Boost::system
        Threads::Threads)
