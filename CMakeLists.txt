cmake_minimum_required(VERSION 3.20)

project(SyncAudio VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

FetchContent_Declare(
    miniaudio
    URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.tar.gz
    URL_HASH SHA256=b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _not_used
)
FetchContent_MakeAvailable(miniaudio)

add_library(miniaudio_backend STATIC desktop/src/miniaudio_impl.c)
target_include_directories(miniaudio_backend SYSTEM PUBLIC "${miniaudio_SOURCE_DIR}")

if(UNIX AND NOT APPLE)
    target_link_libraries(miniaudio_backend PUBLIC dl m pthread)
endif()

add_library(desktop_audio_player STATIC
    desktop/src/DesktopAudioPlayer.cpp
)
target_include_directories(desktop_audio_player PUBLIC desktop/include)
target_link_libraries(desktop_audio_player PRIVATE miniaudio_backend)
target_compile_features(desktop_audio_player PUBLIC cxx_std_20)

add_executable(syncaudio desktop/src/main.cpp)
target_link_libraries(syncaudio PRIVATE desktop_audio_player)

function(enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

enable_project_warnings(desktop_audio_player)
enable_project_warnings(syncaudio)

if(BUILD_TESTING)
    add_executable(desktop_audio_player_tests
        tests/DesktopAudioPlayerTests.cpp
    )
    target_link_libraries(desktop_audio_player_tests PRIVATE desktop_audio_player)
    enable_project_warnings(desktop_audio_player_tests)
    add_test(NAME desktop_audio_player_tests COMMAND desktop_audio_player_tests)
endif()
