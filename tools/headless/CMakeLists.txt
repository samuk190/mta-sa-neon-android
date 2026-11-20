# Host (Linux x86_64) headless MTA:SA join client.
#
# Compiles the Android port's own native network/join code without the NDK.
# Nothing under app/src/main/cpp is modified: Android-only headers are shadowed
# by stubs/ (android/log.h) and the JNI bridge is replaced by stubs/jni_bridge_stub.cpp.
# __ANDROID__ is deliberately NOT defined. gtasa/GameEngine.cpp is therefore not
# built here; game_script.cpp supplies the same linker surface with a scripted
# local car (driver mode) and records remote vehicles (see game_script.cpp).
# send_tap.cpp wraps RakNetWrapper::SendPacket at link time to log packets 58/33/98.
#
#   cmake -S tools/headless -B <build-dir> && cmake --build <build-dir> -j
cmake_minimum_required(VERSION 3.22)
project(neon_headless LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(HEADLESS_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(NATIVE_DIR "${HEADLESS_DIR}/../../app/src/main/cpp")

set(NATIVE_SOURCES
    # Join protocol (the same object the JNI layer drives).
    "${NATIVE_DIR}/net/NeonJoinProtocol.cpp"
    "${NATIVE_DIR}/net/DisconnectMessages.cpp"

    # RakNet-compatible wrapper on top of the native MTA transport.
    "${NATIVE_DIR}/net/raknet/RakNetWrapper.cpp"
    "${NATIVE_DIR}/net/mta/MtaCipher.cpp"
    "${NATIVE_DIR}/net/mta/MtaClientTransport.cpp"
    "${NATIVE_DIR}/net/mta/MtaOfflineCodec.cpp"
    "${NATIVE_DIR}/net/mta/MtaReliabilityCodec.cpp"
    "${NATIVE_DIR}/net/mta/MtaUdpSocket.cpp"

    # Packet dispatch and game-state subsystems the join path references.
    "${NATIVE_DIR}/shared/PacketHandler.cpp"
    "${NATIVE_DIR}/shared/ResourceFiles.cpp"
    "${NATIVE_DIR}/shared/ResourceLoader.cpp"
    "${NATIVE_DIR}/shared/ResourcePackets.cpp"
    "${NATIVE_DIR}/shared/LuaApiExtra.cpp"
    "${NATIVE_DIR}/shared/LuaGuiApi.cpp"
    "${NATIVE_DIR}/shared/LuaJsonApi.cpp"
    "${NATIVE_DIR}/shared/LuaElementApi.cpp"
    "${NATIVE_DIR}/shared/LuaElementTree.cpp"
    "${NATIVE_DIR}/shared/LuaPedApi.cpp"
    "${NATIVE_DIR}/shared/PedQueries.cpp"
    "${NATIVE_DIR}/shared/SpatialWorld.cpp"
    "${NATIVE_DIR}/shared/LuaSpatialApi.cpp"
    "${NATIVE_DIR}/gtasa/SpatialNative.cpp"
    "${NATIVE_DIR}/shared/LuaVehicleApi.cpp"
    "${NATIVE_DIR}/shared/ModelNames.cpp"
    "${NATIVE_DIR}/shared/ModelInfoRead.cpp"
    "${NATIVE_DIR}/shared/ObjectGroups.cpp"
    "${NATIVE_DIR}/shared/SurfaceInfo.cpp"
    "${NATIVE_DIR}/shared/Streaming.cpp"
    "${NATIVE_DIR}/shared/EngineWorld.cpp"
    "${NATIVE_DIR}/shared/EngineRenderer.cpp"
    "${NATIVE_DIR}/shared/CullZones.cpp"
    "${NATIVE_DIR}/shared/VehicleAudio.cpp"
    "${NATIVE_DIR}/shared/BreakEffects.cpp"
    "${NATIVE_DIR}/shared/EngineModelLoader.cpp"
    "${NATIVE_DIR}/shared/EngineAssets.cpp"
    "${NATIVE_DIR}/shared/SampMapParser.cpp"
    "${NATIVE_DIR}/shared/LuaEngineApi.cpp"
    "${NATIVE_DIR}/shared/LuaCategoryHooks.cpp"
    "${NATIVE_DIR}/shared/LuaCameraApi.cpp"
    "${NATIVE_DIR}/shared/LuaFieldApi.cpp"
    "${NATIVE_DIR}/shared/LuaUtilApi.cpp"
    "${NATIVE_DIR}/shared/LuaCryptApi.cpp"
    "${NATIVE_DIR}/shared/SharedCrypto.cpp"
    "${NATIVE_DIR}/shared/LuaFileApi.cpp"
    "${NATIVE_DIR}/shared/LuaXmlApi.cpp"
    "${NATIVE_DIR}/shared/LuaBrowserApi.cpp"
    "${NATIVE_DIR}/shared/LuaDiscordApi.cpp"
    "${NATIVE_DIR}/shared/LuaStoredElementApi.cpp"
    "${NATIVE_DIR}/shared/LuaWorldObjectApi.cpp"
    "${NATIVE_DIR}/shared/LuaWaterApi.cpp"
    "${NATIVE_DIR}/shared/LuaFireApi.cpp"
    "${NATIVE_DIR}/shared/LuaMarkerApi.cpp"
    "${NATIVE_DIR}/shared/LuaBlipApi.cpp"
    "${NATIVE_DIR}/shared/LuaRadarApi.cpp"
    "${NATIVE_DIR}/shared/LuaPickupApi.cpp"
    "${NATIVE_DIR}/shared/LuaLightApi.cpp"
    "${NATIVE_DIR}/shared/LuaBirdApi.cpp"
    "${NATIVE_DIR}/shared/LuaTeamApi.cpp"
    "${NATIVE_DIR}/shared/LuaRopeApi.cpp"
    "${NATIVE_DIR}/shared/LuaNativeUiApi.cpp"
    "${NATIVE_DIR}/shared/LuaFxApi.cpp"
    "${NATIVE_DIR}/shared/LuaWorldQueryApi.cpp"
    "${NATIVE_DIR}/shared/LuaSightApi.cpp"
    "${NATIVE_DIR}/shared/LuaAmbientApi.cpp"
    "${NATIVE_DIR}/shared/LuaClothesApi.cpp"
    "${NATIVE_DIR}/shared/LuaWeaponApi.cpp"
    "${NATIVE_DIR}/shared/LuaProjectileApi.cpp"
    "${NATIVE_DIR}/gtasa/GameTasks.cpp"
    "${NATIVE_DIR}/gtasa/ProjectileEngine.cpp"
    "${NATIVE_DIR}/shared/LuaResourceApi.cpp"
    "${NATIVE_DIR}/shared/LuaAudioApi.cpp"
    "${NATIVE_DIR}/shared/LuaMtaWindowApi.cpp"
    "${NATIVE_DIR}/shared/LuaEventRegistry.cpp"
    "${NATIVE_DIR}/shared/LuaKeyBindApi.cpp"
    "${NATIVE_DIR}/shared/LuaDxApi.cpp"
    "${NATIVE_DIR}/shared/DxShader.cpp"
    "${NATIVE_DIR}/shared/LuaDxShaderApi.cpp"
    "${NATIVE_DIR}/shared/LuaModelEffects.cpp"
    "${NATIVE_DIR}/shared/LuaDx3DApi.cpp"
    "${NATIVE_DIR}/shared/ModelEffects.cpp"
    "${NATIVE_DIR}/gtasa/ModelEffectsNative.cpp"
    "${NATIVE_DIR}/gtasa/Dx3DNative.cpp"
    "${NATIVE_DIR}/shared/LuaSvgApi.cpp"
    "${NATIVE_DIR}/net/Checksums.cpp"
    "${NATIVE_DIR}/net/HttpDownloader.cpp"
    "${NATIVE_DIR}/net/RemoteCalls.cpp"
    "${NATIVE_DIR}/shared/VehicleSync.cpp"
    "${NATIVE_DIR}/shared/CombatSync.cpp"
    "${NATIVE_DIR}/shared/ServerWorld.cpp"
    "${NATIVE_DIR}/shared/ConnectionLoading.cpp"
    "${NATIVE_DIR}/shared/entity/EntityAddCore.cpp"
    "${NATIVE_DIR}/shared/entity/EntityAddDummy.cpp"
    "${NATIVE_DIR}/shared/entity/EntityAddMarker.cpp"
    "${NATIVE_DIR}/shared/entity/EntityAddObject.cpp"
    "${NATIVE_DIR}/shared/entity/EntityAddOther.cpp"
    "${NATIVE_DIR}/shared/entity/EntityAddPed.cpp"
    "${NATIVE_DIR}/shared/entity/EntityAddVehicle.cpp"
    "${NATIVE_DIR}/shared/entity/PedSyncState.cpp"

    # Linker-demanded: sync registry, profiler, Lua VM behind PacketHandler's
    # ActiveVm* hooks, and the remote combat/vehicle/ped state they update.
    "${NATIVE_DIR}/shared/SyncManager.cpp"
    "${NATIVE_DIR}/shared/ElementManager.cpp"
    "${NATIVE_DIR}/shared/PerformanceProfiler.cpp"
    "${NATIVE_DIR}/shared/LuaVMStub.cpp"
    "${NATIVE_DIR}/gtasa/HudComponents.cpp"
    "${NATIVE_DIR}/gtasa/RemoteCombat.cpp"
    "${NATIVE_DIR}/gtasa/RemoteVehicle.cpp"
    "${NATIVE_DIR}/gtasa/PedInterpolator.cpp"
    "${NATIVE_DIR}/hook/SymbolResolver.cpp"

    # Scripted engine surface (replaces gtasa/GameEngine.cpp in this build).
    "${HEADLESS_DIR}/game_script.cpp"

    # Host send tap and JNI bridge stub (shared by every headless executable).
    "${HEADLESS_DIR}/send_tap.cpp"
    "${HEADLESS_DIR}/stubs/jni_bridge_stub.cpp"
)

# The port's objects are compiled once and linked into each headless executable:
# neon_headless (live join client, main.cpp) and neon_resource_tests (host checks).
add_library(neon_core OBJECT ${NATIVE_SOURCES})

# ElementManager.cpp uses std::unique_lock but only gets <shared_mutex> from its
# header; libc++ (Android NDK) provides <mutex> transitively, libstdc++ does not.
# Force-include it for that one translation unit instead of editing the source.
set_source_files_properties("${NATIVE_DIR}/shared/ElementManager.cpp"
    PROPERTIES COMPILE_OPTIONS "-include;mutex")

set(HEADLESS_INCLUDE_DIRS
    # Shadow android/log.h before anything else can resolve it.
    "${HEADLESS_DIR}/stubs"
    "${NATIVE_DIR}"
    "${NATIVE_DIR}/net"
    "${NATIVE_DIR}/net/raknet"
    "${NATIVE_DIR}/net/mta"
    "${NATIVE_DIR}/hook"
    "${NATIVE_DIR}/gtasa"
    "${NATIVE_DIR}/shared"
    "${NATIVE_DIR}/shared/entity"
)
target_include_directories(neon_core PRIVATE ${HEADLESS_INCLUDE_DIRS})

# Vendored Lua 5.3.6 (static, built into the headless build directory only).
add_subdirectory("${NATIVE_DIR}/lua53" "${CMAKE_CURRENT_BINARY_DIR}/lua53")
add_subdirectory("${NATIVE_DIR}/third_party/hlslparser" "${CMAKE_CURRENT_BINARY_DIR}/hlslparser")
add_subdirectory("${NATIVE_DIR}/third_party/cryptopp" "${CMAKE_CURRENT_BINARY_DIR}/cryptopp")
add_subdirectory("${NATIVE_DIR}/third_party/bcrypt" "${CMAKE_CURRENT_BINARY_DIR}/bcrypt")

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# Pure Java checks for the DX text payload/placement; no Android runtime needed.
find_package(Java 1.8 COMPONENTS Runtime Development QUIET)
if(Java_FOUND)
    add_custom_target(neon_dx_text_tests
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/dx-text-java"
        COMMAND "${Java_JAVAC_EXECUTABLE}" -d "${CMAKE_CURRENT_BINARY_DIR}/dx-text-java"
            "${NATIVE_DIR}/../java/com/worldrpg/neonlauncher/game/DxTextLayout.java"
            "${HEADLESS_DIR}/java/DxTextLayoutTest.java"
        COMMAND "${Java_JAVA_EXECUTABLE}" -cp "${CMAKE_CURRENT_BINARY_DIR}/dx-text-java"
            com.worldrpg.neonlauncher.game.DxTextLayoutTest
        VERBATIM)
    add_custom_target(neon_dx_state_tests
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/dx-state-java"
        COMMAND "${Java_JAVAC_EXECUTABLE}" -d "${CMAKE_CURRENT_BINARY_DIR}/dx-state-java"
            "${NATIVE_DIR}/../java/com/worldrpg/neonlauncher/game/DxFrameQueue.java"
            "${NATIVE_DIR}/../java/com/worldrpg/neonlauncher/game/DxFontCache.java"
            "${HEADLESS_DIR}/java/DxRenderStateTest.java"
        COMMAND "${Java_JAVA_EXECUTABLE}" -cp "${CMAKE_CURRENT_BINARY_DIR}/dx-state-java"
            com.worldrpg.neonlauncher.game.DxRenderStateTest
        VERBATIM)
    set(NEON_DX_ANDROID_JAR "" CACHE FILEPATH "Android SDK android.jar for DX view regression tests")
    if(NEON_DX_ANDROID_JAR)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_custom_target(neon_dx_view_tests
                COMMAND "${Python3_EXECUTABLE}" "${HEADLESS_DIR}/run_dx_view_tests.py"
                    --android-jar "${NEON_DX_ANDROID_JAR}"
                    --build-dir "${CMAKE_CURRENT_BINARY_DIR}/dx-view-java"
                    --javac "${Java_JAVAC_EXECUTABLE}" --java "${Java_JAVA_EXECUTABLE}"
                VERBATIM)
        endif()
    endif()
endif()
# Usage requirements only (Lua and zlib headers): the objects are linked by the executables below.
target_link_libraries(neon_core PRIVATE lua53 ZLIB::ZLIB neon_cryptopp neon_bcrypt)
# Link-time wrap of the one RakNetWrapper send entry point (see send_tap.cpp). The
# symbol is RakNetWrapper::SendPacket(uint8_t, BitStreamWriter const&, PacketPriority,
# PacketReliability, PacketOrdering); the calls come from NeonJoinProtocol.cpp.
add_executable(neon_headless "${HEADLESS_DIR}/main.cpp" $<TARGET_OBJECTS:neon_core>)
add_executable(neon_resource_tests "${HEADLESS_DIR}/resource_tests.cpp" $<TARGET_OBJECTS:neon_core>)
add_executable(neon_dx_conversion_tests "${HEADLESS_DIR}/dx_conversion_tests.cpp" $<TARGET_OBJECTS:neon_core>)
add_executable(neon_ped_query_tests "${HEADLESS_DIR}/ped_query_tests.cpp" $<TARGET_OBJECTS:neon_core>)
add_executable(neon_spatial_tests "${HEADLESS_DIR}/spatial_tests.cpp" $<TARGET_OBJECTS:neon_core>)
add_executable(neon_join_codec_tests "${HEADLESS_DIR}/../../tests/test_join_codec.cpp"
    $<FILTER:$<TARGET_OBJECTS:neon_core>,EXCLUDE,NeonJoinProtocol[.]cpp[.]o$>)
target_link_options(neon_dx_conversion_tests PRIVATE
    "-Wl,--wrap=eglCreatePbufferSurface" "-Wl,--wrap=eglMakeCurrent")

foreach(headless_target neon_headless neon_resource_tests neon_dx_conversion_tests neon_ped_query_tests neon_spatial_tests neon_join_codec_tests)
    target_include_directories(${headless_target} PRIVATE ${HEADLESS_INCLUDE_DIRS})
    target_link_options(${headless_target} PRIVATE
        "-Wl,--wrap=_ZN8WorldRPG3Net13RakNetWrapper10SendPacketEhRKNS0_15BitStreamWriterENS0_14PacketPriorityENS0_17PacketReliabilityENS0_14PacketOrderingE")
    target_link_libraries(${headless_target} PRIVATE
        lua53
        neon_hlsl
        neon_cryptopp
        neon_bcrypt
        EGL
        GLESv2
        ZLIB::ZLIB
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
endforeach()

# Codec compatibility checks use the production wrapper and vendored Speex.
find_package(PkgConfig REQUIRED)
pkg_check_modules(HOST_OPUS REQUIRED IMPORTED_TARGET opus)
add_subdirectory("${NATIVE_DIR}/speex" "${CMAKE_CURRENT_BINARY_DIR}/speex")
add_executable(neon_voice_tests voice_tests.cpp "${NATIVE_DIR}/shared/VoiceCodecStub.cpp")
target_include_directories(neon_voice_tests PRIVATE "${NATIVE_DIR}/shared")
target_link_libraries(neon_voice_tests PRIVATE neon_speex PkgConfig::HOST_OPUS)
add_executable(neon_projectile_tests projectile_tests.cpp
    "${NATIVE_DIR}/gtasa/GameTasks.cpp" "${NATIVE_DIR}/gtasa/ProjectileEngine.cpp")
target_include_directories(neon_projectile_tests PRIVATE "${NATIVE_DIR}")
target_link_libraries(neon_projectile_tests PRIVATE Threads::Threads)
