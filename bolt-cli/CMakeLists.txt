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
        $<$<CONFIG:Release>:
            /Oi;
            /Gy
        >
        /permissive-;
        /sdl;
        /W3;
        ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
        ${DEFAULT_CXX_EXCEPTION_HANDLING}
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            /OPT:REF;
            /OPT:ICF
        >
        /DEBUG;
        /SUBSYSTEM:CONSOLE
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
        -fno-signaling-nans;
        -fno-rounding-math;
        -funsafe-math-optimizations;
        -fomit-frame-pointer;
        -march=native;
        -flto;
        -ffp-contract=fast;
        -fmerge-all-constants;
        -fmodulo-sched;
        -fmodulo-sched-allow-regmoves;
    )

    target_link_options(${PROJECT_NAME} PRIVATE
    -Ofast;
    -O3;
    -ffast-math;
    -fno-signaling-nans;
    -fno-rounding-math;
    -funsafe-math-optimizations;
    -fomit-frame-pointer;
    -march=native;
    -flto;
    -ffp-contract=fast;
    -fmerge-all-constants;
    -fmodulo-sched;
    -fmodulo-sched-allow-regmoves;
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

