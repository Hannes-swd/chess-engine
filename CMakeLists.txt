cmake_minimum_required(VERSION 3.20)
project(ChessEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CHESS_BUILD_GUI "Grafische Oberflaeche (SDL2 + Dear ImGui) bauen" ON)

# Strenge Warnungen nur fuer unseren eigenen Code, nicht fuer third_party
function(chess_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Kern: Regeln, Brett, Figuren, Hooks. Keine UI-, Lua- oder Engine-Abhaengigkeit.
# ---------------------------------------------------------------------------
add_library(chess_core STATIC
    src/core/piece_type.cpp
    src/core/position.cpp
    src/core/fen.cpp
    src/core/notation.cpp
    src/core/game.cpp
    src/core/pgn.cpp
    src/core/clock.cpp
    src/core/tactics.cpp
    src/core/openings.cpp
)
target_include_directories(chess_core PUBLIC src)
chess_warnings(chess_core)

# ---------------------------------------------------------------------------
# Engines (eingebaut + UCI/Stockfish), Bots, Analyse
# ---------------------------------------------------------------------------
add_library(chess_engine STATIC
    src/util/json.cpp
    src/engine/score.cpp
    src/engine/engine.cpp
    src/engine/builtin_engine.cpp
    src/engine/process.cpp
    src/engine/uci_engine.cpp
    src/engine/bot.cpp
    src/engine/rating.cpp
    src/engine/training.cpp
    src/analysis/analysis.cpp
    src/app/environment.cpp
)
target_link_libraries(chess_engine PUBLIC chess_core)
chess_warnings(chess_engine)

# ---------------------------------------------------------------------------
# Lua 5.4 (fuer Plugins) und die Plugin-Schicht
# ---------------------------------------------------------------------------
file(GLOB LUA_SOURCES ${CMAKE_SOURCE_DIR}/third_party/lua/src/*.c)
list(REMOVE_ITEM LUA_SOURCES ${CMAKE_SOURCE_DIR}/third_party/lua/src/lua.c ${CMAKE_SOURCE_DIR}/third_party/lua/src/luac.c)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC third_party/lua/src)
set_target_properties(lua PROPERTIES LINKER_LANGUAGE C)

add_library(chess_plugins STATIC src/plugins/lua_plugin.cpp)
target_link_libraries(chess_plugins PUBLIC chess_engine lua)
chess_warnings(chess_plugins)
# Konsolen-Version
add_executable(chessengine_cli src/app/main.cpp)
target_link_libraries(chessengine_cli PRIVATE chess_engine)
chess_warnings(chessengine_cli)
add_custom_command(TARGET chessengine_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:chessengine_cli>/bots
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/bots $<TARGET_FILE_DIR:chessengine_cli>/bots)

# Die eingebaute Engine als UCI-Programm (fuer andere GUIs und fuer Tests)
add_executable(chessengine_uci src/app/uci_main.cpp)
target_link_libraries(chessengine_uci PRIVATE chess_engine)
chess_warnings(chessengine_uci)

# ---------------------------------------------------------------------------
# Grafische Oberflaeche
# ---------------------------------------------------------------------------
if(CHESS_BUILD_GUI)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/SDL2 EXCLUDE_FROM_ALL)

    set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
    )
    target_include_directories(imgui PUBLIC ${IMGUI_DIR})
    target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
    target_link_libraries(imgui PUBLIC SDL2::SDL2-static)

    add_executable(chessengine_gui WIN32
        src/app/gui_main.cpp
        src/ui/theme.cpp
        src/ui/board_view.cpp
        src/ui/settings.cpp
        src/ui/gui_app.cpp
        src/ui/layout.cpp
        src/ui/analysis_panel.cpp
        src/ui/games_panel.cpp
        src/ui/sound.cpp
        src/ui/training_panel.cpp
        src/ui/stats_panel.cpp
        src/ui/plugins_panel.cpp
        src/ui/feel.cpp
        src/ui/image_cache.cpp
        src/ui/i18n.cpp
        src/ui/home_panel.cpp
    )
    target_link_libraries(chessengine_gui PRIVATE chess_plugins imgui SDL2::SDL2main SDL2::SDL2-static windowscodecs ole32)
    chess_warnings(chessengine_gui)
    add_custom_command(TARGET chessengine_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:chessengine_gui>/bots
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/bots $<TARGET_FILE_DIR:chessengine_gui>/bots
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:chessengine_gui>/assets
        COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:chessengine_gui>/plugins
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:chessengine_gui>/plugins)

    # Brett-Bedienung headless testen (simulierte Maus, kein Fenster)
    add_executable(chess_ui_tests
        tests/test_main.cpp
        tests/ui_board_tests.cpp
        src/ui/theme.cpp
        src/ui/board_view.cpp
        src/ui/settings.cpp
        src/ui/i18n.cpp
    )
    target_link_libraries(chess_ui_tests PRIVATE chess_engine imgui)
    chess_warnings(chess_ui_tests)
    add_test(NAME chess_ui_tests COMMAND chess_ui_tests)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_executable(chess_tests
    tests/test_main.cpp
    tests/perft_tests.cpp
    tests/rules_tests.cpp
    tests/variant_tests.cpp
    tests/json_tests.cpp
    tests/engine_tests.cpp
    tests/bot_tests.cpp
    tests/analysis_tests.cpp
    tests/uci_tests.cpp
    tests/clock_tests.cpp
    tests/rating_tests.cpp
    tests/plugin_tests.cpp
    tests/training_tests.cpp
)
target_link_libraries(chess_tests PRIVATE chess_plugins)
chess_warnings(chess_tests)
target_compile_definitions(chess_tests PRIVATE
    CHESS_UCI_EXE="$<TARGET_FILE:chessengine_uci>"
    CHESS_BOTS_DIR="${CMAKE_SOURCE_DIR}/bots"
    CHESS_PLUGINS_DIR="${CMAKE_SOURCE_DIR}/plugins")
add_dependencies(chess_tests chessengine_uci)
add_test(NAME chess_tests COMMAND chess_tests)

# ---------------------------------------------------------------------------
# Weitergeben: "cmake --build build --config Release --target package" erzeugt ein ZIP
# mit Programm, Bots, Plugins, Schriften und Anleitung. Stockfish (GPL) ist nicht enthalten.
# ---------------------------------------------------------------------------
if(CHESS_BUILD_GUI)
    install(TARGETS chessengine_gui chessengine_cli chessengine_uci RUNTIME DESTINATION .)
    install(DIRECTORY bots assets plugins DESTINATION .)
    install(FILES README.md LICENSE THIRD_PARTY.md DESTINATION .)
    install(FILES docs/PLUGINS.md DESTINATION docs)
    install(FILES third_party/lua/LICENSE DESTINATION licenses RENAME Lua.txt)
    install(FILES third_party/imgui/LICENSE.txt DESTINATION licenses RENAME DearImGui.txt)
    install(FILES third_party/SDL2/LICENSE.txt DESTINATION licenses RENAME SDL2.txt)
    install(FILES packaging/engines-LIESMICH.txt DESTINATION engines RENAME LIESMICH.txt)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_NAME ChessEngine)
    set(CPACK_PACKAGE_VERSION 1.0.0)
    set(CPACK_PACKAGE_FILE_NAME "ChessEngine-${CPACK_PACKAGE_VERSION}-win64")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    include(CPack)
endif()