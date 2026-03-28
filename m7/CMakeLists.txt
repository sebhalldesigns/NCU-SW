set(TARGET_NAME ncu-m7)

set(DEFINES
    STM32H755xx
    CORE_CM7
)
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
    
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sys/sys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eth/eth.c

    ${CMAKE_CURRENT_SOURCE_DIR}/src/syscall.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sld/startup_stm32h755xx_cm7.s
)

set(INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/CMSIS/include/
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/CMSIS/device/ST/STM32H7xx/include/
)

set(LWIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../extern/lwip)

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/sld/stm32h755xi_flash_cm7.ld)

set(CPU_PARAMS
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

add_executable(${TARGET_NAME} ${SOURCES})

target_link_libraries(${TARGET_NAME} PRIVATE lwip)

target_include_directories(${TARGET_NAME} PRIVATE ${INCLUDES})

target_compile_definitions(${TARGET_NAME} PRIVATE
    ${DEFINES}
    $<$<CONFIG:Debug>:DEBUG>
)

target_compile_options(${TARGET_NAME} PRIVATE
    ${CPU_PARAMS}
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -fstack-usage
    $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp -MMD -MP>
    $<$<CONFIG:Debug>:-O0 -g3 -ggdb>
    $<$<CONFIG:Release>:-Os>
)

target_link_options(${TARGET_NAME} PRIVATE
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