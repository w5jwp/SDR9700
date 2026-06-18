find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Multimedia)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(PkgConfig REQUIRED)

pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
pkg_check_modules(SPEEXDSP REQUIRED IMPORTED_TARGET speexdsp)
pkg_check_modules(HIDAPI QUIET IMPORTED_TARGET hidapi-hidraw)
if(NOT HIDAPI_FOUND)
    pkg_check_modules(HIDAPI QUIET IMPORTED_TARGET hidapi)
endif()
find_path(EIGEN3_INCLUDE_DIR
    NAMES Eigen/Dense
    PATH_SUFFIXES eigen3
    REQUIRED
)

# SDR9700_core: IC-9700 protocol stack
# Headers listed explicitly so AUTOMOC processes Q_OBJECT classes whose
# headers are not co-located with their .cpp files.
set(SDR9700_MOC_HEADERS
    audio/AudioConverter.h
    audio/AudioHandlerBase.h
    audio/AudioHandlerQtInput.h
    audio/AudioHandlerQtOutput.h
    core/CachingQueue.h
    radio/Commander.h
    radio/UdpAudio.h
    radio/UdpCivData.h
    radio/UdpHandler.h
    radio/RadioCommander.h
)

set(SDR9700_CORE_SOURCES
    ${SDR9700_MOC_HEADERS}

    # Radio / CI-V protocol
    radio/UdpBase.cpp
    radio/UdpHandler.cpp
    radio/UdpCivData.cpp
    radio/UdpAudio.cpp
    radio/Commander.cpp
    radio/RadioCommander.cpp

    # Core utilities
    core/CachingQueue.cpp
    core/LogCategories.cpp
    core/AppSettings.cpp
    core/RadioProfile.cpp
    core/Types.cpp

    # Audio - Qt Multimedia handlers only
    audio/AudioConverter.cpp
    audio/AudioHandlerBase.cpp
    audio/AudioHandlerQtInput.cpp
    audio/AudioHandlerQtOutput.cpp
)

if(HIDAPI_FOUND)
    list(APPEND SDR9700_MOC_HEADERS
        core/Rc28Manager.h
    )
    list(APPEND SDR9700_CORE_SOURCES
        core/Rc28Manager.cpp
    )
endif()

configure_file(core/AppInfo.h.in core/AppInfo.h @ONLY)
configure_file(core/AppBuildConfig.h.in core/AppBuildConfig.h @ONLY)

add_library(SDR9700_core STATIC ${SDR9700_CORE_SOURCES})

# All role subdirs exposed as include roots so flat #include "Foo.h" resolves
# across the split layout without touching every file.
target_include_directories(SDR9700_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/radio
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_BINARY_DIR}/core
    ${EIGEN3_INCLUDE_DIR}
)

target_link_libraries(SDR9700_core PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Multimedia
    OpenSSL::Crypto
    PkgConfig::OPUS
    PkgConfig::SPEEXDSP
)

if(HIDAPI_FOUND)
    target_link_libraries(SDR9700_core PUBLIC PkgConfig::HIDAPI)
    target_compile_definitions(SDR9700_core PUBLIC HAVE_HIDAPI)
endif()

target_compile_options(SDR9700_core PRIVATE -Wall -Wextra)

# SDR9700 executable
set(SDR9700_SOURCES
    main.cpp
    ../resources/resources.qrc

    backend/IRadioBackend.h
    backend/RadioBackend.h
    backend/RadioBackend.cpp

    models/RadioModel.h
    models/RadioModel.cpp
    models/VfoModel.h
    models/VfoModel.cpp
    models/PanadapterModel.h
    models/PanadapterModel.cpp

    gui/SpectrumWidget.h
    gui/SpectrumWidget.cpp
    gui/UiTheme.h
    gui/MemoryStore.h
    gui/MemoryStore.cpp
    gui/RadioProfileDialog.h
    gui/RadioProfileDialog.cpp
    gui/RadioChooserDialog.h
    gui/RadioChooserDialog.cpp
    gui/PreferencesDialog.h
    gui/PreferencesDialog.cpp
    gui/AboutDialog.h
    gui/AboutDialog.cpp
    gui/DtmfWidget.h
    gui/DtmfWidget.cpp
    gui/TitleBarWidget.h
    gui/TitleBarWidget.cpp
    gui/MemoryWidget.h
    gui/MemoryWidget.cpp
    gui/MuteWidget.h
    gui/MuteWidget.cpp
    gui/PttWidget.h
    gui/PttWidget.cpp
    gui/RecieveWidget.h
    gui/RecieveWidget.cpp
    gui/RepeaterWidget.h
    gui/RepeaterWidget.cpp
    gui/TransmitWidget.h
    gui/TransmitWidget.cpp
    gui/VfoWidget.h
    gui/VfoWidget.cpp
    gui/MainWindow.h
    gui/MainWindow.cpp
)

if(HIDAPI_FOUND)
    list(APPEND SDR9700_SOURCES
        gui/RC28MappingDialog.h
        gui/RC28MappingDialog.cpp
    )
endif()

add_executable(SDR9700 ${SDR9700_SOURCES})

target_include_directories(SDR9700 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(SDR9700 PRIVATE
    SDR9700_core
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
)

if(HIDAPI_FOUND)
    target_compile_definitions(SDR9700 PRIVATE HAVE_HIDAPI)
endif()

target_compile_options(SDR9700 PRIVATE -Wall -Wextra)

set_target_properties(SDR9700 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
