# AddressSanitizer de MSVC/clang-cl para el preset de depuración.

option(PDFLITE_ENABLE_ASAN "Compilar con AddressSanitizer" OFF)

if(PDFLITE_ENABLE_ASAN)
    if(NOT MSVC)
        message(FATAL_ERROR "PDFLITE_ENABLE_ASAN solo está soportado con MSVC/clang-cl")
    endif()
    add_compile_options(/fsanitize=address)
    # Las librerías de vcpkg se compilan sin ASan; desactivar las anotaciones de
    # contenedores de la STL evita errores LNK2038 (detect_mismatch) al enlazar.
    add_compile_definitions(_DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
    # ASan no es compatible con /RTC ni con enlazado incremental.
    string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    add_link_options(/INCREMENTAL:NO)

    # El runtime de ASan es una DLL junto a cl.exe; la copiamos junto al exe para
    # que F5 funcione sin tocar el PATH del sistema.
    get_filename_component(_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_file(PDFLITE_ASAN_DLL
        NAMES clang_rt.asan_dynamic-x86_64.dll
        HINTS "${_cl_dir}" "${_cl_dir}/../lib/clang/*/lib/windows"
        NO_DEFAULT_PATH)
endif()
