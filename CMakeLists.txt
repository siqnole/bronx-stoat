cmake_minimum_required(VERSION 3.16)
project(bronx CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

target_include_directories(bronx PRIVATE ${MARIADB_INCLUDE_DIR} ${TESSERACT_INCLUDE_DIRS} ${LEPTONICA_INCLUDE_DIRS})

# Link against the stoatpp, mariadb, tesseract, and leptonica libraries
target_link_libraries(bronx PRIVATE stoatpp ${MARIADB_LIBRARY} ${TESSERACT_LIBRARIES} ${LEPTONICA_LIBRARIES})
