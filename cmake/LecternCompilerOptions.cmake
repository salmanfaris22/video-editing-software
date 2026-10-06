# Shared compiler configuration applied to every first-party target through
# lectern_configure_target(). Third-party headers come in as SYSTEM includes
# (imported targets), so the warning set only applies to our code.

function(lectern_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc)
        if(LECTERN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
            -Wimplicit-fallthrough -Wformat=2 -Wcast-qual
            -Wno-missing-field-initializers)
        if(LECTERN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()

    if(APPLE)
        # CMake repeats static libraries on link lines to resolve cycles;
        # Apple's linker warns about each repetition. The warning is noise.
        target_link_options(${target} PRIVATE "LINKER:-no_warn_duplicate_libraries")
    endif()

    if(LECTERN_ENABLE_ASAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
    if(LECTERN_ENABLE_TSAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()

# Objective-C++ sources use ARC so ownership of Cocoa objects is automatic.
function(lectern_enable_objc_arc target)
    if(APPLE)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    endif()
endfunction()
