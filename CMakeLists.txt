cmake_minimum_required(VERSION 3.25)

# vcpkg exige que la carpeta de cachÃ© binaria exista antes de project().
if(DEFINED ENV{VCPKG_DEFAULT_BINARY_CACHE})
    file(MAKE_DIRECTORY "$ENV{VCPKG_DEFAULT_BINARY_CACHE}")
endif()

project(pdf_lite
    VERSION 0.0.1
    DESCRIPTION "Visor y editor de PDF ligero"
    LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "pdf-lite por ahora solo soporta Windows x64")
endif()

# C++23 por std::expected (manejo de errores sin excepciones); ver ADR 0005.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# CRT dinÃ¡mico (/MD) para coincidir con el triplet x64-windows-static-md.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)
include(Pdfium)

if(MSVC)
    # Fuentes en UTF-8 (acentos y Ã± en literales) y sin min/max de <windows.h>.
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    # Binario pequeÃ±o: elimina funciones y datos sin referencias.
    add_compile_options($<$<NOT:$<CONFIG:Debug>>:/Gy> $<$<NOT:$<CONFIG:Debug>>:/Gw>)
    add_link_options($<$<NOT:$<CONFIG:Debug>>:/OPT:REF> $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>)
endif()

find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Stb REQUIRED)

configure_file(src/core/version.hpp.in "${CMAKE_BINARY_DIR}/generated/core/version.hpp" @ONLY)

add_subdirectory(src)

option(PDFLITE_BUILD_TESTS "Compilar las pruebas" ON)
if(PDFLITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
