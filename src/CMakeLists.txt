# --- core: lógica sin UI ni PDFium -------------------------------------------
add_library(pdflite_core STATIC
    core/command_stack.cpp
    core/settings.cpp)
target_include_directories(pdflite_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(pdflite_core PRIVATE nlohmann_json::nlohmann_json)
pdflite_set_warnings(pdflite_core)

# --- pdf: única capa que conoce PDFium ---------------------------------------
add_library(pdflite_pdf STATIC
    pdf/library.cpp
    pdf/engine.cpp
    pdf/internal/session.cpp)
target_include_directories(pdflite_pdf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# PRIVATE: los headers de PDFium no se filtran al resto de módulos.
target_link_libraries(pdflite_pdf PUBLIC pdflite_core PRIVATE pdfium)
pdflite_set_warnings(pdflite_pdf)

# --- ui: tema, fuentes, widgets ----------------------------------------------
add_library(pdflite_ui STATIC
    ui/fonts.cpp
    ui/theme.cpp
    ui/widgets.cpp
    ui/texture.cpp)
target_include_directories(pdflite_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pdflite_ui PUBLIC imgui::imgui SDL3::SDL3 pdflite_core)
pdflite_set_warnings(pdflite_ui)

# --- módulos -----------------------------------------------------------------
add_library(pdflite_modules STATIC
    modules/page_texture_cache.cpp
    modules/viewer/viewer_module.cpp
    modules/navigation/navigation_module.cpp
    modules/search/search_module.cpp
    modules/text_edit/text_edit_module.cpp
    modules/images/images_module.cpp
    modules/pages/pages_module.cpp
    modules/annotations/annotations_module.cpp
    modules/export/export_module.cpp)
target_include_directories(pdflite_modules PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pdflite_modules PUBLIC pdflite_core pdflite_pdf pdflite_ui
)
# SYSTEM: los warnings de stb_image no deben romper el build con /WX.
target_include_directories(pdflite_modules SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
pdflite_set_warnings(pdflite_modules)

# --- app: ventana, bucle y composición (biblioteca para poder probarla) -------
add_library(pdflite_app STATIC
    app/app.cpp
    app/dialogs.cpp)
target_include_directories(pdflite_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pdflite_app PUBLIC
    pdflite_core pdflite_pdf pdflite_ui pdflite_modules SDL3::SDL3 imgui::imgui)
pdflite_set_warnings(pdflite_app)

# --- ejecutable --------------------------------------------------------------
add_executable(pdf_lite WIN32 app/main.cpp)
target_link_libraries(pdf_lite PRIVATE pdflite_app)
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
# Las licencias permisivas exigen distribuir sus textos junto al binario.
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME pdf-lite-LICENSE.txt)
install(FILES "${PDFLITE_PDFIUM_ROOT}/LICENSE" DESTINATION licenses RENAME pdfium-binaries-LICENSE.txt)
install(DIRECTORY "${PDFLITE_PDFIUM_ROOT}/licenses/" DESTINATION licenses/pdfium)
foreach(pkg sdl3 imgui stb nlohmann-json)
    install(FILES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${pkg}/copyright"
            DESTINATION licenses RENAME "${pkg}-LICENSE.txt")
endforeach()
