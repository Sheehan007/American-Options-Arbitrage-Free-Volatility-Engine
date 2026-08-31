cmake_minimum_required(VERSION 3.20)

project(
    aofve
    VERSION 0.1.0
    DESCRIPTION "Dividend-aware American option pricing and arbitrage diagnostics"
    LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Threads REQUIRED)

option(AOFVE_BUILD_PYTHON "Build the pybind11 extension" OFF)
option(AOFVE_BUILD_EXAMPLES "Build examples" ON)
option(AOFVE_BUILD_BENCHMARKS "Build native benchmarks" OFF)
option(AOFVE_WITH_QUANTLIB "Build the optional QuantLib comparison benchmark" OFF)

add_library(aofve
    src/arbitrage.cpp
    src/implied_volatility.cpp
    src/pricing.cpp
    src/svi.cpp
)
add_library(aofve::aofve ALIAS aofve)

target_compile_features(aofve PUBLIC cxx_std_20)
target_link_libraries(aofve PUBLIC Threads::Threads)
set_target_properties(aofve PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(aofve
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(MSVC)
    target_compile_options(aofve PRIVATE /W4 /permissive-)
else()
    target_compile_options(aofve PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(aofve_tests tests/test_main.cpp)
    target_link_libraries(aofve_tests PRIVATE aofve::aofve)
    add_test(NAME aofve_tests COMMAND aofve_tests)
endif()

if(AOFVE_BUILD_EXAMPLES)
    add_executable(aofve_example examples/example.cpp)
    target_link_libraries(aofve_example PRIVATE aofve::aofve)
endif()

if(AOFVE_BUILD_BENCHMARKS)
    add_executable(aofve_benchmark benchmarks/benchmark.cpp)
    target_link_libraries(aofve_benchmark PRIVATE aofve::aofve)
endif()

if(AOFVE_WITH_QUANTLIB)
    find_package(QuantLib CONFIG QUIET)
    add_executable(aofve_quantlib_benchmark benchmarks/quantlib_benchmark.cpp)
    if(TARGET QuantLib::QuantLib)
        target_link_libraries(aofve_quantlib_benchmark PRIVATE aofve::aofve QuantLib::QuantLib)
    elseif(QuantLib_FOUND)
        target_include_directories(aofve_quantlib_benchmark PRIVATE ${QuantLib_INCLUDE_DIRS})
        target_link_libraries(aofve_quantlib_benchmark PRIVATE aofve::aofve ${QuantLib_LIBRARIES})
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(QUANTLIB REQUIRED IMPORTED_TARGET quantlib)
        target_link_libraries(aofve_quantlib_benchmark PRIVATE aofve::aofve PkgConfig::QUANTLIB)
    endif()
endif()

if(AOFVE_BUILD_PYTHON OR SKBUILD)
    find_package(pybind11 CONFIG REQUIRED)
    pybind11_add_module(_aofve python/bindings.cpp)
    target_link_libraries(_aofve PRIVATE aofve::aofve)
    install(TARGETS _aofve LIBRARY DESTINATION aofve)
endif()

install(TARGETS aofve
    EXPORT aofveTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT aofveTargets
    FILE aofveTargets.cmake
    NAMESPACE aofve::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aofve
)

configure_package_config_file(
    cmake/aofveConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/aofveConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aofve
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/aofveConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/aofveConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/aofveConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aofve
)
