function(foundry_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow
            -Wconversion -Wno-sign-conversion -Wdouble-promotion -Wnon-virtual-dtor)
    endif()
endfunction()
