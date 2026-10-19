# Descarga verificada de PDFium precompilado (bblanchon/pdfium-binaries).
#
# Por qué precompilado: compilar PDFium exige depot_tools + GN y ~1 h de build.
# Por qué es seguro: la versión está fijada y el archivo se verifica con SHA-256
# antes de extraerlo; si el hash no coincide, CMake aborta.
# Origen: https://github.com/bblanchon/pdfium-binaries (ver docs/DEPENDENCIES.md).

set(PDFLITE_PDFIUM_TAG "chromium/8076")
set(PDFLITE_PDFIUM_SHA256 "808d36da9bc5a3104315fb307c80998121f565ee53953633bf33e80d7429e5ac")
set(_pdfium_url
    "https://github.com/bblanchon/pdfium-binaries/releases/download/${PDFLITE_PDFIUM_TAG}/pdfium-win-x64.tgz")

# Compartido entre presets para no descargar una copia por cada build/<preset>/.
set(_pdfium_root "${CMAKE_SOURCE_DIR}/build/_deps/pdfium")
set(_pdfium_stamp "${_pdfium_root}/.sha256")

set(_pdfium_ok FALSE)
if(EXISTS "${_pdfium_stamp}")
    file(READ "${_pdfium_stamp}" _stamp_hash)
    if(_stamp_hash STREQUAL PDFLITE_PDFIUM_SHA256)
        set(_pdfium_ok TRUE)
    endif()
endif()

if(NOT _pdfium_ok)
    message(STATUS "PDFium: descargando ${PDFLITE_PDFIUM_TAG}")
    set(_archive "${CMAKE_SOURCE_DIR}/build/_deps/pdfium-win-x64.tgz")
    file(DOWNLOAD "${_pdfium_url}" "${_archive}"
        EXPECTED_HASH SHA256=${PDFLITE_PDFIUM_SHA256}
        TLS_VERIFY ON
        STATUS _dl_status)
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "PDFium: la descarga falló: ${_dl_status}")
    endif()
    file(REMOVE_RECURSE "${_pdfium_root}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_pdfium_root}")
    file(REMOVE "${_archive}")
    file(WRITE "${_pdfium_stamp}" "${PDFLITE_PDFIUM_SHA256}")
endif()

add_library(pdfium SHARED IMPORTED GLOBAL)
set_target_properties(pdfium PROPERTIES
    IMPORTED_LOCATION "${_pdfium_root}/bin/pdfium.dll"
    IMPORTED_IMPLIB "${_pdfium_root}/lib/pdfium.dll.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${_pdfium_root}/include")
set(PDFLITE_PDFIUM_DLL "${_pdfium_root}/bin/pdfium.dll")
set(PDFLITE_PDFIUM_ROOT "${_pdfium_root}")
