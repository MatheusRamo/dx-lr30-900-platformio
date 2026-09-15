Import("env")

env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.bin",
    env.VerboseAction(
        '"$OBJCOPY" -O ihex "$BUILD_DIR/${PROGNAME}.elf" '
        '"$PROJECT_DIR/firmware.hex"',
        "Exporting $PROJECT_DIR/firmware.hex",
    ),
)
