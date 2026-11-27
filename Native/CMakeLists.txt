cmake_minimum_required(VERSION 3.21)
project(DeckDSNative VERSION 0.1 LANGUAGES CXX)

find_package(SDL3 3.2 CONFIG REQUIRED)
find_package(SDL3_ttf 3.2 CONFIG REQUIRED)
find_package(SDL3_image 3.2 CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(NODEJS NAMES node nodejs REQUIRED)

get_filename_component(DESIGN "${CMAKE_CURRENT_SOURCE_DIR}/../Design" ABSOLUTE)
set(ASSET_HEADER "${CMAKE_CURRENT_BINARY_DIR}/design_assets.inc")
set(NATIVE_ICONS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/battery.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/close.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/wifi.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/wifi-off.svg"
)
add_custom_command(
    OUTPUT "${ASSET_HEADER}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/design_assets.py" "${DESIGN}" "${ASSET_HEADER}" "${NODEJS}" ${NATIVE_ICONS}
    DEPENDS tools/design_assets.py "${DESIGN}/index.html" "${DESIGN}/app.js" "${DESIGN}/vfd-icons.js" "${DESIGN}/audio-engine.js" ${NATIVE_ICONS}
    VERBATIM
)

add_executable(DeckDS MACOSX_BUNDLE
    src/main.cpp src/studio.cpp src/player.cpp src/draw.cpp src/stage.cpp src/deck_ui.cpp src/preview_state.cpp
    src/bluetooth.cpp
    "${ASSET_HEADER}"
)
target_compile_features(DeckDS PRIVATE cxx_std_20)
target_compile_options(DeckDS PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(DeckDS PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DeckDS PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)
set_target_properties(DeckDS PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.deckds.native
    MACOSX_BUNDLE_BUNDLE_NAME DeckDS
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

file(GLOB DECK_FONTS CONFIGURE_DEPENDS "${DESIGN}/fonts/*.ttf" "${DESIGN}/fonts/*.txt")
if(APPLE)
    enable_language(OBJCXX)
    find_library(COREWLAN_FRAMEWORK CoreWLAN REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    set_source_files_properties(${DECK_FONTS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/fonts)
    target_sources(DeckDS PRIVATE src/wifi_macos.mm ${DECK_FONTS})
    target_link_libraries(DeckDS PRIVATE "${COREWLAN_FRAMEWORK}" "${FOUNDATION_FRAMEWORK}")
else()
    target_sources(DeckDS PRIVATE src/wifi.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(GIO QUIET IMPORTED_TARGET gio-2.0)
            if(GIO_FOUND)
                target_link_libraries(DeckDS PRIVATE PkgConfig::GIO)
                target_compile_definitions(DeckDS PRIVATE DECKDS_BLUEZ)
            endif()
        endif()
    endif()
    foreach(font IN LISTS DECK_FONTS)
        get_filename_component(name "${font}" NAME)
        configure_file("${font}" "${CMAKE_CURRENT_BINARY_DIR}/fonts/${name}" COPYONLY)
    endforeach()
endif()
