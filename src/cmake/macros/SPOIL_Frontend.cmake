MACRO(CONFIGURE_SPOIL_FRONTEND _NAME_TARGET)

    TARGET_COMPILE_DEFINITIONS(${_NAME_TARGET} PRIVATE -D USE_SPOIL)
    MESSAGE(STATUS "Support for spoiler front end - Ready")

ENDMACRO()
