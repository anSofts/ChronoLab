cmake_minimum_required(VERSION 3.21)

project(ChronoLab
    VERSION 0.3.5
    DESCRIPTION "Open-source mechanical watch timegrapher"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CHRONOLAB_BUILD_APP "Build the Qt desktop application" ON)
option(CHRONOLAB_BUILD_TESTS "Build the DSP tests" ON)

add_library(chronolab_core
    src/core/SyntheticWatch.cpp
    src/core/TimegrapherAnalyzer.cpp
)
target_include_directories(chronolab_core PUBLIC src)
target_compile_features(chronolab_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(chronolab_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(chronolab_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CHRONOLAB_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Multimedia Concurrent)

    qt_standard_project_setup()

    qt_add_executable(ChronoLab
        resources.qrc
        src/main.cpp
        src/app/LanguageManager.cpp
        src/app/LanguageManager.hpp
        src/app/MainWindow.cpp
        src/app/MainWindow.hpp
        src/audio/AudioCapture.cpp
        src/audio/AudioCapture.hpp
        src/audio/WavFile.cpp
        src/audio/WavFile.hpp
        src/widgets/SignalPlotWidget.cpp
        src/widgets/SignalPlotWidget.hpp
        src/widgets/TimegrapherPlotWidget.cpp
        src/widgets/TimegrapherPlotWidget.hpp
    )

    target_link_libraries(ChronoLab PRIVATE
        chronolab_core
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::Concurrent
    )

    set_target_properties(ChronoLab PROPERTIES
        WIN32_EXECUTABLE TRUE
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ChronoLab"
        MACOSX_BUNDLE_GUI_IDENTIFIER "it.ansofts.chronolab"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
    )

    install(TARGETS ChronoLab
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
    )
endif()

if(CHRONOLAB_BUILD_TESTS)
    enable_testing()
    add_executable(chronolab_core_tests
        tests/test_analyzer.cpp
    )
    target_link_libraries(chronolab_core_tests PRIVATE chronolab_core)
    add_test(NAME chronolab_core_tests COMMAND chronolab_core_tests)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME chronolab_translation_catalogs
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_translations.py"
        )
        add_test(
            NAME chronolab_project_consistency
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_project_consistency.py"
        )
    endif()
endif()
