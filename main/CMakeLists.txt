idf_component_register(
    SRCS "main.cpp"
         "src/defines.cpp"
         "src/module_display.cpp"
         "src/module_gpio.cpp"
         "src/module_mqtt.cpp"
         "src/module_ota.cpp"
         "src/module_pulse_counter.cpp"
         "src/module_pwm.cpp"
         "src/module_sntp.cpp"
         "src/module_timer.cpp"
         "src/module_wifi_provisioning.cpp"
         "src/module_webserver.cpp"
    INCLUDE_DIRS "." "include"
    REQUIRES
        esp_http_server
        nvs_flash
        json
        spiffs
        wifi_provisioning
        esp_wifi
        esp_event
        esp_netif
        mqtt
        esp_https_ota
        driver
        esp_timer
        lwip
)

spiffs_create_partition_image(spiffs web FLASH_IN_PROJECT)
