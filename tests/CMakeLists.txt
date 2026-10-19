find_package(doctest CONFIG REQUIRED)

add_executable(pdflite_tests
    test_main.cpp
    test_pdf_library.cpp
    test_core.cpp
    test_engine.cpp)
target_link_libraries(pdflite_tests PRIVATE pdflite_core pdflite_pdf doctest::doctest)
target_compile_definitions(pdflite_tests PRIVATE PDFLITE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
pdflite_set_warnings(pdflite_tests)

add_custom_command(TARGET pdflite_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFLITE_PDFIUM_DLL}" "$<TARGET_FILE_DIR:pdflite_tests>")
if(PDFLITE_ASAN_DLL)
    add_custom_command(TARGET pdflite_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFLITE_ASAN_DLL}" "$<TARGET_FILE_DIR:pdflite_tests>")
endif()

include("${doctest_DIR}/doctest.cmake")
doctest_discover_tests(pdflite_tests)

# --- Pruebas de interfaz: la app real en una ventana oculta con eventos sintéticos
add_executable(pdflite_uitests
    test_ui.cpp)
target_link_libraries(pdflite_uitests PRIVATE pdflite_app doctest::doctest)
target_compile_definitions(pdflite_uitests PRIVATE PDFLITE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
pdflite_set_warnings(pdflite_uitests)
add_custom_command(TARGET pdflite_uitests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFLITE_PDFIUM_DLL}" "$<TARGET_FILE_DIR:pdflite_uitests>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:pdflite_uitests>/assets"
    VERBATIM)
if(PDFLITE_ASAN_DLL)
    add_custom_command(TARGET pdflite_uitests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFLITE_ASAN_DLL}" "$<TARGET_FILE_DIR:pdflite_uitests>")
endif()
add_test(NAME ui COMMAND pdflite_uitests WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
