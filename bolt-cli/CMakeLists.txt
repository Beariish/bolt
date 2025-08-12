set(PROJECT_NAME bolt-cli)

################################################################################
# Source groups
################################################################################
include_directories(../bolt)

set(Source_Files
    "main.c"
)
source_group("Source Files" FILES ${Source_Files})

set(ALL_FILES
    ${Source_Files}
)

################################################################################
# Target
################################################################################
add_executable(${PROJECT_NAME} ${ALL_FILES})

set(ROOT_NAMESPACE boltcli)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_GLOBAL_KEYWORD "Win32Proj"
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE "TRUE"
)
################################################################################
# Compile definitions
################################################################################
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:"
        "_DEBUG;"
        ""
    ">"
    "$<$<CONFIG:Release>:"
        "NDEBUG"
    ">"
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

################################################################################
# Dependencies
################################################################################
add_dependencies(${PROJECT_NAME}
    bolt
)

set(ADDITIONAL_LIBRARY_DEPENDENCIES
    "bolt"
)
target_link_libraries(${PROJECT_NAME} PRIVATE "${ADDITIONAL_LIBRARY_DEPENDENCIES}")
