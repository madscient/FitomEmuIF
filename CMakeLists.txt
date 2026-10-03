cmake_minimum_required(VERSION 3.18)
project(FitomEmuIF VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ════════════════════════════════════════════════════════════════════════════
#  VS2026 (Visual Studio 18 2026) / CMake 4.x 対応
#
#  VS2026 は CMake 4.2 以降を同梱する。CMake 4.0 で
#  cmake_minimum_required(VERSION < 3.5) を宣言する古いプロジェクトの
#  configure がエラーになるポリシーが導入された
#  ("Compatibility with CMake < 3.5 has been removed")。
#
#  本プロジェクト自体は 3.18 を要求しており問題ないが、submodule の
#  RtAudio (extern/rtaudio) は cmake_minimum_required(VERSION 3.0) を
#  宣言しているため、CMake 4.x で configure が失敗する。
#  CMAKE_POLICY_VERSION_MINIMUM を RtAudio の add_subdirectory 直前だけ
#  一時的に設定して回避する（自プロジェクトのポリシーには影響させない）。
#
#  参考: https://cmake.org/cmake/help/latest/policy/CMP0000.html
# ════════════════════════════════════════════════════════════════════════════

# MSVC ランタイムライブラリを明示する。
# toolset v145 (VS2026 既定) は CMP0091 の既定値が変わる可能性があるため、
# FitomEmuIF・RtAudio 間でランタイムが食い違い LNK2038 が出ないよう固定する。
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

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

# RtAudio 自体の cmake_minimum_required(VERSION 3.0) が CMake 4.x で
# configure エラーになるため、add_subdirectory 中だけポリシー下限を緩める。
# CMAKE_POLICY_VERSION_MINIMUM が未設定の CMake < 3.31 環境では単に無視される。
set(_fitom_saved_policy_min "${CMAKE_POLICY_VERSION_MINIMUM}")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory(${RTAUDIO_DIR} extern/rtaudio EXCLUDE_FROM_ALL)
set(CMAKE_POLICY_VERSION_MINIMUM "${_fitom_saved_policy_min}")
unset(_fitom_saved_policy_min)

# ════════════════════════════════════════════════════════════════════════════
#  ターゲット: FitomEmuIF
# ════════════════════════════════════════════════════════════════════════════
add_library(FitomEmuIF SHARED
    src/FmEmuIfImpl.cpp
)

target_compile_definitions(FitomEmuIF PRIVATE
    FITOM_HW_PLUGIN_EXPORTS
)

target_include_directories(FitomEmuIF PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src       # fitom/IHWPlugin.h, fitom/FmEngineApi.h
)

target_link_libraries(FitomEmuIF PRIVATE
    nlohmann_json::nlohmann_json
    rtaudio                               # RtAudio static lib (submodule)
)

# MSVC: ソースファイルを UTF-8 として扱う（日本語コメントのビルドエラー対策）
if(MSVC)
    target_compile_options(FitomEmuIF PRIVATE /utf-8)
endif()

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
option(BUILD_FITOMEMUIF_TEST "Build tests" OFF)
if(BUILD_FITOMEMUIF_TEST)
    enable_testing()

    # ── スモークテスト ───────────────────────────────────────────────────────
    add_executable(FitomEmuIF_test test/smoke_test.cpp)
    target_include_directories(FitomEmuIF_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(FitomEmuIF_test PRIVATE FitomEmuIF)
    # smoke_test.cpp は assert で判定する。Release 構成の NDEBUG を外さないと
    # 判定がすべて消え、何も確かめないまま成功する。
    if(MSVC)
        target_compile_options(FitomEmuIF_test PRIVATE /utf-8 /UNDEBUG)
    else()
        target_compile_options(FitomEmuIF_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME smoke COMMAND FitomEmuIF_test)

    # ── 検証用エンジン（test/stub_engine.cpp を定義で作り分ける）────────────
    # FitomEmuIF はエンジン DLL を自分と同じディレクトリから探すので、出力先は変えない。
    function(fitomemuif_add_stub_engine name)
        add_library(${name} MODULE test/stub_engine.cpp)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_compile_definitions(${name} PRIVATE ${ARGN})
        if(MSVC)
            target_compile_options(${name} PRIVATE /utf-8)
        endif()
    endfunction()
    fitomemuif_add_stub_engine(StubEngine)
    fitomemuif_add_stub_engine(StubEngineLegacy       STUB_LEGACY)
    fitomemuif_add_stub_engine(StubEngineNoPartName   STUB_OMIT_GETPARTNAME)
    fitomemuif_add_stub_engine(StubEngineNoMemoryName STUB_OMIT_GETMEMORYNAME)

    # ── 部位ゲインと外部メモリの中継テスト ───────────────────────────────────
    add_executable(FitomEmuIF_relay_test test/engine_relay_test.cpp)
    target_include_directories(FitomEmuIF_relay_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(FitomEmuIF_relay_test PRIVATE FitomEmuIF)
    if(MSVC)
        target_compile_options(FitomEmuIF_relay_test PRIVATE /utf-8)
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(FitomEmuIF_relay_test PRIVATE dl)
    endif()
    add_dependencies(FitomEmuIF_relay_test
        StubEngine StubEngineLegacy StubEngineNoPartName StubEngineNoMemoryName)

    add_test(NAME relay_current
        COMMAND FitomEmuIF_relay_test current $<TARGET_FILE:StubEngine>)
    add_test(NAME relay_legacy
        COMMAND FitomEmuIF_relay_test legacy $<TARGET_FILE:StubEngineLegacy>)
    add_test(NAME relay_no_part_name
        COMMAND FitomEmuIF_relay_test incomplete $<TARGET_FILE:StubEngineNoPartName>)
    add_test(NAME relay_no_memory_name
        COMMAND FitomEmuIF_relay_test incomplete $<TARGET_FILE:StubEngineNoMemoryName>)
endif()
