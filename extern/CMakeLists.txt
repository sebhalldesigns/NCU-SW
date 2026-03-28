

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