cmake_minimum_required(VERSION 3.24)
project(3sx LANGUAGES C CXX)

include(${PROJECT_SOURCE_DIR}/cmake/imgui_sources.cmake)

set(THREESX_CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel)
if(NOT PSP)
    list(APPEND THREESX_CONFIGURATIONS Statcheck)
endif()

get_property(THREESX_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(THREESX_IS_MULTI_CONFIG)
    set(CMAKE_CONFIGURATION_TYPES "${THREESX_CONFIGURATIONS}" CACHE STRING "Available build types" FORCE)
else()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "${THREESX_CONFIGURATIONS}")
endif()

set(CMAKE_C_FLAGS_STATCHECK "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_STATCHECK "${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_EXE_LINKER_FLAGS_STATCHECK "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
set(CMAKE_SHARED_LINKER_FLAGS_STATCHECK "${CMAKE_SHARED_LINKER_FLAGS_DEBUG}")
set(CMAKE_MODULE_LINKER_FLAGS_STATCHECK "${CMAKE_MODULE_LINKER_FLAGS_DEBUG}")

# ======================================
# Options and platform setup
# ======================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ======================================
# Include directories
# ======================================

include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/imgui/imgui
    ${PROJECT_SOURCE_DIR}/src/imgui/dcimgui
    ${PROJECT_SOURCE_DIR}/src/sdk
    ${PROJECT_SOURCE_DIR}/src/platform/video/opengl/glad
)

# ======================================
# Source collection
# ======================================

file(GLOB_RECURSE GAME_SRC CONFIGURE_DEPENDS src/*.c)

if(ANDROID)
    # Shared lib so SDL3's Java side can dlopen + call SDL_main.
    add_library(3sx SHARED ${GAME_SRC})
else()
    add_executable(3sx MACOSX_BUNDLE
        ${GAME_SRC}
    )
endif()

if(NOT PSP AND NOT VITA)
    target_sources(3sx PRIVATE
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:Release>>:${IMGUI_SOURCES}>
    )
endif()

# ======================================
# Compiler and linker flags
# ======================================

if(PSP)
    target_compile_definitions(3sx PRIVATE
        $<$<CONFIG:Debug>:DEBUG>

        $<$<CONFIG:Release>:RELEASE>

        CRS_APP_DRIVER_PSP
        CRS_VIDEO_DRIVER_PSP
        CRS_INPUT_DRIVER_SDL
    )
elseif(VITA)
    target_compile_definitions(3sx PRIVATE
        $<$<CONFIG:Debug>:DEBUG>
        NETPLAY_ENABLED

        $<$<CONFIG:Release>:RELEASE>

        GEKKONET_STATIC
        GEKKONET_NO_ASIO

        CRS_APP_DRIVER_SDL
        # SDL_GPU has no Vita backend — only OpenGL via vitaGL is available.
        CRS_VIDEO_DRIVER_SDL_GENERIC
        CRS_VIDEO_DRIVER_OPENGL
        CRS_INPUT_DRIVER_SDL

        ARCADE_ROM
        # Needs build-ffmpeg-vita.sh (adpcm_adx + adx parser).
        SOUND_ENABLED
    )
else()
    target_compile_definitions(3sx PRIVATE
        $<$<CONFIG:Debug>:DEBUG>
        NETPLAY_ENABLED

        $<$<CONFIG:Release>:RELEASE>
        # CHECKSUM (tf-psa-crypto) is desktop-only; Android handles ISO parsing Java-side via SAF.
        $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${ANDROID}>>>:CHECKSUM>
        SOUND_ENABLED

        $<$<CONFIG:Statcheck>:STATCHECK>

        # GekkoNet is a pre-built static lib; these were previously propagated by its cmake target
        GEKKONET_STATIC
        GEKKONET_NO_ASIO

        CRS_APP_DRIVER_SDL
        CRS_VIDEO_DRIVER_SDL_GENERIC
        CRS_VIDEO_DRIVER_SDL_GPU
        $<$<NOT:$<BOOL:${ANDROID}>>:CRS_VIDEO_DRIVER_OPENGL>
        $<$<NOT:$<CONFIG:Statcheck>>:CRS_INPUT_DRIVER_SDL>
        $<$<CONFIG:Statcheck>:CRS_INPUT_DRIVER_STATCHECK>

        ARCADE_ROM
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:Release>>:IMGUI>

        GL_SILENCE_DEPRECATION
    )
endif()


# Feature toggles
target_compile_definitions(3sx PRIVATE
    MEMCARD_DISABLED
    # CPS3
)

# Optional Discord Rich Presence — set to your own Discord application ID to
# enable in-game presence. Leave unset to compile with Discord RPC stubbed.
#   cmake -DDISCORD_APP_ID=1234567890123456789 ...
if(DEFINED DISCORD_APP_ID)
    target_compile_definitions(3sx PRIVATE DISCORD_APP_ID="${DISCORD_APP_ID}")
endif()

# Build identification baked in for `3sx --version`. BUILD_VERSION can be
# overridden via -DBUILD_VERSION=...; otherwise we use the project version.
# BUILD_GIT_SHA is auto-detected from the working tree when not provided.
if(NOT DEFINED BUILD_VERSION)
    set(BUILD_VERSION "1.7.27")
endif()
if(NOT DEFINED BUILD_GIT_SHA)
    execute_process(
        COMMAND git -C ${CMAKE_SOURCE_DIR} rev-parse --short HEAD
        OUTPUT_VARIABLE BUILD_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT BUILD_GIT_SHA)
        set(BUILD_GIT_SHA "unknown")
    endif()
endif()
target_compile_definitions(3sx PRIVATE
    BUILD_VERSION="${BUILD_VERSION}"
    BUILD_GIT_SHA="${BUILD_GIT_SHA}"
)

if(CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(DISABLED_WARNINGS
        -Wpointer-to-int-cast
        -Wno-deprecated-non-prototype
        -Wno-tautological-overlap-compare
        -Wno-c2x-extensions
        -Wno-incompatible-pointer-types
        -Wno-pointer-sign
    )
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(DISABLED_WARNINGS
        -Wno-unused-but-set-variable
        -Wno-pointer-sign
        -Wno-parentheses
        -Wno-incompatible-pointer-types
        -Wno-discarded-qualifiers
        -Wno-format-overflow
        -Wno-comment
        -Wno-maybe-uninitialized
        -Wno-array-bounds
        -Wno-aggressive-loop-optimizations
        -Wno-stringop-overflow
        -Wno-restrict
        -Wno-stringop-truncation
    )
endif()

target_compile_options(3sx PRIVATE
    -Wall
    -Werror

    -fno-strict-aliasing

    ${DISABLED_WARNINGS}

    $<$<OR:$<CONFIG:Debug>,$<CONFIG:Release>,$<CONFIG:Statcheck>>:-Wno-unused-function>
    # $<$<CONFIG:Debug>:-Wno-unused-variable>
    -Wno-unused-variable
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:Statcheck>>:-Wno-unused-but-set-variable>
)

if(PSP)
    target_compile_options(3sx PRIVATE -G0)
    target_link_options(3sx PRIVATE -G0)
endif()

if(VITA)
    # vita-elf-create needs ABS32/MOVW_ABS_NC relocations, not PIE-style ones.
    target_compile_options(3sx PRIVATE -fno-pic -fno-pie)
    target_link_options(3sx PRIVATE -no-pie)

    # Wrap sceGxmCreateContext so Vita3K's spurious ALREADY_INITIALIZED is handled.
    target_link_options(3sx PRIVATE -Wl,--wrap=sceGxmCreateContext)
endif()

set_target_properties(3sx PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist"
)

if(APPLE)
    set_target_properties(3sx PROPERTIES
        OUTPUT_NAME "3SX"
        MACOSX_BUNDLE_BUNDLE_NAME "3SX"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
elseif(UNIX AND NOT APPLE)
    set_target_properties(3sx PROPERTIES
        INSTALL_RPATH "\$ORIGIN/../lib"
    )
endif()

# ======================================
# Dependencies
# ======================================

set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

if(NOT VITA)
    find_package(ZLIB REQUIRED)
endif()
if(NOT VITA AND NOT ANDROID)
    find_package(OpenGL REQUIRED)
endif()

# Per-platform dep layout:
#   Android: build-deps-android.sh produces <dep>/build-android/
#   Vita:    build-deps-vita.sh    produces <dep>/build-vita/
#            build-ffmpeg-vita.sh  produces ffmpeg/build-vita-arm/ (custom
#            adpcm_adx-enabled build overriding the vitasdk prebuilt)
#   Desktop: build-deps.sh         produces <dep>/build/
if(ANDROID)
    set(DEPS_SUFFIX "build-android")
elseif(VITA)
    set(DEPS_SUFFIX "build-vita")
else()
    set(DEPS_SUFFIX "build")
endif()

if(VITA)
    set(FFMPEG_ROOT    "${THIRD_PARTY_DIR}/ffmpeg/build-vita-arm")
else()
    set(FFMPEG_ROOT    "${THIRD_PARTY_DIR}/ffmpeg/${DEPS_SUFFIX}")
endif()
set(SDL3_ROOT          "${THIRD_PARTY_DIR}/sdl3/${DEPS_SUFFIX}")
set(LIBCDIO_ROOT       "${THIRD_PARTY_DIR}/libcdio/${DEPS_SUFFIX}")
set(GEKKONET_ROOT      "${THIRD_PARTY_DIR}/GekkoNet/${DEPS_SUFFIX}")
set(SDL3_NET_ROOT      "${THIRD_PARTY_DIR}/SDL_net/${DEPS_SUFFIX}")
set(MINIZIP_NG_ROOT    "${THIRD_PARTY_DIR}/minizip-ng/${DEPS_SUFFIX}")
set(TF_PSA_CRYPTO_ROOT "${THIRD_PARTY_DIR}/tf-psa-crypto/${DEPS_SUFFIX}")

if(VITA)
    # Vita: most deps live inside $VITASDK/arm-vita-eabi sysroot. The
    # vitasdk toolchain file already wires include paths + library search
    # paths. GekkoNet / SDL3_net / minizip-ng come from build-deps-vita.sh.
    # ffmpeg overrides the vitasdk prebuilt with our adpcm_adx-enabled build
    # so its includes must come BEFORE the implicit sysroot include path.
    include_directories(BEFORE
        ${FFMPEG_ROOT}/include
    )
    include_directories(
        ${GEKKONET_ROOT}/include
        ${SDL3_NET_ROOT}/include
        ${MINIZIP_NG_ROOT}/include
    )
elseif(ANDROID)
    include_directories(
        ${SDL3_ROOT}/include
        ${GEKKONET_ROOT}/include
        ${SDL3_NET_ROOT}/include
        ${MINIZIP_NG_ROOT}/include
        ${FFMPEG_ROOT}/include
    )
else()
    include_directories(
        ${FFMPEG_ROOT}/include
        ${SDL3_ROOT}/include
        ${LIBCDIO_ROOT}/include
        ${GEKKONET_ROOT}/include
        ${SDL3_NET_ROOT}/include
        ${MINIZIP_NG_ROOT}/include
        ${TF_PSA_CRYPTO_ROOT}/include
    )
endif()

if(PSP)
    target_link_libraries(3sx PRIVATE
        m
        "${SDL3_ROOT}/lib/libSDL3.a"
        GL
        ZLIB::ZLIB
        pspdebug
        pspvram
        pspaudio
        pspvfpu
        pspgu
        pspge
        pspdisplay
        psphprm
        pspctrl
        psppower
    )
elseif(VITA)
    # vitasdk: SDL3 + zlib prebuilt in sysroot. SDL3_net + GekkoNet built
    # from source via build-deps-vita.sh into third_party/<dep>/build-vita/.
    # ffmpeg built from source via build-ffmpeg-vita.sh into
    # third_party/ffmpeg/build-vita-arm/ (vitasdk's prebuilt libavcodec.a
    # lacks adpcm_adx). Use absolute paths so the linker picks ours over the
    # vitasdk prebuilt that sits on the implicit library search path.
    # libcdio + tf-psa-crypto skipped (no ISO parsing, no CHECKSUM on Vita).
    target_link_libraries(3sx PRIVATE
        m
        "${GEKKONET_ROOT}/lib/libGekkoNet.a"
        "${SDL3_NET_ROOT}/lib/libSDL3_net.a"
        SDL3
        SDL3_image
        "${FFMPEG_ROOT}/lib/libavformat.a"
        "${FFMPEG_ROOT}/lib/libavcodec.a"
        "${FFMPEG_ROOT}/lib/libswresample.a"
        "${FFMPEG_ROOT}/lib/libavutil.a"
        # vitaGL with HAVE_VITA3K_SUPPORT=1 (Vita3K-friendly gxm init path).
        # The vitasdk prebuilt links sceGxmVshInitialize which Vita3K rejects.
        # Built by build-deps-vita.sh into third_party/vitaGL-vita3k/.
        "${PROJECT_SOURCE_DIR}/third_party/vitaGL-vita3k/lib/libvitaGL.a"
        "${PROJECT_SOURCE_DIR}/third_party/vitaGL-vita3k/lib/libvitashark.a"
        mathneon
        SceKernelDmacMgr_stub
        SceShaccCgExt
        taihen_stub
        SceShaccCg_stub
        SceGxm_stub
        SceDisplay_stub
        SceCtrl_stub
        SceTouch_stub
        SceAudio_stub
        SceAudioIn_stub
        SceSysmodule_stub
        SceCommonDialog_stub
        SceIme_stub
        SceNet_stub
        SceNetCtl_stub
        SceAppMgr_stub
        ScePower_stub
        SceIofilemgr_stub
        ScePgf_stub
        SceMotion_stub
        SceHid_stub
        SceCamera_stub
        pthread
        mp3lame
        "${MINIZIP_NG_ROOT}/lib/libminizip-ng.a"
        z
        m
        stdc++
    )
elseif(ANDROID)
    # Android: skip libcdio (no ISO parsing) and tf-psa-crypto (CHECKSUM off).
    # ffmpeg IS built for Android now (adpcm_adx + swresample only) for music.
    # NB: minizip-ng installs as libminizip.a on Android (CMake build) vs
    # libminizip-ng.a on Windows (msys2 mingw). Different upstream packaging.
    target_link_libraries(3sx PRIVATE
        m
        "${GEKKONET_ROOT}/lib/libGekkoNet.a"
        "${SDL3_NET_ROOT}/lib/libSDL3_net.a"
        "${MINIZIP_NG_ROOT}/lib/libminizip.a"
        "${FFMPEG_ROOT}/lib/libavcodec.so"
        "${FFMPEG_ROOT}/lib/libavutil.so"
        "${FFMPEG_ROOT}/lib/libswresample.so"
        ZLIB::ZLIB
        log         # __android_log_print
        android     # Java VM hooks for SAF
        stdc++      # GekkoNet
    )
else()
    target_link_libraries(3sx PRIVATE
        m
        "${LIBCDIO_ROOT}/lib/libiso9660.a"
        "${LIBCDIO_ROOT}/lib/libcdio.a"
        "${GEKKONET_ROOT}/lib/libGekkoNet.a"
        "${SDL3_NET_ROOT}/lib/libSDL3_net.a"
        "${MINIZIP_NG_ROOT}/lib/libminizip-ng.a"
        "${TF_PSA_CRYPTO_ROOT}/lib/libtfpsacrypto.a"
        ZLIB::ZLIB
        OpenGL::GL
        stdc++ # For GekkoNet
    )
endif()

if(ANDROID)
    # add_library(3sx SHARED) already gives us lib3sx.so naming. Just link
    # the SDL3 shared lib (loaded by Java side before dlopen'ing us).
    target_link_libraries(3sx PRIVATE
        "${SDL3_ROOT}/lib/libSDL3.so"
    )
elseif(APPLE)
    target_link_libraries(3sx PRIVATE
        ${FFMPEG_ROOT}/lib/libavcodec.dylib
        ${FFMPEG_ROOT}/lib/libavformat.dylib
        ${FFMPEG_ROOT}/lib/libavutil.dylib
        ${FFMPEG_ROOT}/lib/libswresample.dylib
        ${SDL3_ROOT}/lib/libSDL3.0.dylib
        iconv
    )
elseif(WIN32)
    target_link_libraries(3sx PRIVATE
        ${FFMPEG_ROOT}/lib/libavcodec.dll.a
        ${FFMPEG_ROOT}/lib/libavformat.dll.a
        ${FFMPEG_ROOT}/lib/libavutil.dll.a
        ${FFMPEG_ROOT}/lib/libswresample.dll.a
        ${SDL3_ROOT}/lib/libSDL3.dll.a
        iconv
        bcrypt
		dbghelp
        ws2_32
        iphlpapi
        crypt32
    )
    target_link_options(3sx PRIVATE
        "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>,$<CONFIG:Statcheck>>:--for-linker>"
        "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>,$<CONFIG:Statcheck>>:--pdb=3sx.pdb>"
        # Release: link as Windows GUI (no console window). stdout/stderr are
        # still redirected to console.log by CrashLog_Init, so log capture
        # still works.
        "$<$<CONFIG:Release>:-mwindows>"
    )
elseif(UNIX)
    target_link_libraries(3sx PRIVATE
        ${FFMPEG_ROOT}/lib/libavcodec.so
        ${FFMPEG_ROOT}/lib/libavformat.so
        ${FFMPEG_ROOT}/lib/libavutil.so
        ${FFMPEG_ROOT}/lib/libswresample.so
        ${SDL3_ROOT}/lib/libSDL3.so
    )
endif()

# ======================================
# Installation
# ======================================

set(PROJECT_LICENSE_FILES
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt"
)

set(SDL_GPU_SHADER_DIR "${CMAKE_SOURCE_DIR}/src/platform/video/sdl_gpu/shaders/compiled")
set(OPENGL_SHADER_DIR "${CMAKE_SOURCE_DIR}/src/platform/video/opengl/shaders")

if(APPLE)
    install(TARGETS 3sx
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION .
    )

    file(GLOB FFMPEG_DYLIBS "${FFMPEG_ROOT}/lib/*.dylib")

    install(FILES
        ${FFMPEG_DYLIBS}
        ${SDL3_ROOT}/lib/libSDL3.0.dylib
        DESTINATION 3SX.app/Contents/Frameworks
    )

    install(FILES
        ${PROJECT_LICENSE_FILES}
        DESTINATION 3SX.app/Contents/Resources/licenses
    )

    install(CODE "file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/3SX.app/Contents/Resources/shaders/sdlgpu/compiled\")")

    install(DIRECTORY "${SDL_GPU_SHADER_DIR}/"
        DESTINATION 3SX.app/Contents/Resources/shaders/sdlgpu
    )

    install(DIRECTORY "${OPENGL_SHADER_DIR}/"
        DESTINATION 3SX.app/Contents/Resources/shaders/opengl
    )
elseif(WIN32)
    install(TARGETS 3sx
		RUNTIME_DEPENDENCY_SET deps
        RUNTIME DESTINATION bin
    )

    execute_process(COMMAND cygpath -m /mingw64/bin
		OUTPUT_VARIABLE binPath
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	# automatically copies all the dependent DLLS, and ignores the system ones because they aren't necessary
	install(RUNTIME_DEPENDENCY_SET deps
		DIRECTORIES ${binPath} "${FFMPEG_ROOT}/bin" "${SDL3_ROOT}/bin"
		PRE_EXCLUDE_REGEXES "^api"
		POST_EXCLUDE_REGEXES ".*system32/.*\\.dll"
		DESTINATION bin
	)

    install(FILES
        "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>,$<CONFIG:Statcheck>>:${CMAKE_CURRENT_BINARY_DIR}/3sx.pdb>"
        DESTINATION bin
    )

    install(FILES
        ${PROJECT_LICENSE_FILES}
        DESTINATION bin/licenses
    )

    install(CODE "file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/bin/shaders/sdlgpu/compiled\")")

    install(DIRECTORY "${SDL_GPU_SHADER_DIR}/"
        DESTINATION bin/shaders/sdlgpu
    )

    install(DIRECTORY "${OPENGL_SHADER_DIR}/"
        DESTINATION bin/shaders/opengl
    )
elseif(UNIX)
    install(TARGETS 3sx
        RUNTIME DESTINATION bin
    )

    file(GLOB FFMPEG_SO "${FFMPEG_ROOT}/lib/*.so*")
    file(GLOB SDL3_SO "${SDL3_ROOT}/lib/libSDL3.so*")

    install(FILES
        ${FFMPEG_SO}
        ${SDL3_SO}
        DESTINATION lib
    )

    install(FILES
        ${PROJECT_LICENSE_FILES}
        DESTINATION share/3sx/licenses
    )

    install(CODE "file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/bin/shaders/sdlgpu/compiled\")")

    install(DIRECTORY "${SDL_GPU_SHADER_DIR}/"
        DESTINATION bin/shaders/sdlgpu
    )

    install(DIRECTORY "${OPENGL_SHADER_DIR}/"
        DESTINATION bin/shaders/opengl
    )
endif()

# ======================================
# Packaging
# ======================================

if(PSP)
    create_pbp_file(
        TARGET 3sx
        ICON_PATH NULL
        BACKGROUND_PATH NULL
        MUSIC_PATH NULL
        PREVIEW_PATH NULL
        TITLE "3SX"
        VERSION 0.1
    )
elseif(VITA)
    include("${VITASDK}/share/vita.cmake" REQUIRED)
    set(VITA_APP_NAME "3SX")
    set(VITA_TITLEID  "3SXX00001")
    set(VITA_VERSION  "01.00")
    vita_create_self(3sx.self 3sx UNSAFE)

    # Optional AFS bundling for personal-use builds. The default VPK ships
    # engine code only; the user stages SF33RD.AFS at
    # ux0:data/3sx/resources/SF33RD.AFS themselves. To bake the AFS into
    # your own VPK, drop it at vita/resources/SF33RD.AFS before configure
    # (or pass -DVITA_AFS_PATH=/path/to/SF33RD.AFS). Resulting VPK ships
    # Capcom IP — keep it private.
    if(NOT DEFINED VITA_AFS_PATH)
        set(VITA_AFS_PATH "${CMAKE_SOURCE_DIR}/vita/resources/SF33RD.AFS")
    endif()
    set(VITA_AFS_FILE_ARG "")
    if(EXISTS "${VITA_AFS_PATH}")
        message(STATUS "Vita: bundling SF33RD.AFS into VPK (personal-use only).")
        set(VITA_AFS_FILE_ARG FILE "${VITA_AFS_PATH}" resources/SF33RD.AFS)
    else()
        message(STATUS "Vita: shipping engine-only VPK. User must stage ux0:data/3sx/resources/SF33RD.AFS.")
    endif()

    vita_create_vpk(3sx.vpk ${VITA_TITLEID} 3sx.self
        VERSION ${VITA_VERSION}
        NAME ${VITA_APP_NAME}
        # GLES2 shaders for libshacccg / vitaShark (desktop GL3.3 core
        # variants crash sceShaccCgGetFirstParameter at link time).
        FILE ${CMAKE_SOURCE_DIR}/src/platform/video/opengl/shaders/vita/vert.glsl         shaders/opengl/vert.glsl
        FILE ${CMAKE_SOURCE_DIR}/src/platform/video/opengl/shaders/vita/frag_solid.glsl   shaders/opengl/frag_solid.glsl
        FILE ${CMAKE_SOURCE_DIR}/src/platform/video/opengl/shaders/vita/frag_direct.glsl  shaders/opengl/frag_direct.glsl
        FILE ${CMAKE_SOURCE_DIR}/src/platform/video/opengl/shaders/vita/frag_palette_4.glsl shaders/opengl/frag_palette_4.glsl
        FILE ${CMAKE_SOURCE_DIR}/src/platform/video/opengl/shaders/vita/frag_palette_8.glsl shaders/opengl/frag_palette_8.glsl
        ${VITA_AFS_FILE_ARG}
    )
else()
    set(CPACK_PACKAGE_NAME "3SX")
    set(CPACK_PACKAGE_VENDOR "CrowdedStreet")
    # set(CPACK_PACKAGE_VERSION "1.0.0")

    if(WIN32)
        set(CPACK_GENERATOR "ZIP")
    elseif(APPLE)
        set(CPACK_GENERATOR "DragNDrop")
    else(UNIX)
        set(CPACK_GENERATOR "TGZ")
    endif()

    include(CPack)
endif()
