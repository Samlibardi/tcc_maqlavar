idf_component_register(SRCS "main.c" "wash_cycle.c" "iopanel.c" "devstate.c"
                    INCLUDE_DIRS "."
                    REQUIRES pca9554 driver nvs_flash esp_wifi esp_netif wifi_provisioning)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wswitch -Wno-error=switch)