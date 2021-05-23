MACRO(CONFIGURE_SDL_SOUND _NAME_TARGET _ONLY_DEFINES)
    SET(PREVIOUS_INVOCATION ${CONFIGURE_SDL_SOUND_INVOKED_PREVIOUSLY})
    FIND_PACKAGE(SDL)
    FIND_PACKAGE(SDL_mixer)
    IF(SDL_FOUND AND SDL_MIXER_FOUND)
        IF(NOT _ONLY_DEFINES)
            TARGET_LINK_LIBRARIES(${_NAME_TARGET} PRIVATE ${SDL_LIBRARY} ${SDL_MIXER_LIBRARIES})
            TARGET_INCLUDE_DIRECTORIES(${_NAME_TARGET} PRIVATE ${SDL_INCLUDE_DIR} ${SDL_MIXER_INCLUDE_DIRS})
        ENDIF()
        TARGET_COMPILE_DEFINITIONS(${_NAME_TARGET} PRIVATE -D SOUND_SDL)
        TARGET_COMPILE_DEFINITIONS(${_NAME_TARGET} PRIVATE -D SOUND)
        IF(NOT PREVIOUS_INVOCATION)
            MESSAGE(STATUS "Support for sound with SDL - Ready")
        ENDIF()
        SET(CONFIGURE_SDL_SOUND_INVOKED_PREVIOUSLY YES CACHE
            INTERNAL "Mark if CONFIGURE_SDL_SOUND called successfully" FORCE)
    ELSE()
        MESSAGE(FATAL_ERROR "Support for sound with SDL - Failed")
    ENDIF()
ENDMACRO()
