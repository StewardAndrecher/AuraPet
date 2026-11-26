cmake_minimum_required(VERSION 3.21.1)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" AURAPET_VERSION_RAW)
string(STRIP "${AURAPET_VERSION_RAW}" AURAPET_PROJECT_VERSION)

if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
endif()

project(AuraPet VERSION ${AURAPET_PROJECT_VERSION} LANGUAGES CXX)

option(AURAPET_RELEASE_ASSETS "Use AuraPet's original icon" ON)

set(AURAPET_ICON "${CMAKE_CURRENT_SOURCE_DIR}/distribution/assets/Glow.icns")
set(AURAPET_ICON_NAME "Glow.icns")
if(NOT AURAPET_RELEASE_ASSETS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AuraPet.icns")
    set(AURAPET_ICON "${CMAKE_CURRENT_SOURCE_DIR}/AuraPet.icns")
    set(AURAPET_ICON_NAME "AuraPet.icns")
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Sql Concurrent Network)
find_package(libavif 1.0 CONFIG REQUIRED)
option(AURAPET_REQUIRE_LIBARCHIVE "Require external libarchive and its official headers" OFF)
find_package(LibArchive 3.7 QUIET)
add_library(aurapet_archive INTERFACE)
if(LibArchive_FOUND)
    target_link_libraries(aurapet_archive INTERFACE LibArchive::LibArchive)
elseif(APPLE AND NOT AURAPET_REQUIRE_LIBARCHIVE)
    find_library(ARCHIVE_LIBRARY archive REQUIRED)
    target_link_libraries(aurapet_archive INTERFACE ${ARCHIVE_LIBRARY})
else()
    message(FATAL_ERROR "Install libarchive >= 3.7 and add its prefix to CMAKE_PREFIX_PATH")
endif()

qt_add_executable(gif_pet
    main.cpp
    desktop_application.cpp
    desktop_application.h
    desktop_pet_manager.cpp
    desktop_pet_manager.h
    settings_store.cpp
    settings_store.h
    role_activation.cpp
    role_activation.h
    pet_import_controller.cpp
    pet_import_controller.h
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
    role_card_editor.cpp
    role_card_editor.h
    voice_training.cpp
    voice_training.h
    role_library.h
    role_memory.cpp
    role_share.cpp
    random_speech.cpp
    random_speech.h
    random_speech_dialog.cpp
    random_speech_dialog.h
    role_package.cpp
    role_package_dialog.cpp
    role_package.h
    role_actions.h
    role_actions.cpp
    action_editor.h
    action_editor.cpp
    role_library_dialog.cpp
    role_library_dialog.h
    space_transit.cpp
    space_transit.h
    # Cross-platform AI chat, memory, speech bubble & streaming TTS client
    chat_dialog.h
    chat_dialog.cpp
    chat_session.h
    chat_session.cpp
    chat_credentials.h
    chat_stream.h
    chat_stream.cpp
    chat_summary.h
    chat_summary.cpp
    memory_dialog.h
    memory_dialog.cpp
    pet_chat_overlay.h
    pet_chat_overlay.cpp
    speech_bubble.h
    speech_bubble.cpp
    speech_playback.h
    speech_playback.cpp
    speech_text.h
    speech_text.cpp
    voice_service_dialog.h
    voice_service_dialog.cpp
    global_hotkeys.h
    mac_companion_services.h
)

