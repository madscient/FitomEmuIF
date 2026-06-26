cmake_minimum_required(VERSION 3.18)
project(FitomEmuIF VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ════════════════════════════════════════════════════════════════════════════
#  依存: nlohmann_json
#  システムインストール版を優先し、なければ FetchContent で取得する。
# ════════════════════════════════════════════════════════════════════════════
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ════════════════════════════════════════════════════════════════════════════
#  依存: RtAudio (submodule: extern/rtaudio)
#
#  追加手順:
#    git submodule add https://github.com/thestk/rtaudio.git extern/rtaudio
#    git submodule update --init --recursive
# ════════════════════════════════════════════════════════════════════════════
set(RTAUDIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/rtaudio")
if(NOT EXISTS "${RTAUDIO_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "RtAudio submodule が見つかりません。\n"
        "  git submodule add https://github.com/thestk/rtaudio.git extern/rtaudio\n"
        "  git submodule update --init --recursive\n"
        "を実行してください。")
endif()

# RtAudio のビルドオプション
set(RTAUDIO_BUILD_STATIC_LIBS ON  CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_TESTING     OFF CACHE BOOL "" FORCE)
add_subdirectory(${RTAUDIO_DIR} extern/rtaudio EXCLUDE_FROM_ALL)

# ════════════════════════════════════════════════════════════════════════════
#  ターゲット: FitomEmuIF
# ════════════════════════════════════════════════════════════════════════════
add_library(FitomEmuIF SHARED
    src/FmHwIfImpl.cpp
)

target_compile_definitions(FitomEmuIF PRIVATE
    FITOM_HW_PLUGIN_EXPORTS
)

target_include_directories(FitomEmuIF PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include   # IHWPlugin.h, FmEngineApi.h
)

target_link_libraries(FitomEmuIF PRIVATE
    nlohmann_json::nlohmann_json
    rtaudio                               # RtAudio static lib (submodule)
)

# Linux: dlopen → libdl
if(UNIX AND NOT APPLE)
    target_link_libraries(FitomEmuIF PRIVATE dl)
endif()

# ── プラットフォーム別のオーディオバックエンドリンク ─────────────────────────
# RtAudio の CMake が target_link_libraries で自動設定するが、
# 一部環境では手動指定が必要なため明示する。
if(WIN32)
    target_link_libraries(FitomEmuIF PRIVATE ole32 winmm ksuser mfplat mfuuid)
elseif(APPLE)
    target_link_libraries(FitomEmuIF PRIVATE
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )
else()
    # ALSA は必須、PulseAudio / JACK はオプション
    find_package(ALSA REQUIRED)
    target_link_libraries(FitomEmuIF PRIVATE ALSA::ALSA)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PULSE   QUIET libpulse-simple)
        pkg_check_modules(JACK    QUIET jack)
        if(PULSE_FOUND)
            target_link_libraries(FitomEmuIF PRIVATE ${PULSE_LIBRARIES})
            target_include_directories(FitomEmuIF PRIVATE ${PULSE_INCLUDE_DIRS})
            target_compile_options(FitomEmuIF PRIVATE ${PULSE_CFLAGS_OTHER})
        endif()
        if(JACK_FOUND)
            target_link_libraries(FitomEmuIF PRIVATE ${JACK_LIBRARIES})
            target_include_directories(FitomEmuIF PRIVATE ${JACK_INCLUDE_DIRS})
        endif()
    endif()
endif()

# ════════════════════════════════════════════════════════════════════════════
#  インストール
# ════════════════════════════════════════════════════════════════════════════
install(TARGETS FitomEmuIF
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# ════════════════════════════════════════════════════════════════════════════
#  テスト（オプション）
# ════════════════════════════════════════════════════════════════════════════
option(BUILD_FITOMEMUIF_TEST "Build smoke test" OFF)
if(BUILD_FITOMEMUIF_TEST)
    add_executable(FitomEmuIF_test test/smoke_test.cpp)
    target_include_directories(FitomEmuIF_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(FitomEmuIF_test PRIVATE FitomEmuIF)
endif()
