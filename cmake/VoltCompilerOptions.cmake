option(VOLT_ENABLE_ASAN "Enable AddressSanitizer for Volt targets" OFF)
option(VOLT_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for Volt targets" OFF)
option(VOLT_ENABLE_COVERAGE "Enable coverage instrumentation for Volt targets" OFF)
option(VOLT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for Volt targets" ON)
option(VOLT_ENABLE_CLANG_TIDY "Run clang-tidy for selected Volt targets" OFF)
option(VOLT_CLANG_TIDY_WARNINGS_AS_ERRORS "Treat clang-tidy warnings as errors" ON)

set(VOLT_CLANG_TIDY_EXECUTABLE "" CACHE FILEPATH "Path to the clang-tidy executable")

if(VOLT_ENABLE_CLANG_TIDY)
    if(NOT VOLT_CLANG_TIDY_EXECUTABLE)
        find_program(
            VOLT_CLANG_TIDY_EXECUTABLE
            NAMES clang-tidy clang-tidy-21 clang-tidy-20 clang-tidy-19 clang-tidy-18 clang-tidy-17
            REQUIRED
        )
    endif()

    set(VOLT_CLANG_TIDY_COMMAND ${VOLT_CLANG_TIDY_EXECUTABLE} --quiet)
    if(VOLT_CLANG_TIDY_WARNINGS_AS_ERRORS)
        list(APPEND VOLT_CLANG_TIDY_COMMAND "--warnings-as-errors=*")
    endif()
endif()

function(volt_apply_project_warnings target scope)
    if(MSVC)
        target_compile_options(${target} ${scope} /W4 /permissive-)
        if(VOLT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} ${scope} /WX)
        endif()
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
        if(VOLT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} ${scope} -Werror)
        endif()
    endif()
endfunction()

function(volt_apply_clang_tidy target)
    if(VOLT_ENABLE_CLANG_TIDY)
        set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${VOLT_CLANG_TIDY_COMMAND}")
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
