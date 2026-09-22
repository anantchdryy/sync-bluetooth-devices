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

add_library(playback_clock STATIC core/src/PlaybackClock.cpp)
target_include_directories(playback_clock PUBLIC core/include)
target_compile_features(playback_clock PUBLIC cxx_std_20)

add_library(network_audio STATIC
    network/src/ClockSync.cpp
    network/src/PacketSerializer.cpp
    network/src/UdpAudioReceiver.cpp
    network/src/UdpAudioSender.cpp
)
target_include_directories(network_audio PUBLIC network/include)
target_compile_features(network_audio PUBLIC cxx_std_20)
target_link_libraries(network_audio PUBLIC playback_clock)
if(WIN32)
    target_link_libraries(network_audio PRIVATE ws2_32)
endif()

add_library(jitter_buffer STATIC core/src/JitterBuffer.cpp)
target_include_directories(jitter_buffer PUBLIC core/include)
target_link_libraries(jitter_buffer PUBLIC network_audio)
target_compile_features(jitter_buffer PUBLIC cxx_std_20)

add_library(desktop_audio_player STATIC
    desktop/src/DesktopAudioPlayer.cpp
)
target_include_directories(desktop_audio_player PUBLIC desktop/include)
target_link_libraries(desktop_audio_player
    PUBLIC playback_clock
    PRIVATE miniaudio_backend
)
target_compile_features(desktop_audio_player PUBLIC cxx_std_20)

add_library(desktop_network_host STATIC desktop/src/DesktopNetworkHost.cpp)
target_include_directories(desktop_network_host PUBLIC desktop/include)
target_link_libraries(desktop_network_host
    PUBLIC playback_clock network_audio
    PRIVATE miniaudio_backend
)
target_compile_features(desktop_network_host PUBLIC cxx_std_20)

add_library(desktop_audio_client STATIC desktop/src/DesktopAudioClient.cpp)
target_include_directories(desktop_audio_client PUBLIC desktop/include)
target_link_libraries(desktop_audio_client
    PUBLIC jitter_buffer network_audio playback_clock
    PRIVATE miniaudio_backend
)
target_compile_features(desktop_audio_client PUBLIC cxx_std_20)

add_executable(syncaudio desktop/src/main.cpp)
target_link_libraries(syncaudio PRIVATE
    desktop_audio_client
    desktop_audio_player
    desktop_network_host
)

function(enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

enable_project_warnings(desktop_audio_player)
enable_project_warnings(playback_clock)
enable_project_warnings(network_audio)
enable_project_warnings(jitter_buffer)
enable_project_warnings(desktop_network_host)
enable_project_warnings(desktop_audio_client)
enable_project_warnings(syncaudio)

if(BUILD_TESTING)
    add_executable(clock_sync_tests tests/ClockSyncTests.cpp)
    target_link_libraries(clock_sync_tests PRIVATE network_audio)
    enable_project_warnings(clock_sync_tests)
    add_test(NAME clock_sync_tests COMMAND clock_sync_tests)

    add_executable(jitter_buffer_tests tests/JitterBufferTests.cpp)
    target_link_libraries(jitter_buffer_tests PRIVATE jitter_buffer)
    enable_project_warnings(jitter_buffer_tests)
    add_test(NAME jitter_buffer_tests COMMAND jitter_buffer_tests)

    add_executable(packet_serializer_tests tests/PacketSerializerTests.cpp)
    target_link_libraries(packet_serializer_tests PRIVATE network_audio)
    enable_project_warnings(packet_serializer_tests)
    add_test(NAME packet_serializer_tests COMMAND packet_serializer_tests)

    add_executable(playback_clock_tests tests/PlaybackClockTests.cpp)
    target_link_libraries(playback_clock_tests PRIVATE playback_clock)
    enable_project_warnings(playback_clock_tests)
    add_test(NAME playback_clock_tests COMMAND playback_clock_tests)

    add_executable(desktop_audio_player_tests
        tests/DesktopAudioPlayerTests.cpp
    )
    target_link_libraries(desktop_audio_player_tests PRIVATE desktop_audio_player)
    enable_project_warnings(desktop_audio_player_tests)
    add_test(NAME desktop_audio_player_tests COMMAND desktop_audio_player_tests)
endif()