target_include_directories(gif_pet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(gif_pet PRIVATE cxx_std_17)
target_compile_definitions(gif_pet PRIVATE
    AURAPET_VERSION="${PROJECT_VERSION}"
    GIFPET_START_WITH_KAIMAO=0
    AURAPET_RELEASE_ASSETS=$<BOOL:${AURAPET_RELEASE_ASSETS}>)
target_link_libraries(gif_pet PRIVATE Qt6::Widgets Qt6::Sql Qt6::Concurrent Qt6::Network avif aurapet_archive)

if(MSVC)
    target_compile_options(gif_pet PRIVATE /utf-8)
endif()

if(APPLE)
    enable_language(OBJCXX)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(VISION_FRAMEWORK Vision REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    find_library(IMAGEIO_FRAMEWORK ImageIO REQUIRED)
    find_library(CARBON_FRAMEWORK Carbon REQUIRED)
    find_library(SECURITY_FRAMEWORK Security REQUIRED)
    find_library(SERVICE_MANAGEMENT_FRAMEWORK ServiceManagement REQUIRED)
    find_library(USER_NOTIFICATIONS_FRAMEWORK UserNotifications REQUIRED)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    target_sources(gif_pet PRIVATE
        mac_companion_services.mm
        mac_window_layer.mm
        mac_window_layer.h
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
        global_hotkeys.cpp
        speech_audio.mm
        chat_credentials.mm
        "${AURAPET_ICON}"
        voice/train_role.py
    )
    set_source_files_properties(voice/train_role.py PROPERTIES MACOSX_PACKAGE_LOCATION Resources/voice)
    set_source_files_properties("${AURAPET_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(gif_pet PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED TRUE)
    set_source_files_properties(mac_window_layer.mm background_removal.mm gif_background.mm frame_sequence.mm chat_credentials.mm speech_audio.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(gif_pet PRIVATE
        ${APPKIT_FRAMEWORK} ${VISION_FRAMEWORK} ${COREGRAPHICS_FRAMEWORK} ${COREVIDEO_FRAMEWORK} ${IMAGEIO_FRAMEWORK}
        ${SECURITY_FRAMEWORK} ${AVFOUNDATION_FRAMEWORK} ${CARBON_FRAMEWORK}
        ${SERVICE_MANAGEMENT_FRAMEWORK} ${USER_NOTIFICATIONS_FRAMEWORK})
else()
    target_sources(gif_pet PRIVATE
        chat_credentials_win.cpp
        speech_audio_win.cpp
        global_hotkeys_win.cpp
        win_companion_services.cpp
    )
    if(WIN32)
        target_link_libraries(gif_pet PRIVATE winmm crypt32 user32 shell32 advapi32)
        set_target_properties(gif_pet PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()

set_target_properties(gif_pet PROPERTIES
    AUTOMOC ON
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/AuraPetInfo.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "AuraPet"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.gifpet.desktop"
    MACOSX_BUNDLE_ICON_FILE "${AURAPET_ICON_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    OUTPUT_NAME "AuraPet"
)

find_package(Qt6 QUIET COMPONENTS LinguistTools)
if(TARGET Qt6::lrelease)
    qt_add_translations(gif_pet TS_FILES translations/aurapet_en.ts RESOURCE_PREFIX "/i18n")
endif()

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(aurapet_regressions
        tests/regressions.cpp desktop_application.cpp desktop_application.h
        pet_asset.cpp role_library.cpp role_memory.cpp role_share.cpp role_actions.cpp
        role_package.cpp companion_events.cpp random_speech.cpp space_transit.cpp space_transit.h
        pet_motion_rules.cpp pet_motion_rules.h
        chat_session.cpp chat_stream.cpp chat_summary.cpp
        settings_store.cpp speech_bubble.cpp speech_playback.cpp speech_playback.h speech_text.cpp
        pet_window.cpp role_library_dialog.cpp role_package_dialog.cpp role_card_editor.cpp
        voice_training.cpp random_speech_dialog.cpp action_editor.cpp
    )
    set_target_properties(aurapet_regressions PROPERTIES AUTOMOC ON)
    target_include_directories(aurapet_regressions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(aurapet_regressions PRIVATE cxx_std_17)
    target_compile_definitions(aurapet_regressions PRIVATE AURAPET_VERSION="${PROJECT_VERSION}" AURAPET_RELEASE_ASSETS=1)
    target_link_libraries(aurapet_regressions PRIVATE Qt6::Widgets Qt6::Sql Qt6::Network Qt6::Concurrent avif aurapet_archive)
    if(MSVC)
        target_compile_options(aurapet_regressions PRIVATE /utf-8)
    endif()
    if(APPLE)
        target_sources(aurapet_regressions PRIVATE
            chat_credentials.mm speech_audio.mm mac_window_layer.mm
            sequence_dialog.cpp template_dialog.cpp
            action_templates.cpp frame_sequence.mm import_preview.cpp background_removal.mm gif_background.mm)
        set_target_properties(aurapet_regressions PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED TRUE)
        set_source_files_properties(mac_window_layer.mm background_removal.mm gif_background.mm frame_sequence.mm chat_credentials.mm speech_audio.mm
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(aurapet_regressions PRIVATE
            ${APPKIT_FRAMEWORK} ${VISION_FRAMEWORK} ${COREGRAPHICS_FRAMEWORK} ${COREVIDEO_FRAMEWORK} ${IMAGEIO_FRAMEWORK}
            ${SECURITY_FRAMEWORK} ${AVFOUNDATION_FRAMEWORK})
    else()
        target_sources(aurapet_regressions PRIVATE
            chat_credentials_win.cpp
            speech_audio_win.cpp
        )
        if(WIN32)
            target_link_libraries(aurapet_regressions PRIVATE winmm crypt32 user32 shell32 advapi32)
        endif()
    endif()
    add_test(NAME aurapet_regressions COMMAND aurapet_regressions)
    set_tests_properties(aurapet_regressions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 35)
endif()
