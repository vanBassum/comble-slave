# ──────────────────────────────────────────────────────────────
# Board fragment: ESP32-C3 SuperMini OLED
#   ESP32-C3FH4/FN4 (RISC-V, 4 MB flash, no PSRAM) · USB-C on the chip's
#   native USB Serial/JTAG · blue LED on GPIO8, active LOW
#   0.42" 72x40 SSD1306 OLED on I2C0, SDA=GPIO5 SCL=GPIO6, addr 0x3C
#
# BOARD_HAS_DISPLAY is read back in main/CMakeLists.txt, which is where the
# UI sources are added to the build — so a headless board does not compile
# (or link) a screen it has not got.
#
# A board fragment may append to BOARD_SOURCES (extra .cpp files under this
# folder that need compiling). Component deps are NOT set here — see the note
# in main/CMakeLists.txt: managed deps go in main/idf_component.yml, IDF
# built-ins in COMPONENT_REQUIRES. esp_driver_i2c is already there, for this.
# ──────────────────────────────────────────────────────────────

set(BOARD_HAS_DISPLAY TRUE)

list(APPEND BOARD_SOURCES "${CMAKE_CURRENT_LIST_DIR}/BoardContext.cpp")
