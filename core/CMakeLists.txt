cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SyncAudioCore LANGUAGES CXX)
endif()

add_library(syncaudio_core STATIC
    src/ClockSync.cpp
    src/DriftCorrection.cpp
    src/JitterBuffer.cpp
    src/PacketSerializer.cpp
    src/PlaybackClock.cpp
    src/OutputLatency.cpp
    src/Room.cpp
)
target_include_directories(syncaudio_core PUBLIC include)
target_compile_features(syncaudio_core PUBLIC cxx_std_20)
