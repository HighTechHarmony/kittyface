idf_component_register(SRCS "main.c" "audio_out.c" "wav_player.c" "status_led.c" "boot_button.c" "zb_coord.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_i2s esp_driver_gpio nvs_flash spiffs esp-zigbee-lib esp-zboss-lib led_strip)

spiffs_create_partition_image(audio ${CMAKE_SOURCE_DIR}/spiffs FLASH_IN_PROJECT)
