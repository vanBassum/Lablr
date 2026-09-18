# ──────────────────────────────────────────────────────────────
# Board fragment: ESP32-S3 N16R8
#   ESP32-S3 (Xtensa LX7, dual core) · 16 MB quad flash · 8 MB OCTAL PSRAM
#   · native USB on GPIO19/20 · no LED bound (see BoardConfig.h)
#
# A board fragment may append to BOARD_SOURCES (extra .cpp files under this
# folder that need compiling). Component deps are NOT set here — see the note
# in main/CMakeLists.txt: managed deps go in main/idf_component.yml, IDF
# built-ins in COMPONENT_REQUIRES.
# ──────────────────────────────────────────────────────────────

list(APPEND BOARD_SOURCES "${CMAKE_CURRENT_LIST_DIR}/BoardContext.cpp")
