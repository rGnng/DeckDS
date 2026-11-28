cmake_minimum_required(VERSION 3.21)
project(DeckDSNative VERSION 0.1 LANGUAGES CXX)

find_package(SDL3 3.2 CONFIG REQUIRED)
find_package(SDL3_ttf 3.2 CONFIG REQUIRED)
find_package(SDL3_image 3.2 CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(PkgConfig QUIET)
find_package(SQLite3 QUIET)
find_program(NODEJS NAMES node nodejs REQUIRED)

get_filename_component(DESIGN "${CMAKE_CURRENT_SOURCE_DIR}/../Design" ABSOLUTE)
set(ASSET_HEADER "${CMAKE_CURRENT_BINARY_DIR}/design_assets.inc")
set(NATIVE_SVGS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/dials/vu-ceramic.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/dials/vu-obsidian.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/arrow-repeat.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/battery.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/close.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/wifi.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/wifi-off.svg"
)
add_custom_command(
    OUTPUT "${ASSET_HEADER}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/design_assets.py" "${DESIGN}" "${ASSET_HEADER}" "${NODEJS}" ${NATIVE_SVGS}
    DEPENDS tools/design_assets.py "${DESIGN}/index.html" "${DESIGN}/app.js" "${DESIGN}/vfd-icons.js" "${DESIGN}/audio-engine.js" ${NATIVE_SVGS}
    VERBATIM
)

add_executable(DeckDS MACOSX_BUNDLE
    src/main.cpp src/studio.cpp src/player.cpp src/draw.cpp src/stage.cpp src/deck_ui.cpp src/preview_state.cpp
    src/bluetooth.cpp src/media.cpp src/media_playback.cpp src/media_visuals.cpp
    src/web_config.cpp
    "${ASSET_HEADER}"
)
target_compile_features(DeckDS PRIVATE cxx_std_20)
target_compile_options(DeckDS PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(DeckDS PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DeckDS PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)
if(PkgConfig_FOUND)
    pkg_check_modules(MICROHTTPD QUIET IMPORTED_TARGET libmicrohttpd>=0.9.71)
    if(MICROHTTPD_FOUND AND (APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
        find_package(OpenSSL REQUIRED COMPONENTS Crypto)
        set(WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${WEB_DIR}/index.html" "${WEB_DIR}/config.css" "${WEB_DIR}/config.js"
            "${WEB_DIR}/debug.html" "${WEB_DIR}/debug.js" "${WEB_DIR}/login.html" "${WEB_DIR}/login.js"
            "${WEB_DIR}/vendor/bootstrap.min.css" "${DESIGN}/deckds-logo.svg")
        file(READ "${WEB_DIR}/index.html" DECKDS_WEB_HTML)
        file(READ "${WEB_DIR}/config.css" DECKDS_WEB_CSS)
        file(READ "${WEB_DIR}/config.js" DECKDS_WEB_JS)
        file(READ "${WEB_DIR}/debug.html" DECKDS_DEBUG_HTML)
        file(READ "${WEB_DIR}/debug.js" DECKDS_DEBUG_JS)
        file(READ "${WEB_DIR}/login.html" DECKDS_LOGIN_HTML)
        file(READ "${WEB_DIR}/login.js" DECKDS_LOGIN_JS)
        file(READ "${WEB_DIR}/vendor/bootstrap.min.css" DECKDS_WEB_BOOTSTRAP HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," DECKDS_WEB_BOOTSTRAP "${DECKDS_WEB_BOOTSTRAP}")
        file(READ "${DESIGN}/deckds-logo.svg" DECKDS_WEB_LOGO)
        configure_file(src/web_assets.inc.in "${CMAKE_CURRENT_BINARY_DIR}/web_assets.inc" @ONLY)
        target_sources(DeckDS PRIVATE src/web_password.cpp)
        target_link_libraries(DeckDS PRIVATE PkgConfig::MICROHTTPD OpenSSL::Crypto)
        target_compile_definitions(DeckDS PRIVATE DECKDS_WEB_CONFIG)
    endif()
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavformat>=59 libavcodec>=59 libavutil>=57 libswresample>=4)
    if(FFMPEG_FOUND AND SQLite3_FOUND)
        target_include_directories(DeckDS PRIVATE ${SQLite3_INCLUDE_DIRS})
        target_link_libraries(DeckDS PRIVATE PkgConfig::FFMPEG ${SQLite3_LIBRARIES})
        target_compile_definitions(DeckDS PRIVATE DECKDS_FFMPEG)
    endif()
endif()
set_target_properties(DeckDS PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.deckds.native
    MACOSX_BUNDLE_BUNDLE_NAME DeckDS
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

file(GLOB DECK_FONTS CONFIGURE_DEPENDS "${DESIGN}/fonts/*.ttf" "${DESIGN}/fonts/*.txt")
if(APPLE)
    enable_language(OBJCXX)
    set_target_properties(DeckDS PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
    find_library(COREWLAN_FRAMEWORK CoreWLAN REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    set_source_files_properties(${DECK_FONTS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/fonts)
    target_sources(DeckDS PRIVATE src/wifi_macos.mm ${DECK_FONTS})
    target_link_libraries(DeckDS PRIVATE "${COREWLAN_FRAMEWORK}" "${FOUNDATION_FRAMEWORK}")
else()
    target_sources(DeckDS PRIVATE src/wifi.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PkgConfig_FOUND)
        pkg_check_modules(GIO QUIET IMPORTED_TARGET gio-2.0)
        if(GIO_FOUND)
            target_link_libraries(DeckDS PRIVATE PkgConfig::GIO)
            target_compile_definitions(DeckDS PRIVATE DECKDS_BLUEZ DECKDS_NETWORK_MANAGER)
        endif()
    endif()
    foreach(font IN LISTS DECK_FONTS)
        get_filename_component(name "${font}" NAME)
        configure_file("${font}" "${CMAKE_CURRENT_BINARY_DIR}/fonts/${name}" COPYONLY)
    endforeach()
endif()
