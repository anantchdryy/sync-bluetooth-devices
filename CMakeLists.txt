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

add_subdirectory(core)

add_library(desktop_transport STATIC
    desktop/src/ClockSyncTransport.cpp
    desktop/src/ControlChannel.cpp
    desktop/src/DiscoveryService.cpp
    desktop/src/UdpAudioReceiver.cpp
    desktop/src/UdpAudioSender.cpp
)
target_include_directories(desktop_transport PUBLIC desktop/include)
target_include_directories(desktop_transport SYSTEM PRIVATE desktop/third_party)
target_link_libraries(desktop_transport PUBLIC syncaudio_core)
target_compile_features(desktop_transport PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(desktop_transport PRIVATE ws2_32)
endif()

add_library(desktop_audio_player STATIC
    desktop/src/DesktopAudioPlayer.cpp
)
target_include_directories(desktop_audio_player PUBLIC desktop/include)
target_link_libraries(desktop_audio_player
    PUBLIC syncaudio_core
    PRIVATE miniaudio_backend
)
target_compile_features(desktop_audio_player PUBLIC cxx_std_20)

add_library(desktop_network_host STATIC
    desktop/src/DesktopNetworkHost.cpp
    desktop/src/NetworkImpairment.cpp)
target_include_directories(desktop_network_host PUBLIC desktop/include)
target_link_libraries(desktop_network_host
    PUBLIC syncaudio_core desktop_transport
    PRIVATE miniaudio_backend
)
target_compile_features(desktop_network_host PUBLIC cxx_std_20)

add_library(desktop_audio_client STATIC desktop/src/DesktopAudioClient.cpp)
target_include_directories(desktop_audio_client PUBLIC desktop/include)
target_link_libraries(desktop_audio_client
    PUBLIC syncaudio_core desktop_transport
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
enable_project_warnings(syncaudio_core)
enable_project_warnings(desktop_transport)
enable_project_warnings(desktop_network_host)
enable_project_warnings(desktop_audio_client)
enable_project_warnings(syncaudio)

if(BUILD_TESTING)
    add_executable(control_channel_tests tests/ControlChannelTests.cpp)
    target_link_libraries(control_channel_tests PRIVATE desktop_transport)
    enable_project_warnings(control_channel_tests)
    add_test(NAME control_channel_tests COMMAND control_channel_tests)

    add_executable(desktop_transport_tests tests/DesktopTransportTests.cpp)
    target_link_libraries(desktop_transport_tests PRIVATE desktop_transport)
    enable_project_warnings(desktop_transport_tests)
    add_test(NAME desktop_transport_tests COMMAND desktop_transport_tests)

    add_executable(clock_sync_core_tests tests/ClockSyncCoreTests.cpp)
    target_link_libraries(clock_sync_core_tests PRIVATE syncaudio_core)
    enable_project_warnings(clock_sync_core_tests)
    add_test(NAME clock_sync_core_tests COMMAND clock_sync_core_tests)

    add_executable(drift_correction_tests tests/DriftCorrectionTests.cpp)
    target_link_libraries(drift_correction_tests PRIVATE syncaudio_core)
    enable_project_warnings(drift_correction_tests)
    add_test(NAME drift_correction_tests COMMAND drift_correction_tests)

    add_executable(clock_sync_tests tests/ClockSyncTests.cpp)
    target_link_libraries(clock_sync_tests PRIVATE desktop_transport)
    enable_project_warnings(clock_sync_tests)
    add_test(NAME clock_sync_tests COMMAND clock_sync_tests)

    add_executable(jitter_buffer_tests tests/JitterBufferTests.cpp)
    target_link_libraries(jitter_buffer_tests PRIVATE syncaudio_core)
    enable_project_warnings(jitter_buffer_tests)
    add_test(NAME jitter_buffer_tests COMMAND jitter_buffer_tests)

    add_executable(packet_serializer_tests tests/PacketSerializerTests.cpp)
    target_link_libraries(packet_serializer_tests PRIVATE syncaudio_core)
    enable_project_warnings(packet_serializer_tests)
    add_test(NAME packet_serializer_tests COMMAND packet_serializer_tests)

    add_executable(playback_clock_tests tests/PlaybackClockTests.cpp)
    target_link_libraries(playback_clock_tests PRIVATE syncaudio_core)
    enable_project_warnings(playback_clock_tests)
    add_test(NAME playback_clock_tests COMMAND playback_clock_tests)

    add_executable(desktop_audio_player_tests
        tests/DesktopAudioPlayerTests.cpp
    )
    target_link_libraries(desktop_audio_player_tests PRIVATE desktop_audio_player)
    enable_project_warnings(desktop_audio_player_tests)
    add_test(NAME desktop_audio_player_tests COMMAND desktop_audio_player_tests)

    add_executable(network_impairment_tests tests/NetworkImpairmentTests.cpp)
    target_link_libraries(network_impairment_tests PRIVATE desktop_network_host)
    enable_project_warnings(network_impairment_tests)
    add_test(NAME network_impairment_tests COMMAND network_impairment_tests)

    add_executable(reliability_stress_tests tests/ReliabilityStressTests.cpp)
    target_link_libraries(reliability_stress_tests PRIVATE syncaudio_core)
    enable_project_warnings(reliability_stress_tests)
    add_test(NAME reliability_stress_tests COMMAND reliability_stress_tests)

    add_executable(output_latency_tests tests/OutputLatencyTests.cpp)
    target_link_libraries(output_latency_tests PRIVATE syncaudio_core)
    enable_project_warnings(output_latency_tests)
    add_test(NAME output_latency_tests COMMAND output_latency_tests)
endif()
