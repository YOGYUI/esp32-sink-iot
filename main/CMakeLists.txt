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
    INCLUDE_DIRS "." "include"
)
