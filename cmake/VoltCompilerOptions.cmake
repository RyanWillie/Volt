option(VOLT_ENABLE_ASAN "Enable AddressSanitizer for Volt targets" OFF)
option(VOLT_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for Volt targets" OFF)
option(VOLT_ENABLE_COVERAGE "Enable coverage instrumentation for Volt targets" OFF)

function(volt_apply_project_warnings target scope)
    if(MSVC)
        target_compile_options(${target} ${scope} /W4 /permissive-)
    else()
        target_compile_options(
            ${target}
            ${scope}
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
        )
    endif()
endfunction()

function(volt_apply_sanitizers target)
    if(MSVC)
        return()
    endif()

    set(sanitizers "")

    if(VOLT_ENABLE_ASAN)
        list(APPEND sanitizers "address")
    endif()

    if(VOLT_ENABLE_UBSAN)
        list(APPEND sanitizers "undefined")
    endif()

    if(sanitizers)
        list(JOIN sanitizers "," sanitizer_flags)
        target_compile_options(${target} PRIVATE -fsanitize=${sanitizer_flags} -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=${sanitizer_flags})
    endif()
endfunction()

function(volt_apply_coverage target)
    if(NOT VOLT_ENABLE_COVERAGE)
        return()
    endif()

    if(MSVC)
        message(FATAL_ERROR "VOLT_ENABLE_COVERAGE is not supported with MSVC")
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "VOLT_ENABLE_COVERAGE requires a Clang or GNU-like compiler")
    endif()

    target_compile_options(${target} PRIVATE --coverage -O0 -g)
    target_link_options(${target} PRIVATE --coverage)
endfunction()
