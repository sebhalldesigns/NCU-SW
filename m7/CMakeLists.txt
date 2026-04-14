set(TARGET_NAME ncu-m7)

set(DEFINES
    STM32H745xx
    CORE_CM7
)
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
    
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sys/sys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/task/task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eth/eth.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eth/eth_ws.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eth/http_custom_fs.c

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
set(TUSB_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/usb)
set(TUSB_CONFIG_HEADER ${TUSB_CONFIG_DIR}/tusb_config.h)

if(NOT EXISTS ${TUSB_CONFIG_HEADER})
    message(FATAL_ERROR "Missing TinyUSB config header: ${TUSB_CONFIG_HEADER}")
endif()

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/sld/stm32h745xi_flash_cm7.ld)

set(CPU_PARAMS
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

add_executable(${TARGET_NAME} ${SOURCES})

target_link_libraries(${TARGET_NAME} PRIVATE
    lwip
    tinyusb
)

target_include_directories(${TARGET_NAME} PRIVATE
    ${INCLUDES}
    ${TUSB_CONFIG_DIR}
)

target_compile_definitions(${TARGET_NAME} PRIVATE
    ${DEFINES}
    CFG_TUSB_MCU=OPT_MCU_STM32H7
    CFG_TUSB_OS=OPT_OS_NONE
    CFG_TUSB_RHPORT0_MODE=OPT_MODE_DEVICE
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

# Embed Resources function courtesy of shir0areed on GitHub
function(embed_resources target)
    set(script_path "${CMAKE_CURRENT_BINARY_DIR}/anything_to_c.cmake")
    file(WRITE  "${script_path}" "file(READ \${CMAKE_ARGV3} buf HEX)\n")
    file(APPEND "${script_path}" "string(REGEX REPLACE \"([0-9a-f][0-9a-f])\" \"0x\\\\1, \" buf \${buf})\n")
    file(APPEND "${script_path}" "file(WRITE \${CMAKE_ARGV4} \"const unsigned char \${CMAKE_ARGV5}[] = { \${buf}0x00 };\\n\")\n")
    file(APPEND "${script_path}" "file(APPEND \${CMAKE_ARGV4} \"const unsigned \${CMAKE_ARGV6} = sizeof(\${CMAKE_ARGV5}) - 1;\\n\")\n")
    foreach(res_path ${ARGN})
        string(MAKE_C_IDENTIFIER ${res_path} identifier)
        set(src_path "${CMAKE_CURRENT_SOURCE_DIR}/${res_path}")
        set(dst_path "${CMAKE_CURRENT_BINARY_DIR}/${identifier}.c")
        set(anything_to_c ${CMAKE_COMMAND} -P ${script_path} ${src_path} ${dst_path} ${identifier} ${identifier}_size)
        add_custom_command(OUTPUT ${dst_path} COMMAND ${anything_to_c} DEPENDS ${src_path} VERBATIM)
        target_sources(${target} PRIVATE ${src_path} ${dst_path} )
    endforeach()
endfunction()

embed_resources(${TARGET_NAME}
    web/index.html
    web/styles.css
)
