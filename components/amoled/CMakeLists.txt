idf_component_register(
    SRCS "amoled_driver.c" "initSequence.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio log
)

# product_pins.h uses #ifdef CONFIG_LILYGO_T4_S3_241 to select pin definitions.
# We don't include the vendor's Kconfig.projbuild, so set the define directly.
target_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_LILYGO_T4_S3_241=1)
