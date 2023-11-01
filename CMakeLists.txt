cmake_minimum_required(VERSION 3.21)
project(cuda_rtc
    LANGUAGES CXX
    VERSION 0.1.0
    HOMEPAGE_URL https://github.com/thomasfrosio/cuda-rtc
)

if (NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()

# Make sure debug configuration doesn't collide with release.
set(CMAKE_DEBUG_POSTFIX "d")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Jitify options.
option(JITIFY_LINK_STATIC "Whether jitify should link its CUDA dependencies statically" OFF)
option(JITIFY_THREAD_SAFE "Whether jitify should be thread-safe" ON)
option(JITIFY_ENABLE_EXCEPTIONS "Whether jitify should throw exceptions" ON)
option(JITIFY_FAIL_IMMEDIATELY "Users can enable this for easier debugging" OFF)
option(JITIFY_USE_LIBCUFILT "Use Jitify's builtin demangler by default" OFF)

# Jitify.
add_library(cuda_rtc_jifity2 INTERFACE src/cuda-rtc/jitify2.hpp)
add_library(cuda_rtc::jitify2 ALIAS cuda_rtc_jifity2)

target_include_directories(cuda_rtc_jifity2 INTERFACE "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>")

# Link everything at link-time and don't rely on jitify's runtime loading, for now.
find_package(CUDAToolkit 11 REQUIRED)
target_link_libraries(cuda_rtc_jifity2 INTERFACE
    CUDA::cuda_driver
    $<IF:$<BOOL:${JITIFY_LINK_STATIC}>, CUDA::nvrtc_static, CUDA::nvrtc>
)

if (CUDAToolkit_VERSION_MAJOR GREATER_EQUAL 12)
    target_link_libraries(cuda_rtc_jifity2 INTERFACE
        $<IF:$<BOOL:${JITIFY_LINK_STATIC}>, CUDA::nvJitLink_static, CUDA::nvJitLink>)
endif ()

if (MSVC)
    target_link_libraries(cuda_rtc_jifity2 INTERFACE DbgHelp) # for UnDecorateSymbolName
else ()
    find_package(Threads REQUIRED)
    target_link_libraries(cuda_rtc_jifity2 INTERFACE ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif ()

# "JITIFY_LINK_*_STATIC" means that libraries are linked at link-time, statically or dynamically.
target_compile_definitions(cuda_rtc_jifity2 INTERFACE
    JITIFY_LINK_NVRTC_STATIC
    JITIFY_LINK_NVJITLINK_STATIC
    JITIFY_LINK_CUDA_STATIC
    "$<$<BOOL:${JITIFY_THREAD_SAFE}>:JITIFY_THREAD_SAFE>"
    "$<$<BOOL:${JITIFY_ENABLE_EXCEPTIONS}>:JITIFY_ENABLE_EXCEPTIONS>"
    "$<$<BOOL:${JITIFY_FAIL_IMMEDIATELY}>:JITIFY_FAIL_IMMEDIATELY>"
    "$<$<BOOL:${JITIFY_USE_LIBCUFILT}>:JITIFY_USE_LIBCUFILT>"
)

# Preprocess
add_executable(cuda_rtc_preprocess src/cuda-rtc/preprocess.cpp)
add_executable(cuda_rtc::preprocess ALIAS cuda_rtc_preprocess)

target_link_libraries(cuda_rtc_preprocess PRIVATE cuda_rtc_jifity2)
set_target_properties(cuda_rtc_preprocess
    PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# Install targets:
include(GNUInstallDirs)
install(TARGETS cuda_rtc_jifity2 cuda_rtc_preprocess
    EXPORT cuda_rtc

    INCLUDES
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"

    RUNTIME
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT cuda_rtc_runtime
)

# Install jifity2 header:
install(FILES ${PROJECT_SOURCE_DIR}/src/cuda-rtc/jitify2.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cuda-rtc)

# Generate and install config files:
include(CMakePackageConfigHelpers)
set(JITIFY_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/cuda_rtc_generated)
configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/cuda_rtcConfig.cmake.in
    ${JITIFY_GENERATED_DIR}/cuda_rtcConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cuda_rtc)
write_basic_package_version_file(
    ${JITIFY_GENERATED_DIR}/cuda_rtcConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${JITIFY_GENERATED_DIR}/cuda_rtcConfig.cmake
    ${JITIFY_GENERATED_DIR}/cuda_rtcConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cuda_rtc)

# Export:
install(EXPORT cuda_rtc
    FILE cuda_rtcTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cuda_rtc
    NAMESPACE cuda_rtc::
)
