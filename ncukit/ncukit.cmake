set(SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/syscall.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sysmem.c
)

set(NCUKIT_STARTUP_SOURCE
    ${CMAKE_CURRENT_LIST_DIR}/sld/startup_stm32h755xx_cm4.s
)

set(INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/../common
    ${CMAKE_CURRENT_LIST_DIR}/../common/CMSIS/include/
    ${CMAKE_CURRENT_LIST_DIR}/../common/CMSIS/device/ST/STM32H7xx/include/
)

set(DEFINES
    STM32H755xx
    CORE_CM4
)

set(CPU_PARAMS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
)

add_compile_options(
    ${CPU_PARAMS}
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -fstack-usage
)

add_compile_definitions(
    ${DEFINES}
    $<$<CONFIG:Debug>:DEBUG>
)

set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -x assembler-with-cpp -MMD -MP")

set(NCUKIT_DIR ${CMAKE_CURRENT_LIST_DIR})

include_directories(${INCLUDES})

add_library(ncukit STATIC ${SOURCES})

function(toolchain_target_hook target_name)

    message(STATUS "Invoking toolchain target hook for target '${target_name}'...")

    set(TARGET_NAME ${target_name})

    # Bare-metal firmware should not use PIC/PIE.
    # PIC introduces GOT accesses for globals (e.g. tunables), but startup only
    # initializes .data/.bss, not GOT relocation tables.
    if(TARGET ${TARGET_NAME})
        set_target_properties(${TARGET_NAME} PROPERTIES POSITION_INDEPENDENT_CODE OFF)
        target_compile_options(${TARGET_NAME} PRIVATE -fno-pic -fno-pie)
    endif()
    if(TARGET ${TARGET_NAME}_objects)
        set_target_properties(${TARGET_NAME}_objects PROPERTIES POSITION_INDEPENDENT_CODE OFF)
        target_compile_options(${TARGET_NAME}_objects PRIVATE -fno-pic -fno-pie)
    endif()
    if(TARGET app_objects)
        set_target_properties(app_objects PROPERTIES POSITION_INDEPENDENT_CODE OFF)
        target_compile_options(app_objects PRIVATE -fno-pic -fno-pie)
    endif()

    target_link_libraries(${target_name} PRIVATE ncukit)

    # Simulink-generated projects typically enable only C/CXX.
    # Assemble startup explicitly with the C compiler driver and link the object.
    set(STARTUP_OBJ ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_startup.o)
    add_custom_command(
        OUTPUT ${STARTUP_OBJ}
        COMMAND ${CMAKE_C_COMPILER} ${CPU_PARAMS} -x assembler-with-cpp -c ${NCUKIT_STARTUP_SOURCE} -o ${STARTUP_OBJ}
        DEPENDS ${NCUKIT_STARTUP_SOURCE}
        VERBATIM
    )
    add_custom_target(${TARGET_NAME}_startup_obj DEPENDS ${STARTUP_OBJ})
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}_startup_obj)
    target_link_libraries(${TARGET_NAME} PRIVATE ${STARTUP_OBJ})

    set(LINKER_SCRIPT ${NCUKIT_DIR}/sld/stm32h755xi_flash_cm4.ld)

    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<CONFIG:Debug>:-O0 -g3 -ggdb>
        $<$<CONFIG:Release>:-Os>
    )

    target_link_options(${TARGET_NAME} PRIVATE
        -fno-pie
        -T${LINKER_SCRIPT}
        ${CPU_PARAMS}
        -Wl,-Map=${TARGET_NAME}.map
        --specs=nosys.specs
        -Wl,--start-group -lc -lm -lstdc++ -lsupc++ -Wl,--end-group
        -Wl,-z,max-page-size=8
        -Wl,--print-memory-usage
    )

    set_target_properties(${TARGET_NAME} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.bin
    )

endfunction()
