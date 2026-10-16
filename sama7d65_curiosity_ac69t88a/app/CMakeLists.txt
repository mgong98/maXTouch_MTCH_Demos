# Application sources
#
# Structure:
#   src/main.c           - Entry point and main loop
#   src/ui/ui.c          - Static UI layouts (EDITOR REPLACEABLE)
#   src/ui_touch.c       - Dynamic touch indicators (must stay in C)
#   src/input_handlers.c - Hardware input callbacks (must stay in C)
#
# When migrating to LVGL Pro Editor:
#   1. Replace src/ui/ui.c and src/ui/ui.h with editor-generated code
#   2. Add any additional generated files below
#   3. Ensure widget names in generated code match those used in input_handlers.c

target_sources(app PRIVATE
    src/main.c
    src/ui/ui.c
    src/ui_touch.c
    src/input_handlers.c
)

target_include_directories(app PRIVATE src)
