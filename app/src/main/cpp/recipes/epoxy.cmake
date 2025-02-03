file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/epoxy")
file(GENERATE
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/epoxy/config.h
        CONTENT "
#pragma once
#define ENABLE_EGL 1
#define EPOXY_PUBLIC __attribute__((visibility(\"default\"))) extern
#define HAVE_KHRPLATFORM_H")
set(registrys gl egl)
set(EPOXY_SOURCES dispatch_common.c  dispatch_egl.c)
list(TRANSFORM EPOXY_SOURCES PREPEND "libepoxy/src/")
foreach(registry ${registrys})
        add_custom_command(
                OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/epoxy/${registry}_generated_dispatch.c" "${CMAKE_CURRENT_BINARY_DIR}/epoxy/${registry}_generated.h"
                COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/libepoxy/src/gen_dispatch.py"
                "--source" "--header" "--outputdir=${CMAKE_CURRENT_BINARY_DIR}/epoxy/" "${CMAKE_CURRENT_SOURCE_DIR}/libepoxy/registry/${registry}.xml"
                COMMENT "Generating source code (epoxy/${registry}_generated.h)"
                VERBATIM)
        add_custom_target(
                ${registry}_generated
                DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/epoxy/${registry}_generated_dispatch.c" "${CMAKE_CURRENT_BINARY_DIR}/epoxy/${registry}_generated.h"
        )
        set(EPOXY_SOURCES ${EPOXY_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/epoxy/${registry}_generated_dispatch.c)
endforeach()
add_library(epoxy STATIC ${EPOXY_SOURCES})
target_include_directories(epoxy PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/epoxy ${CMAKE_CURRENT_SOURCE_DIR}/libepoxy/include ${CMAKE_CURRENT_SOURCE_DIR}/libepoxy/src)
target_compile_options(epoxy PRIVATE ${common_compile_options})
