

set(LWIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lwip)

# Only the source files we actually need
set(LWIP_SOURCES
    # Core
    ${LWIP_DIR}/src/core/init.c
    ${LWIP_DIR}/src/core/def.c
    ${LWIP_DIR}/src/core/mem.c
    ${LWIP_DIR}/src/core/memp.c
    ${LWIP_DIR}/src/core/netif.c
    ${LWIP_DIR}/src/core/pbuf.c
    ${LWIP_DIR}/src/core/stats.c
    ${LWIP_DIR}/src/core/sys.c
    ${LWIP_DIR}/src/core/timeouts.c
    ${LWIP_DIR}/src/core/inet_chksum.c

    # TCP/IP
    ${LWIP_DIR}/src/core/tcp.c
    ${LWIP_DIR}/src/core/tcp_in.c
    ${LWIP_DIR}/src/core/tcp_out.c
    ${LWIP_DIR}/src/core/udp.c
    ${LWIP_DIR}/src/core/ip.c
    ${LWIP_DIR}/src/core/raw.c

    # IPv4
    ${LWIP_DIR}/src/core/ipv4/autoip.c
    ${LWIP_DIR}/src/core/ipv4/dhcp.c
    ${LWIP_DIR}/src/core/ipv4/etharp.c
    ${LWIP_DIR}/src/core/ipv4/icmp.c
    ${LWIP_DIR}/src/core/ipv4/igmp.c
    ${LWIP_DIR}/src/core/ipv4/ip4.c
    ${LWIP_DIR}/src/core/ipv4/ip4_addr.c
    ${LWIP_DIR}/src/core/ipv4/ip4_frag.c

    # Network interfaces
    ${LWIP_DIR}/src/netif/ethernet.c

    # HTTPD

    ${LWIP_DIR}/src/apps/http/httpd.c
    ${LWIP_DIR}/src/apps/http/fs.c
    ${LWIP_DIR}/src/apps/http/fsdata.c
    ${LWIP_DIR}/src/apps/http/http_client.c
)

set(LWIP_INCLUDES
    ${LWIP_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/lwip
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/CMSIS/include
)

add_library(lwip STATIC ${LWIP_SOURCES})

target_include_directories(lwip PUBLIC ${LWIP_INCLUDES})

target_compile_options(lwip PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)

set(TINYUSB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tinyusb)

set(TINYUSB_SOURCES
    ${TINYUSB_DIR}/src/tusb.c
    ${TINYUSB_DIR}/src/common/tusb_fifo.c
    ${TINYUSB_DIR}/src/device/usbd.c
    ${TINYUSB_DIR}/src/device/usbd_control.c
    ${TINYUSB_DIR}/src/class/cdc/cdc_device.c
    ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dcd_dwc2.c
    ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dwc2_common.c
)

add_library(tinyusb STATIC ${TINYUSB_SOURCES})

target_include_directories(tinyusb PUBLIC
    ${TINYUSB_DIR}/src
    ${PROJECT_SOURCE_DIR}/m7/src/usb
    ${PROJECT_SOURCE_DIR}/common/
    ${PROJECT_SOURCE_DIR}/common/CMSIS/include/
    ${PROJECT_SOURCE_DIR}/common/CMSIS/device/ST/STM32H7xx/include/
)

target_compile_definitions(tinyusb PUBLIC
    CFG_TUSB_MCU=OPT_MCU_STM32H7
    CFG_TUSB_OS=OPT_OS_NONE
    CFG_TUSB_RHPORT0_MODE=OPT_MODE_DEVICE
    STM32H745xx
    CORE_CM7
)

target_compile_options(tinyusb PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard
)
