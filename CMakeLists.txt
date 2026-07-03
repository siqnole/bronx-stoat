cmake_minimum_required(VERSION 3.16)
project(bronx CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(JSON_DIAGNOSTICS=1)

# Add the parent directory containing the stoatpp library
add_subdirectory(.. stoatpp_build)

add_executable(bronx main.cpp)

find_path(MARIADB_INCLUDE_DIR mysql.h
    PATHS
    /home/linuxbrew/.linuxbrew/opt/mariadb-connector-c/include/mariadb
    /usr/include/mariadb
    /usr/local/include/mariadb
)

find_library(MARIADB_LIBRARY NAMES mariadb mysqlclient
    PATHS
    /home/linuxbrew/.linuxbrew/opt/mariadb-connector-c/lib
    /usr/lib
    /usr/local/lib
)

if(NOT MARIADB_INCLUDE_DIR OR NOT MARIADB_LIBRARY)
    message(FATAL_ERROR "MariaDB client library (mysql.h or libmariadb) not found!")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(TESSERACT REQUIRED tesseract)
pkg_check_modules(LEPTONICA REQUIRED lept)

# Cairo chart renderer — search Homebrew paths first, then system
list(APPEND CMAKE_PREFIX_PATH /home/linuxbrew/.linuxbrew)
set(ENV{PKG_CONFIG_PATH} "/home/linuxbrew/.linuxbrew/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(CAIRO cairo)
if(CAIRO_FOUND)
    message(STATUS "Cairo ${CAIRO_VERSION} found — chart rendering enabled")
    target_compile_definitions(bronx PRIVATE BRONX_CHART_ENABLED=1)
    # Linuxbrew puts libs in a non-standard path; add it so ld can find libcairo.so
    target_link_directories(bronx PRIVATE ${CAIRO_LIBRARY_DIRS})
else()
    message(STATUS "Cairo not found — chart rendering disabled")
endif()

target_include_directories(bronx PRIVATE
    ${MARIADB_INCLUDE_DIR}
    ${TESSERACT_INCLUDE_DIRS}
    ${LEPTONICA_INCLUDE_DIRS}
    $<$<BOOL:${CAIRO_FOUND}>:${CAIRO_INCLUDE_DIRS}>
)

# Link against stoatpp, mariadb, tesseract, leptonica, and (optionally) Cairo
target_link_libraries(bronx PRIVATE
    stoatpp
    ${MARIADB_LIBRARY}
    ${TESSERACT_LIBRARIES}
    ${LEPTONICA_LIBRARIES}
    $<$<BOOL:${CAIRO_FOUND}>:${CAIRO_LIBRARIES}>
)
