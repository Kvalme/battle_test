cmake_minimum_required(VERSION 3.27)
project(sw_battle_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optional developer check; vendored sources and generated files are excluded.
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format-21 clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    file(GLOB_RECURSE FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp")
    add_custom_target(check-format
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${FORMAT_SOURCES}
        COMMENT "Checking C++ formatting against .clang-format"
        VERBATIM)
endif()

# Vendored, header-only XML parser. No installed packages or build-time downloads.
add_library(sw_pugixml INTERFACE)
target_include_directories(sw_pugixml SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/pugixml")
target_compile_definitions(sw_pugixml INTERFACE PUGIXML_HEADER_ONLY PUGIXML_NO_XPATH)

# Embedded unit templates keep their filename for startup diagnostics.
file(GLOB UNIT_FILES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/units/*.xml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${UNIT_FILES})
list(LENGTH UNIT_FILES UNIT_COUNT)
set(UNIT_XML_DOCUMENTS "")
foreach(unit_file IN LISTS UNIT_FILES)
    get_filename_component(unit_filename "${unit_file}" NAME)
    file(READ "${unit_file}" unit_xml)
    # Find a raw-string delimiter that cannot terminate either input early.
    set(suffix 0)
    while(TRUE)
        set(delimiter "SW_XML_${suffix}")
        string(FIND "${unit_xml}" ")${delimiter}\"" xml_match)
        string(FIND "${unit_filename}" ")${delimiter}\"" filename_match)
        if(xml_match EQUAL -1 AND filename_match EQUAL -1)
            break()
        endif()
        math(EXPR suffix "${suffix} + 1")
    endwhile()
    string(APPEND UNIT_XML_DOCUMENTS
        "    {R\"${delimiter}(${unit_filename})${delimiter}\", R\"${delimiter}(${unit_xml})${delimiter}\"},
")
endforeach()
configure_file(cmake/UnitsXml.hpp.in generated/UnitsXml.hpp @ONLY)

add_library(sw_core
    src/Core/AbilityManager.cpp
    src/Core/ActivationContext.cpp
    src/Core/MtRandom.cpp
    src/Core/Pathfinder.cpp
    src/Core/Properties.cpp
    src/Core/Scheduler.cpp
    src/Core/World.cpp)
target_include_directories(sw_core PUBLIC src)

# OBJECT ensures every self-registering feature is included in each final executable.
file(GLOB_RECURSE FEATURE_SOURCES CONFIGURE_DEPENDS src/Features/*.cpp)
list(FILTER FEATURE_SOURCES EXCLUDE REGEX "/Features/Tests/")
add_library(sw_features OBJECT ${FEATURE_SOURCES})
target_link_libraries(sw_features PUBLIC sw_core)

add_library(sw_config src/IO/Config/UnitLoader.cpp)
target_link_libraries(sw_config PUBLIC sw_core PRIVATE sw_pugixml)

add_executable(sw_battle_test src/main.cpp src/IO/System/CommandParser.cpp)
target_link_libraries(sw_battle_test PRIVATE sw_features sw_config)
target_include_directories(sw_battle_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT sw_battle_test)

include(CTest)
if(BUILD_TESTING)
    file(GLOB_RECURSE TEST_FEATURE_SOURCES CONFIGURE_DEPENDS src/Features/Tests/*.cpp)
    add_executable(sw_tests tests/BattleTests.cpp tests/FeatureTests.cpp tests/TerminationTests.cpp ${TEST_FEATURE_SOURCES})
    target_link_libraries(sw_tests PRIVATE sw_features sw_config)
    target_include_directories(sw_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(sw_tests PRIVATE SW_TEST_UNIT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/units")
    add_executable(sw_bad_registry src/main.cpp src/IO/System/CommandParser.cpp tests/BadRegistry.cpp)
    target_link_libraries(sw_bad_registry PRIVATE sw_features sw_config)
    target_include_directories(sw_bad_registry PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_test(NAME cli_bad_registry COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:sw_bad_registry> -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/bad-registry-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/BadRegistry.cmake)
    add_executable(sw_activation_limit src/main.cpp src/IO/System/CommandParser.cpp)
    target_compile_definitions(sw_activation_limit PRIVATE SW_TEST_ACTIVATION_LIMIT=1)
    target_link_libraries(sw_activation_limit PRIVATE sw_features sw_config)
    target_include_directories(sw_activation_limit PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_test(NAME cli_activation_limit COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:sw_activation_limit> -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/activation-limit-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/ActivationLimit.cmake)
    add_test(NAME battle_contracts COMMAND sw_tests)
    foreach(number RANGE 1 4)
        add_test(NAME scenario_${number} COMMAND sw_battle_test "${CMAKE_CURRENT_SOURCE_DIR}/scenario_${number}.txt")
        set_tests_properties(scenario_${number} PROPERTIES TIMEOUT 10)
    endforeach()
    add_test(NAME effects_scenario COMMAND sw_battle_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/scenarios/effects.txt")
    set_tests_properties(effects_scenario battle_contracts PROPERTIES TIMEOUT 10)
    add_test(NAME cli_protocol COMMAND ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:sw_battle_test> -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/protocol-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/Protocol.cmake)
endif()
