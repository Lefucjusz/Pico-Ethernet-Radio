add_library(lwip STATIC)

target_sources(lwip # TODO cleanup unused
    PRIVATE
        # API
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/api/api_lib.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/api/api_msg.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/api/err.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/api/if_api.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/api/netbuf.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/api/netdb.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/api/netifapi.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/api/sockets.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/api/tcpip.c

        # Core
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/altcp_alloc.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/altcp_tcp.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/altcp.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/def.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/dns.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/inet_chksum.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/init.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/ip.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/mem.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/memp.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/netif.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/pbuf.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/raw.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/stats.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/sys.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/tcp_in.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/tcp_out.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/tcp.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/timeouts.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/udp.c

        # Core IPv4
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/ipv4/acd.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/ipv4/autoip.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/ipv4/dhcp.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/ipv4/etharp.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/ipv4/icmp.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/ipv4/igmp.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/ipv4/ip4_addr.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/ipv4/ip4_frag.c
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/core/ipv4/ip4.c

        # netif
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/netif/ethernet.c

        # FreeRTOS port
        ${CMAKE_CURRENT_LIST_DIR}/arch/sys_arch.c
)
        
target_include_directories(lwip 
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/lwip/src/include
        ${CMAKE_CURRENT_LIST_DIR}/lwip/contrib
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(lwip
    PRIVATE
        freertos_kernel
    PUBLIC
        pico_rand
)
