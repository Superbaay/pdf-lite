# --- pdf: única capa que conoce PDFium ---------------------------------------
add_library(pdflite_pdf STATIC
    pdf/library.cpp)
target_include_directories(pdflite_pdf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# PRIVATE: los headers de PDFium no se filtran al resto de módulos.
target_link_libraries(pdflite_pdf PRIVATE pdfium)
pdflite_set_warnings(pdflite_pdf)

# --- core: lógica sin UI ni PDFium -------------------------------------------
add_library(pdflite_core INTERFACE)
target_include_directories(pdflite_core INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/generated")

# --- ui: tema, fuentes, widgets ----------------------------------------------
add_library(pdflite_ui STATIC
    ui/fonts.cpp)
target_include_directories(pdflite_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pdflite_ui PUBLIC imgui::imgui pdflite_core)
pdflite_set_warnings(pdflite_ui)

# --- ejecutable --------------------------------------------------------------
add_executable(pdf_lite WIN32
    app/main.cpp
    app/app.cpp)
target_link_libraries(pdf_lite PRIVATE
    pdflite_core pdflite_pdf pdflite_ui
    SDL3::SDL3 imgui::imgui)
pdflite_set_warnings(pdf_lite)

# La carpeta distribuible es la del exe: pdfium.dll + assets/ junto a él.
add_custom_command(TARGET pdf_lite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFLITE_PDFIUM_DLL}" "$<TARGET_FILE_DIR:pdf_lite>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:pdf_lite>/assets"
    VERBATIM)
if(PDFLITE_ASAN_DLL)
    add_custom_command(TARGET pdf_lite POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFLITE_ASAN_DLL}" "$<TARGET_FILE_DIR:pdf_lite>")
endif()

install(TARGETS pdf_lite RUNTIME DESTINATION .)
install(FILES "${PDFLITE_PDFIUM_DLL}" DESTINATION .)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets" DESTINATION .)
