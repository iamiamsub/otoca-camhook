cmake_minimum_required(VERSION 3.31)

project(otoca-camhook VERSION 0.3.1 LANGUAGES CXX RC)

set(CAMHOOK_VERSION "${PROJECT_VERSION}")

add_library(otoca-camhook SHARED src/camhook.cc)
add_executable(otoca-scan WIN32 src/scan.cc src/card.cc src/scan.manifest)
add_executable(qrtest tools/qrtest.cc src/card.cc)

target_link_libraries(otoca-camhook PRIVATE advapi32)
target_link_libraries(otoca-scan PRIVATE advapi32 comctl32 ole32 shell32 windowscodecs)
target_link_libraries(qrtest PRIVATE advapi32 ole32 windowscodecs)

# Version resources
foreach (target otoca-camhook otoca-scan)
    if (target STREQUAL otoca-scan)
        set(RC_EXT exe)
        set(RC_TYPE VFT_APP)
        set(RC_DESCRIPTION "otoca-scan - holds a printed card in front of the otoca d'or camera")
    else ()
        set(RC_EXT dll)
        set(RC_TYPE VFT_DLL)
        set(RC_DESCRIPTION "otoca-camhook - card camera for otoca d'or")
    endif ()
    set(RC_NAME ${target})
    configure_file(src/version.rc.in ${CMAKE_BINARY_DIR}/${target}.rc @ONLY)
    target_sources(${target} PRIVATE ${CMAKE_BINARY_DIR}/${target}.rc)
endforeach ()

foreach (target otoca-camhook otoca-scan qrtest)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
    if (MSVC)
        # Reproducible output: the same source always gives the same file.
        target_compile_options(${target} PRIVATE /Brepro /W4 /utf-8)
        target_link_options(${target} PRIVATE /Brepro)
    endif ()
endforeach ()
