set(PROJECT_NAME bolt)

################################################################################
# Source groups
################################################################################
set(Header_Files
    "bolt.h"
    "boltstd/picomatch/picomatch.h"
    "boltstd/boltstd.h"
    "boltstd/boltstd_arrays.h"
    "boltstd/boltstd_core.h"
    "boltstd/boltstd_math.h"
    "boltstd/boltstd_meta.h"
    "boltstd/boltstd_strings.h"
    "boltstd/boltstd_tables.h"
    "boltstd/boltstd_io.h"
    "boltstd/boltstd_regex.h"
    "bt_buffer.h"
    "bt_compiler.h"
    "bt_config.h"
    "bt_context.h"
    "bt_debug.h"
    "bt_embedding.h"
    "bt_gc.h"
    "bt_object.h"
    "bt_op.h"
    "bt_parser.h"
    "bt_prelude.h"
    "bt_tokenizer.h"
    "bt_type.h"
    "bt_userdata.h"
    "bt_value.h"
)
source_group("Header Files" FILES ${Header_Files})

set(Source_Files
    "bolt.c"
    "boltstd/picomatch/picomatch.c"
    "boltstd/boltstd.c"
    "boltstd/boltstd_arrays.c"
    "boltstd/boltstd_core.c"
    "boltstd/boltstd_math.c"
    "boltstd/boltstd_meta.c"
    "boltstd/boltstd_strings.c"
    "boltstd/boltstd_tables.c"
    "boltstd/boltstd_io.c"
    "boltstd/boltstd_regex.c"
    "bt_buffer.c"
    "bt_compiler.c"
    "bt_debug.c"
    "bt_embedding.c"
    "bt_gc.c"
    "bt_object.c"
    "bt_parser.c"
    "bt_prelude.c"
    "bt_tokenizer.c"
    "bt_type.c"
    "bt_userdata.c"
    "bt_value.c"
)
source_group("Source Files" FILES ${Source_Files})

set(ALL_FILES
    ${Header_Files}
    ${Source_Files}
)

################################################################################
# Target
################################################################################
add_library(${PROJECT_NAME} STATIC ${ALL_FILES})

set(ROOT_NAMESPACE bolt)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_GLOBAL_KEYWORD "Win32Proj"
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE "TRUE"
)
################################################################################
# MSVC runtime library
################################################################################
get_property(MSVC_RUNTIME_LIBRARY_DEFAULT TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY)
string(CONCAT "MSVC_RUNTIME_LIBRARY_STR"
    $<$<CONFIG:Release>:
        MultiThreaded
    >
    $<$<NOT:$<OR:$<CONFIG:Release>>>:${MSVC_RUNTIME_LIBRARY_DEFAULT}>
)
set_target_properties(${PROJECT_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY ${MSVC_RUNTIME_LIBRARY_STR})

################################################################################
# Compile definitions
################################################################################
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:"
        "_DEBUG"
    ">"
    "$<$<CONFIG:Release>:"
        "NDEBUG"
    ">"
    "_CRT_SECURE_NO_WARNINGS;"
    "_CONSOLE;"
    "UNICODE;"
    "_UNICODE"
)

################################################################################
# Compile and link options
################################################################################
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:
            /Od;
            /MTd;
        >
        $<$<CONFIG:Release>:
            /O2;
            /Ob2;
            /Oi;
            /Oy;
            /Gr;
            /Gy-;
            /Ot;
            /GR-;
            /GS-;
            /GL;
            /Gm-;
            /Zc:inline;
            /WX-;
            /Zc:forScope;
            /openmp-;
            /FC;
            /Ot;
            /MT;
        >
        /std:c11;
        /fp:except-;
        /fp:fast;
        /permissive-;
        /sdl-;
        /W3;
        /Zi;
        /arch:AVX;
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            /OPT:NOREF;
            /LTCG;
            /OPT:ICF;
            /NXCOMPAT:NO;
            /DYNAMICBASE:NO;
        >
        /DEBUG;
        /SUBSYSTEM:CONSOLE;
    )
endif()

if(NOT MSVC)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    set(TARGET x86_64-none-none)

    target_link_libraries(${PROJECT_NAME} PUBLIC m)
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Ofast;
        -O3;
        -ffast-math;
        -fomit-frame-pointer;
        -march=native;
        -flto;
        -ffp-contract=fast;
        -fmerge-all-constants;
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        -Ofast;
        -O3;
        -ffast-math;
        -fomit-frame-pointer;
        -march=native;
        -flto;
        -ffp-contract=fast;
        -fmerge-all-constants;
    )
endif()