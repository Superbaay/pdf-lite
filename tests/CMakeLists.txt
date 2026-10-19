find_package(doctest CONFIG REQUIRED)

add_executable(pdflite_tests
    test_main.cpp
    test_pdf_library.cpp)
target_link_libraries(pdflite_tests PRIVATE pdflite_core pdflite_pdf doctest::doctest)
pdflite_set_warnings(pdflite_tests)

add_custom_command(TARGET pdflite_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFLITE_PDFIUM_DLL}" "$<TARGET_FILE_DIR:pdflite_tests>")
if(PDFLITE_ASAN_DLL)
    add_custom_command(TARGET pdflite_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFLITE_ASAN_DLL}" "$<TARGET_FILE_DIR:pdflite_tests>")
endif()

include("${doctest_DIR}/doctest.cmake")
doctest_discover_tests(pdflite_tests)
