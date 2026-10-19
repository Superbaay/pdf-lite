# Warnings altos para el código propio. Las librerías externas se incluyen como
# SYSTEM (/external:I), así que sus warnings no nos rompen el build.

option(PDFLITE_WARNINGS_AS_ERRORS "Tratar warnings como errores (/WX)" ON)

function(pdflite_set_warnings target)
    if(MSVC)
        # clang-cl también entiende estas banderas estilo MSVC.
        target_compile_options(${target} PRIVATE /W4 /permissive- /w14265 /w14555)
        if(PDFLITE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(${target} PRIVATE
                -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
                -Wnull-dereference -Wdouble-promotion
                # Nombres de PDFium/SDL vienen en macros de C: demasiado ruido.
                -Wno-c++98-compat -Wno-c++98-compat-pedantic)
        endif()
    endif()
endfunction()
