add_library(audio STATIC)

pico_generate_pio_header(audio 
    ${CMAKE_CURRENT_LIST_DIR}/audio_i2s/audio_i2s.pio
)

target_sources(audio 
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/audio_i2s/audio_i2s.c
        ${CMAKE_CURRENT_LIST_DIR}/decoder/decoder.c
        ${CMAKE_CURRENT_LIST_DIR}/player/player.c
)

target_include_directories(audio
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
    PUBLIC 
        ${CMAKE_CURRENT_LIST_DIR}/audio_i2s
        ${CMAKE_CURRENT_LIST_DIR}/decoder
        ${CMAKE_CURRENT_LIST_DIR}/player
)

target_link_libraries(audio
    PRIVATE
        hardware_dma
        hardware_pio
        freertos_kernel
        utils
        ipc
        helix_mp3
        logger
)
