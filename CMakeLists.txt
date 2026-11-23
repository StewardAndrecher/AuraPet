cmake_minimum_required(VERSION 3.21)
project(AuraPet LANGUAGES CXX)

option(GIFPET_START_WITH_KAIMAO "Start with the bundled Kaimao when no asset is specified" OFF)

set(AURAPET_ASSET "distribution/assets/Glow.gif")
set(AURAPET_ICON "${CMAKE_CURRENT_SOURCE_DIR}/distribution/assets/Glow.icns")
set(AURAPET_ICON_NAME "Glow.icns")

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Sql Concurrent)
find_package(libavif 1.0 CONFIG REQUIRED)

qt_add_executable(gif_pet
    main.cpp
    pet_asset.cpp
    pet_asset.h
    pet_window.cpp
    pet_window.h
    pet_tether.cpp
    pet_tether.h
    pet_treats.cpp
    pet_treats.h
    random_movement.cpp
    random_movement.h
    pet_motion_rules.cpp
    pet_motion_rules.h
    activity_meter.cpp
    activity_meter.h
    focus_session.cpp
    focus_session.h
    companion_menu.cpp
    companion_events.cpp
    companion_events.h
    companion_menu.h
    pet_appearance.cpp
    pet_appearance.h
    role_library.cpp
    role_library.h
    role_memory.cpp
    role_share.cpp
    role_package.cpp
    role_package_dialog.cpp
    role_package.h
    role_actions.h
    role_actions.cpp
    action_editor.h
    action_editor.cpp
    role_library_dialog.cpp
    role_library_dialog.h
)
set_source_files_properties("${AURAPET_ASSET}" PROPERTIES QT_RESOURCE_ALIAS "凯猫.gif")
qt_add_resources(gif_pet "default_pet"
    PREFIX "/default"
    FILES "${AURAPET_ASSET}"
)
target_compile_features(gif_pet PRIVATE cxx_std_17)
target_compile_definitions(gif_pet PRIVATE
    GIFPET_START_WITH_KAIMAO=$<BOOL:${GIFPET_START_WITH_KAIMAO}>
    AURAPET_RELEASE_ASSETS=1)
target_link_libraries(gif_pet PRIVATE Qt6::Widgets Qt6::Sql Qt6::Concurrent avif)
if(APPLE)
    find_library(ARCHIVE_LIBRARY archive REQUIRED)
    target_link_libraries(gif_pet PRIVATE ${ARCHIVE_LIBRARY})
else()
    find_package(LibArchive REQUIRED)
    target_link_libraries(gif_pet PRIVATE LibArchive::LibArchive)
endif()
if(APPLE)
    enable_language(OBJCXX)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Network)
    find_library(VISION_FRAMEWORK Vision REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    find_library(IMAGEIO_FRAMEWORK ImageIO REQUIRED)
    find_library(CARBON_FRAMEWORK Carbon REQUIRED)
    find_library(SECURITY_FRAMEWORK Security REQUIRED)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    target_sources(gif_pet PRIVATE
        background_removal.h
        background_removal.mm
        gif_background.h
        gif_background.mm
        frame_sequence.h
        frame_sequence.mm
        sequence_dialog.h
        sequence_dialog.cpp
        action_templates.h
        action_templates.cpp
        template_dialog.h
        template_dialog.cpp
        import_preview.h
        import_preview.cpp
        global_hotkeys.h
        global_hotkeys.cpp
        chat_dialog.h
        chat_dialog.cpp
        chat_session.h
        chat_session.cpp
        pet_chat_overlay.h
        pet_chat_overlay.cpp
        speech_bubble.h
        speech_bubble.cpp
        speech_playback.h
        speech_playback.cpp
        speech_audio.mm
        chat_credentials.h
        chat_credentials.mm
        chat_stream.h
        chat_stream.cpp
        chat_summary.h
        chat_summary.cpp
        memory_dialog.h
        memory_dialog.cpp
        "${AURAPET_ICON}"
    )
    set_source_files_properties("${AURAPET_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(gif_pet PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED TRUE)
    set_source_files_properties(background_removal.mm gif_background.mm frame_sequence.mm chat_credentials.mm speech_audio.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(gif_pet PRIVATE Qt6::Network
        ${VISION_FRAMEWORK} ${COREGRAPHICS_FRAMEWORK} ${COREVIDEO_FRAMEWORK} ${IMAGEIO_FRAMEWORK}
        ${SECURITY_FRAMEWORK} ${AVFOUNDATION_FRAMEWORK} ${CARBON_FRAMEWORK})
endif()
set_target_properties(gif_pet PROPERTIES
    AUTOMOC ON
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/AuraPetInfo.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "AuraPet"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.gifpet.desktop"
    MACOSX_BUNDLE_ICON_FILE "${AURAPET_ICON_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0.27"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.27"
    OUTPUT_NAME "AuraPet"
)
