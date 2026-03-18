cmake_minimum_required(VERSION 3.21)

project(spider_two_suites LANGUAGES CXX)

include(GNUInstallDirs)

option(SPIDER_BUILD_APP "Build the SDL3 game executable" ON)
option(SPIDER_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(spider_core
    src/core/game_state.cpp
    src/core/game_session.cpp
    src/core/layout.cpp
    src/core/persistence.cpp
)

target_include_directories(spider_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(spider_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(spider_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SPIDER_BUILD_APP)
    find_package(SDL3 REQUIRED CONFIG)
    find_package(SDL3_image REQUIRED CONFIG)

    add_executable(spider_two_suites
        src/app/main.cpp
    )

    target_link_libraries(spider_two_suites
        PRIVATE
            spider_core
            SDL3::SDL3
            SDL3_image::SDL3_image
    )

    target_compile_definitions(spider_two_suites
        PRIVATE
            SPIDER_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/src/assets"
    )

    if(UNIX AND NOT APPLE)
        install(TARGETS spider_two_suites
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )

        install(FILES packaging/linux/spider-two-suites.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        )

        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/spider-two-suites/assets
            FILES_MATCHING
                PATTERN "*.png"
        )

        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/icons/hicolor/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor
            FILES_MATCHING
                PATTERN "*.png"
        )

        find_program(UPDATE_DESKTOP_DATABASE update-desktop-database)
        if(UPDATE_DESKTOP_DATABASE)
            install(CODE "
                execute_process(
                    COMMAND \"${UPDATE_DESKTOP_DATABASE}\"
                        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/applications\"
                    RESULT_VARIABLE _udd_result
                )
                if(NOT _udd_result EQUAL 0)
                    message(WARNING \"update-desktop-database failed (exit \${_udd_result})\")
                endif()
            ")
        endif()

        find_program(GTK_UPDATE_ICON_CACHE gtk-update-icon-cache)
        if(GTK_UPDATE_ICON_CACHE)
            install(CODE "
                execute_process(
                    COMMAND \"${GTK_UPDATE_ICON_CACHE}\" -f -t
                        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/icons/hicolor\"
                    RESULT_VARIABLE _guic_result
                )
                if(NOT _guic_result EQUAL 0)
                    message(WARNING \"gtk-update-icon-cache failed (exit \${_guic_result})\")
                endif()
            ")
        endif()
    endif()
endif()

if(SPIDER_BUILD_TESTS)
    enable_testing()

    add_executable(spider_core_tests
        tests/game_state_test.cpp
    )

    target_link_libraries(spider_core_tests PRIVATE spider_core)

    add_test(NAME spider_core_tests COMMAND spider_core_tests)
    set_tests_properties(spider_core_tests PROPERTIES
        ENVIRONMENT "SPIDER_LAST_SESSION_PATH=${CMAKE_CURRENT_BINARY_DIR}/spider_core_test_save.txt"
    )

    add_executable(spider_app_smoke_test
        src/app/main.cpp
    )

    target_include_directories(spider_app_smoke_test
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fake_sdl
    )

    target_link_libraries(spider_app_smoke_test PRIVATE spider_core)

    target_compile_definitions(spider_app_smoke_test
        PRIVATE
            SPIDER_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/src/assets"
    )

    add_test(NAME spider_app_smoke_test COMMAND spider_app_smoke_test)
    set_tests_properties(spider_app_smoke_test PROPERTIES
        ENVIRONMENT "SPIDER_LAST_SESSION_PATH=${CMAKE_CURRENT_BINARY_DIR}/spider_app_smoke_save.txt"
    )
endif()
