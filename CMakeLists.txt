cmake_minimum_required(VERSION 3.31)

project(otoca-camhook VERSION 0.1.0 LANGUAGES CXX RC)

set(CAMHOOK_VERSION "${PROJECT_VERSION}")

add_library(otoca-camhook SHARED src/camhook.cc)
add_executable(qrtest tools/qrtest.cc)

set(RC_NAME otoca-camhook)
set(RC_DESCRIPTION "otoca-camhook - card camera for otoca d'or")
configure_file(src/version.rc.in ${CMAKE_BINARY_DIR}/otoca-camhook.rc @ONLY)
target_sources(otoca-camhook PRIVATE ${CMAKE_BINARY_DIR}/otoca-camhook.rc)

foreach (target otoca-camhook qrtest)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    if (MSVC)
        # Reproducible output: the same source always gives the same file.
        target_compile_options(${target} PRIVATE /Brepro /W4)
        target_link_options(${target} PRIVATE /Brepro)
    endif ()
endforeach ()
