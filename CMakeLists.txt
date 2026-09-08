cmake_minimum_required(VERSION 3.16)
project(AllPurposeDownloader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_executable(downloader
    src/main.cpp
    src/DownloadEngine.cpp
    src/DownloadTask.cpp
    src/LocalServer.cpp
    src/MainWindow.cpp
    src/DownloadRowWidget.cpp
)

target_include_directories(downloader PRIVATE
    src
    third_party
)

target_link_libraries(downloader PRIVATE
    Qt6::Widgets
    Qt6::Network
    CURL::libcurl
    Threads::Threads
)

if(WIN32)
    target_link_libraries(downloader PRIVATE ws2_32 wldap32 crypt32 normaliz)
endif()
