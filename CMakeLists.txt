# To get started on a new plugin, copy this entire folder (containing this file and C++ sources) to
# a convenient location, and then start making modifications.

# The first line of any CMake project should be a call to `cmake_minimum_required`, which checks
# that the installed CMake will be able to understand the following CMakeLists, and ensures that
# CMake's behaviour is compatible with the named version. This is a standard CMake command, so more
# information can be found in the CMake docs.

cmake_minimum_required(VERSION 3.15)


if (WIN32)
    #set (CMAKE_GENERATOR_TOOLSET ClangCL)
    #static linking in Windows
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# If we are compiling for Mac OS we want to target OS versions down to 10.9
option(UniversalBinary "Build universal binary for mac" ON)

if (APPLE)
    # Xcode 15+ / macOS 26+ SDKs no longer support deployment targets below 10.13.
    set (CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE INTERNAL "")
    # JUCE builds its "juceaide" helper in a nested cmake run that does not inherit
    # CMAKE_OSX_DEPLOYMENT_TARGET. Without this it defaults to the host OS version and
    # fails on the macOS 26/27 SDK (CGWindowListCreateImage is marked obsoleted in 15.0).
    set (ENV{MACOSX_DEPLOYMENT_TARGET} "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    if (UniversalBinary)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE INTERNAL "")
        #set(CMAKE_OSX_ARCHITECTURES arm64 x86_64)        
    endif()
endif()







# The top-level CMakeLists.txt file for a project must contain a literal, direct call to the
# `project()` command. `project()` sets up some helpful variables that describe source/binary
# directories, and the current project version. This is a standard CMake command.

project(MEON VERSION 1.0.0)

set(BUILDVERSION 1)
# CI 빌드 번호 (GitHub Actions run_number). 0 이면 로컬 빌드: 자동 업데이트를 끈다.
set(MEON_BUILD_ID 0 CACHE STRING "MEON CI build number (0 = local build)")
# 개발용 실행 옵션(--screen=, --dev-jam 등)은 로컬·CI 빌드(빌드 번호 0)에서만 켠다. 릴리스 빌드에는 들어가지 않는다.
if (MEON_BUILD_ID EQUAL 0)
    set(MEON_DEV_OPTIONS 1)
else()
    set(MEON_DEV_OPTIONS 0)
endif()


# If you've installed JUCE somehow (via a package manager, or directly using the CMake install
# target), you'll need to tell this project that it depends on the installed copy of JUCE. If you've
# included JUCE directly in your source tree (perhaps as a submodule), you'll need to tell CMake to
# include that subdirectory as part of the build.

# find_package(JUCE CONFIG REQUIRED)        # If you've installed JUCE to your system
# or


# By default we don't want Xcode schemes to be made for modules, etc
set(CMAKE_XCODE_GENERATE_SCHEME OFF)

# No ZERO_CHECK target (it helps bust cache for cmake)
set(CMAKE_SUPPRESS_REGENERATION true) 

# prevent install all
#set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY true)

# Adds all the module sources so they appear correctly in the IDE
# Must be set before JUCE is added as a sub-dir (or any targets are made)
# https://github.com/juce-framework/JUCE/commit/6b1b4cf7f6b1008db44411f2c8887d71a3348889
set_property(GLOBAL PROPERTY USE_FOLDERS YES)

# This is a failed attempt to bury ALL_BUILD in Targets/
# This should be called before any target is made
# Bug in Xcode? https://gitlab.kitware.com/cmake/cmake/-/issues/21383
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "Targets")

# Create a /Modules directory in the IDE with the JUCE Module code
option(JUCE_ENABLE_MODULE_SOURCE_GROUPS "Show all module sources in IDE projects" ON)


# include JUCE

add_subdirectory(deps/juce EXCLUDE_FROM_ALL )


juce_add_modules(deps/ff_meters)

# MEON core: 화면 없는 로직 (서버 API, 가짜 서버, 상태, 모델). 앱·플러그인과 단위 테스트가 같이 쓴다.
set(MEON_CORE_SOURCES
    Source/meon/core/util/Scheduler.h
    Source/meon/core/api/ApiTypes.h
    Source/meon/core/api/ApiError.h
    Source/meon/core/api/ApiJson.h
    Source/meon/core/api/ApiJson.cpp
    Source/meon/core/api/Transport.h
    Source/meon/core/api/MeonApi.h
    Source/meon/core/api/MeonApi.cpp
    Source/meon/core/api/Realtime.h
    Source/meon/core/api/Realtime.cpp
    Source/meon/core/api/fake/FakeMeonServer.h
    Source/meon/core/api/fake/FakeMeonServer.cpp
    Source/meon/core/api/fake/FakeWorld.cpp
    Source/meon/core/state/AppStore.h
    Source/meon/core/state/AppStore.cpp
    Source/meon/core/auth/TokenStore.h
    Source/meon/core/auth/TokenStore.cpp
    Source/meon/core/auth/AuthController.h
    Source/meon/core/auth/AuthController.cpp
    Source/meon/core/home/HomeController.h
    Source/meon/core/home/HomeController.cpp
    Source/meon/core/room/RoomController.h
    Source/meon/core/room/RoomController.cpp
    Source/meon/core/account/AccountController.h
    Source/meon/core/account/AccountController.cpp
    Source/meon/core/util/Hangul.h
    Source/meon/core/jam/Engine.h
    Source/meon/core/jam/PeerState.h
    Source/meon/core/jam/SilenceDetector.h
    Source/meon/core/jam/ChatChannel.h
    Source/meon/core/jam/JamSession.h
    Source/meon/core/jam/JamSession.cpp
    Source/meon/core/log/SessionLog.h
    Source/meon/core/log/SessionLog.cpp
    Source/meon/core/jam/RttReporter.h
    Source/meon/core/MeonCore.h
    Source/meon/core/MeonCore.cpp
    Source/meon/core/model/Parts.h
    Source/meon/core/model/Validation.h
    Source/meon/core/model/Validation.cpp
    Source/meon/core/model/RoomFilter.h
)

# MEON ui: 새 화면·위젯 (리메이크). 앱·플러그인 타깃만 쓴다 (GUI)
set(MEON_UI_SOURCES
    Source/meon/ui/theme/Tokens.h
    Source/meon/ui/theme/Icons.h
    Source/meon/ui/theme/Icons.cpp
    Source/meon/ui/theme/LookAndFeel.h
    Source/meon/ui/theme/LookAndFeel.cpp
    Source/meon/ui/widgets/Button.h
    Source/meon/ui/widgets/Button.cpp
    Source/meon/ui/widgets/Inputs.h
    Source/meon/ui/widgets/Inputs.cpp
    Source/meon/ui/widgets/Controls.h
    Source/meon/ui/widgets/Controls.cpp
    Source/meon/ui/widgets/Meter.h
    Source/meon/ui/widgets/Meter.cpp
    Source/meon/ui/widgets/Display.h
    Source/meon/ui/widgets/Display.cpp
    Source/meon/ui/widgets/Masonry.h
    Source/meon/ui/Messages.h
    Source/meon/ui/Links.h
    Source/meon/ui/screens/OnboardingScreen.h
    Source/meon/ui/screens/OnboardingScreen.cpp
    Source/meon/ui/screens/HomeScreen.h
    Source/meon/ui/screens/HomeScreen.cpp
    Source/meon/ui/screens/JamScreen.h
    Source/meon/ui/screens/JamScreen.cpp
    Source/meon/ui/overlays/HomeOverlays.h
    Source/meon/ui/overlays/HomeOverlays.cpp
    Source/meon/ui/overlays/FormText.h
    Source/meon/ui/overlays/JamOverlays.h
    Source/meon/ui/overlays/JamOverlays.cpp
    Source/meon/ui/overlays/SettingsModal.h
    Source/meon/ui/overlays/SettingsModal.cpp
    Source/meon/ui/AppRoot.h
    Source/meon/ui/AppRoot.cpp
    Source/meon/ui/dev/WidgetGallery.h
    Source/meon/ui/dev/WidgetGallery.cpp
    Source/meon/ui/dev/DevJamScreen.h
    Source/meon/ui/dev/DevJamScreen.cpp
)

# 가짜 서버 초기 데이터 (MeonFixtures::world_json)
juce_add_binary_data(MEON_Fixtures HEADER_NAME MeonFixtures.h NAMESPACE MeonFixtures SOURCES
    Source/meon/core/api/fake/fixtures/world.json)
set_target_properties(MEON_Fixtures PROPERTIES POSITION_INDEPENDENT_CODE ON)



set (FormatsToBuild VST3 Standalone)

# On Mac, a AU version will be built too
if (APPLE)
    list (APPEND FormatsToBuild AU)
endif()

# On Linux, LV2 will be built too
if (UNIX)
    list (APPEND FormatsToBuild LV2)
endif()

# If you are building a VST2 or AAX plugin, CMake needs to be told where to find these SDKs on your
# system. This setup should be done before calling `juce_add_plugin`.

#juce_set_vst2_sdk_path("../VST2_SDK")
#juce_set_aax_sdk_path("../AAX_SDK_2p3p2")


if (AAX_SDK_PATH)
    juce_set_aax_sdk_path (${AAX_SDK_PATH})
    
    if (APPLE OR (NOT ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")))
      list (APPEND FormatsToBuild AAX)
    endif()
endif()

if (VST2_SDK_PATH)
    juce_set_vst2_sdk_path (${VST2_SDK_PATH})
    list (APPEND FormatsToBuild VST)
endif()


set (MacPList "<plist version=\"1.0\">
<dict>
<key>CFBundleVersion</key>
<string>${BUILDVERSION}</string>
</dict>
</plist>")


# `juce_add_plugin` adds a static library target with the name passed as the first argument
# (AudioPluginExample here). This target is a normal CMake target, but has a lot of extra properties set
# up by default. As well as this shared code static library, this function adds targets for each of
# the formats specified by the FORMATS arguments. This function accepts many optional arguments.
# Check the readme at `docs/CMake API.md` in the JUCE repo for the full list.

function(sono_add_custom_plugin_target target_name product_name formats is_instrument plugincode)

   if (is_instrument)
        set (vst3cats Instrument Network)
        set (vst2cat "kPlugCategSynth")
    else()
        set (vst3cats Fx Network)        
        set (vst2cat "kPlugCategEffect")
   endif()

    juce_add_plugin("${target_name}"
        IS_SYNTH "${is_instrument}"
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COMPANY_NAME "Meon"
        BUNDLE_ID "com.meon.MEON"
        MICROPHONE_PERMISSION_ENABLED TRUE
        
        ICON_BIG "images/icons/mac/meon_mac_1024.png"
        ICON_SMALL "images/icons/mac/meon_mac_256.png"
        NEEDS_WEB_BROWSER FALSE
        VST2_CATEGORY "${vst2cat}"
        VST3_CATEGORIES "${vst3cats}"
        AAX_CATEGORY "AAX_ePlugInCategory_None"

	LV2URI "https://github.com/meonhq/desktop/lv2/meon"
	
        # mac settings
        HARDENED_RUNTIME_ENABLED TRUE
        HARDENED_RUNTIME_OPTIONS "com.apple.security.device.audio-input"
        PLIST_TO_MERGE "${MacPList}"
        AU_MAIN_TYPE "kAudioUnitType_MusicEffect"
        
        
        # other settings...
        PLUGIN_MANUFACTURER_CODE Meon
        PLUGIN_CODE ${plugincode}
        FORMATS ${formats}
        DESCRIPTION "Meon - Online Jam Session"
        PRODUCT_NAME "${product_name}")

    juce_generate_juce_header("${target_name}")


    set (HEADER_INCLUDES
        deps/aoo/lib
        deps/aoo/deps
    )

    set (LIB_PATHS  "")

    set (PLAT_COMPILE_DEFS
        $<$<CONFIG:Debug>:LOGLEVEL=2>
        USE_CODEC_OPUS=1
        AOO_TIMEFILTER_CHECK=0
        AOO_STATIC)

    set(PlatSourceFiles
        Source/CrossPlatformUtils.h
      )


    # platform specific stuff
    if (APPLE)
        list (APPEND HEADER_INCLUDES deps/mac/include)
        list (APPEND LIB_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/deps/mac/lib)
        list (APPEND PlatSourceFiles  Source/CrossPlatformUtilsMac.mm Source/meon/MeonKeyRepeat_mac.mm)
    elseif (WIN32)
        # ASIO SDK (Steinberg, 2.3.4 부터 GPLv3 이중 라이선스). 기본 위치: 저장소 옆 ../asiosdk
        if (NOT ASIO_SDK_PATH)
            set (ASIO_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../asiosdk" CACHE PATH "Steinberg ASIO SDK folder (contains common/iasiodrv.h)")
        endif()
        if (NOT EXISTS "${ASIO_SDK_PATH}/common/iasiodrv.h")
            message (FATAL_ERROR "ASIO SDK not found at ${ASIO_SDK_PATH}. Download ASIO SDK 2.3.4+ from https://www.steinberg.net/asiosdk and unzip it to ../asiosdk (or pass -DASIO_SDK_PATH=...)")
        endif()
        list (APPEND HEADER_INCLUDES deps/windows "${ASIO_SDK_PATH}/common")
        list (APPEND PlatSourceFiles  Source/CrossPlatformUtilsWindows.cpp)

	message (STATUS "Win generator platform is: ${CMAKE_VS_PLATFORM_NAME}" )
        if ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
            list (APPEND LIB_PATHS
                $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/deps/windows/Debug32>
                $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/deps/windows/Release32>
            )
        else()
            list (APPEND LIB_PATHS
                $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/deps/windows/Debug>
                $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/deps/windows/Release>
            )
        endif()

        list (APPEND PLAT_COMPILE_DEFS
            _USE_MATH_DEFINES
            WINVER=0x0601
            _WIN32_WINNT=0x0601)
    else()
        # Linux
        list (APPEND PlatSourceFiles  Source/CrossPlatformUtilsLinux.cpp)
	list ( APPEND PLAT_COMPILE_DEFS
		JUCE_USE_MP3AUDIOFORMAT=1 )
    endif()



    set(SourceFiles
        ${PlatSourceFiles}
        Source/ChannelGroup.cpp
        Source/ChannelGroup.h
        Source/DebugLogC.h
        Source/EffectParams.cpp
        Source/EffectParams.h
        Source/LatencyMeasurer.cpp
        Source/LatencyMeasurer.h
        Source/LocalLatencyMeasurer.h
        Source/MVerb.h
        Source/Metronome.cpp
        Source/Metronome.h
        Source/RunCumulantor.cpp
        Source/RunCumulantor.h
        Source/RunningCumulant.h
        Source/SonoStandaloneFilterApp.cpp
        Source/SonoStandaloneFilterWindow.h
        Source/SonoUtility.h
        Source/Soundboard.cpp
        Source/Soundboard.h
        Source/SoundboardButtonColors.h
        Source/SoundboardChannelProcessor.cpp
        Source/SoundboardChannelProcessor.h
        Source/SoundboardProcessor.cpp
        Source/SoundboardProcessor.h
        Source/SonobusPluginProcessor.cpp
        Source/SonobusPluginProcessor.h
        Source/SonobusTypes.h
        Source/faustCompressor.h
        Source/faustExpander.h
        Source/faustLimiter.h
        Source/faustParametricEQ.h
        Source/mtdm.h
        Source/zitaRev.h
        Source/meon/MeonTheme.cpp
        Source/meon/MeonTheme.h
        Source/meon/MeonEmoji.cpp
        Source/meon/MeonEmoji.h
        Source/meon/MeonKeyRepeat.h
        Source/meon/MeonParts.cpp
        Source/meon/MeonParts.h
        Source/meon/MeonEarGuard.cpp
        Source/meon/MeonEarGuard.h
        Source/meon/MeonSettings.cpp
        Source/meon/MeonSettings.h
        Source/meon/MeonEditor.cpp
        Source/meon/MeonEditor.h
        Source/meon/MeonAudio.cpp
        Source/meon/MeonAudio.h
        Source/meon/MeonUpdater.cpp
        Source/meon/MeonUpdater.h
        Source/meon/shell/ShellHooks.h
        Source/meon/MeonProcessorGlue.cpp
        Source/meon/core/jam/EngineLink.h
        Source/meon/core/jam/EngineLink.cpp
        ${MEON_CORE_SOURCES}
        ${MEON_UI_SOURCES}
    )
    set(AOOSourceFiles        
        deps/aoo/lib/src/SLIP.hpp
        deps/aoo/lib/src/client.cpp
        deps/aoo/lib/src/client.hpp
        deps/aoo/lib/src/codec_opus.cpp
        deps/aoo/lib/src/codec_pcm.cpp
        deps/aoo/lib/src/common.cpp
        deps/aoo/lib/src/common.hpp
        deps/aoo/lib/src/lockfree.hpp
        deps/aoo/lib/src/net_utils.cpp
        deps/aoo/lib/src/net_utils.hpp
        deps/aoo/lib/src/server.cpp
        deps/aoo/lib/src/server.hpp
        deps/aoo/lib/src/sink.cpp
        deps/aoo/lib/src/sink.hpp
        deps/aoo/lib/src/source.cpp
        deps/aoo/lib/src/source.hpp
        deps/aoo/lib/src/sync.cpp
        deps/aoo/lib/src/sync.hpp
        deps/aoo/lib/src/time.cpp
        deps/aoo/lib/src/time.hpp
        deps/aoo/lib/src/time_dll.hpp
        deps/aoo/lib/aoo/aoo.h
        deps/aoo/lib/aoo/aoo.hpp
        deps/aoo/lib/aoo/aoo_net.h
        deps/aoo/lib/aoo/aoo_net.hpp
        deps/aoo/lib/aoo/aoo_opus.h
        deps/aoo/lib/aoo/aoo_pcm.h
        deps/aoo/lib/aoo/aoo_types.h
        deps/aoo/lib/aoo/aoo_utils.hpp
        
        deps/aoo/deps/md5/md5.c
        deps/aoo/deps/md5/md5.h
        deps/aoo/deps/oscpack/osc/OscOutboundPacketStream.cpp
        deps/aoo/deps/oscpack/osc/OscPrintReceivedElements.cpp
        deps/aoo/deps/oscpack/osc/OscReceivedElements.cpp
        deps/aoo/deps/oscpack/osc/OscTypes.cpp
        deps/aoo/deps/oscpack/osc/MessageMappingOscPacketListener.h
        deps/aoo/deps/oscpack/osc/OscException.h
        deps/aoo/deps/oscpack/osc/OscHostEndianness.h
        deps/aoo/deps/oscpack/osc/OscOutboundPacketStream.h
        deps/aoo/deps/oscpack/osc/OscPacketListener.h
        deps/aoo/deps/oscpack/osc/OscPrintReceivedElements.h
        deps/aoo/deps/oscpack/osc/OscReceivedElements.h
        deps/aoo/deps/oscpack/osc/OscTypes.h
    )

    target_sources("${target_name}" PRIVATE 
           ${SourceFiles} 
           ${AOOSourceFiles}
       )

    # No, we don't want our source buried in extra nested folders
    set_target_properties("${target_name}" PROPERTIES FOLDER "")

    # The source tree should uhhh, still look like the source tree, yo
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source PREFIX "" FILES ${SourceFiles})
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/deps/aoo PREFIX "aoo" FILES ${AOOSourceFiles})

    # Move the INTERFACE auto-created JUCE library stuff into its own folder
    source_group("JUCE Library Code" REGULAR_EXPRESSION "juce_")


    #target_include_directories("${target_name}"
    #        INTERFACE
    #            $<TARGET_PROPERTY:"${target_name}",INCLUDE_DIRECTORIES>)

    

    target_include_directories("${target_name}"
        PUBLIC
        ${HEADER_INCLUDES}
    )


    # Require at least C++17 to build `my_target`
    target_compile_features("${target_name}" PRIVATE cxx_std_17)

    # 소스의 한국어 리터럴은 UTF-8 (MSVC 는 기본이 시스템 코드 페이지)
    target_compile_options("${target_name}" PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
        

    # This cleans up the folder organization, especially on Xcode.
    # It tucks the Plugin varieties into a "Targets" folder and generate an Xcode Scheme manually
    # Xcode scheme generation is turned off globally to limit noise from other targets
    # The non-hacky way of doing this is via the global PREDEFINED_TARGETS_FOLDER property
    # However that doesn't seem to be working in Xcode
    # Not all plugin types (au, vst) available on each build type (win, macos, linux)
    foreach(target ${formats} "All")
        if(TARGET ${target_name}_${target})
            set_target_properties(${target_name}_${target} PROPERTIES
                # Tuck the actual plugin targets into a folder where they won't bother us
                FOLDER "Targets"
            
                # MacOS only: Sets the default executable that Xcode will open on build
                # For this exact path to to work, manually build the AudioPluginHost.xcodeproj in the JUCE subdir 
                # XCODE_SCHEME_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/deps/juce/extras/AudioPluginHost/Builds/MacOSX/build/Debug/AudioPluginHost.app"
            
                # Let us build the target in Xcode 
                XCODE_GENERATE_SCHEME ON)
        endif()
    endforeach()
    
    
    target_compile_definitions("${target_name}"
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
	JUCE_USE_WINDOWS_MEDIA_FORMAT=1
        JUCE_LOAD_CURL_SYMBOLS_LAZILY=1
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0
        JUCE_JACK=1
        JUCE_ALSA=1
        JUCE_USE_ANDROID_OBOE=1
        JUCE_USE_OBOE_STABILIZED_CALLBACK=1
        JUCE_LOAD_CURL_SYMBOLS_LAZILY=1
        FF_AUDIO_ALLOW_ALLOCATIONS_IN_MEASURE_BLOCK=0
        SONOBUS_BUILD_VERSION="${PROJECT_VERSION}"
        MEON_BUILD_VERSION="${PROJECT_VERSION}"
        MEON_BUILD_NUMBER=${BUILDVERSION}
        MEON_BUILD_ID=${MEON_BUILD_ID}
        MEON_DEV_OPTIONS=${MEON_DEV_OPTIONS}
        ${PLAT_COMPILE_DEFS} )

    # 아이콘: JUCE 는 ICON_BIG 하나를 축소해 모든 크기를 만든다. MEON 은 작은 크기에서 M 만 쓰는
    # 크기별 아이콘(scripts/make_icons.py 로 생성)을 쓰므로, JUCE 가 만든 파일을 미리 만든 것으로 덮어쓴다.
    # 앱·AU·VST3 번들이 모두 이 파일을 쓴다.
    get_target_property(meon_gen_dir "${target_name}" JUCE_GENERATED_SOURCES_DIRECTORY)
    if (APPLE)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/images/icons/MEON.icns" "${meon_gen_dir}/Icon.icns" COPYONLY)
    elseif (WIN32)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/images/icons/MEON.ico" "${meon_gen_dir}/icon.ico" COPYONLY)
    endif()

    # Windows: JUCE 7 의 GDI 글꼴 경로는 CFF(.otf) 곡선(3차 베지어)을 그리지 못하므로
    # Pretendard 공식 배포본의 TrueType 판(public/static/alternative)을 쓴다.
    if (WIN32)
        set (MEON_FONT_FILES
            Source/fonts/win/Pretendard-Regular.ttf
            Source/fonts/win/Pretendard-Medium.ttf
            Source/fonts/win/Pretendard-SemiBold.ttf
            Source/fonts/win/Pretendard-Bold.ttf)
    else()
        set (MEON_FONT_FILES
            Source/fonts/Pretendard-Regular.otf
            Source/fonts/Pretendard-Medium.otf
            Source/fonts/Pretendard-SemiBold.otf
            Source/fonts/Pretendard-Bold.otf)
    endif()

    juce_add_binary_data("${target_name}_SBData" SOURCES
        ${MEON_FONT_FILES}
        images/bar_click.wav
        images/beat_click.wav
        images/lgc_bar.wav
        images/meon_logo.svg
    )

    set_target_properties(${target_name}_SBData PROPERTIES FOLDER "Targets")

    if (UNIX AND NOT APPLE)
       target_compile_options("${target_name}_SBData"
         PRIVATE
         -fPIC
       )

       find_library(OPUS_LIB opus)
       if (NOT OPUS_LIB)
	 message(FATAL_ERROR "opus library not found, please install libopus develop package")
       endif()

       if (JUCE_LINUX_TARGET_ARCHITECTURE MATCHES "arm" )
          message(STATUS "ARM platform, adding -march=native")
          target_compile_options(${target_name} PUBLIC
           -march=native
          )
       endif()
     
       if (TARGET ${target_name}_Standalone)
           # make linux executable all lower case
           string(TOLOWER ${target_name} tmptargname)

           set_target_properties("${target_name}_Standalone"
	     PROPERTIES
	       OUTPUT_NAME ${tmptargname}
           )
       endif()

   endif()


    target_link_directories("${target_name}" INTERFACE
        ${LIB_PATHS}
    )

    target_link_libraries("${target_name}" 
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
	    juce::juce_cryptography
            juce::juce_audio_plugin_client
            
            ff_meters
            
            ${target_name}_SBData
            MEON_Fixtures

            opus
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
        #   juce::juce_recommended_warning_flags
        )

    if (APPLE)
        target_link_libraries("${target_name}" PRIVATE "-framework Security")   # 로그인 토큰 키체인
    endif()

endfunction()

# MEON: 독립 앱 + AU + VST3 (하나의 타깃)
sono_add_custom_plugin_target(MEON Meon "${FormatsToBuild}" FALSE  "Mjam")

# 개발용: 독립 앱을 빌드할 때마다 고정 경로(기본 /Applications/Meon.app)로 덮어쓴다.
# Dock 에 이 경로를 고정해 두면 항상 최신 빌드가 열린다. 끄려면 -DMEON_DEV_INSTALL_DIR=
if (APPLE AND TARGET MEON_Standalone)
    set(MEON_DEV_INSTALL_DIR "/Applications" CACHE PATH "빌드 후 Meon.app 을 복사할 폴더 (비우면 복사 안 함)")
    if (MEON_DEV_INSTALL_DIR)
        # rsync 는 새 파일을 임시 이름으로 쓴 뒤 바꿔치기하므로 실행 중인 앱이 있어도 안전하다.
        # 번들 폴더 자체는 그대로 두므로 Dock 고정·권한(마이크) 기록이 유지된다.
        add_custom_command(TARGET MEON_Standalone POST_BUILD
            COMMAND mkdir -p "${MEON_DEV_INSTALL_DIR}/Meon.app"
            COMMAND rsync -a --delete "$<TARGET_BUNDLE_DIR:MEON_Standalone>/" "${MEON_DEV_INSTALL_DIR}/Meon.app/"
            COMMAND codesign --force --sign - "${MEON_DEV_INSTALL_DIR}/Meon.app"
            COMMAND /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "${MEON_DEV_INSTALL_DIR}/Meon.app"
            COMMENT "Meon.app -> ${MEON_DEV_INSTALL_DIR}/Meon.app"
            VERBATIM)
    endif()
endif()

# 단위 테스트 (오디오 장치·화면 없이 도는 meon 코어). CI(linux.yml)에서 돌린다. -DMEON_TESTS=OFF 로 끈다.
option(MEON_TESTS "MEON 단위 테스트 타깃(MEON_Tests)" ON)
if (MEON_TESTS)
    juce_add_console_app(MEON_Tests PRODUCT_NAME "MEON_Tests")
    juce_generate_juce_header(MEON_Tests)
    target_sources(MEON_Tests PRIVATE
        Source/meon/tests/TestMain.cpp
        Source/meon/tests/EarGuardTests.cpp
        Source/meon/tests/ApiJsonTests.cpp
        Source/meon/tests/ValidationTests.cpp
        Source/meon/tests/FakeServerTests.cpp
        Source/meon/tests/AppStoreTests.cpp
        Source/meon/tests/MasonryTests.cpp
        Source/meon/tests/AuthTests.cpp
        Source/meon/tests/JamTests.cpp
        Source/meon/tests/HomeTests.cpp
        Source/meon/tests/RoomTests.cpp
        Source/meon/tests/ScriptSoakTests.cpp
        Source/meon/MeonEarGuard.cpp
        ${MEON_CORE_SOURCES})
    target_compile_features(MEON_Tests PRIVATE cxx_std_17)
    target_compile_options(MEON_Tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_compile_definitions(MEON_Tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        MEON_BUILD_ID=${MEON_BUILD_ID}
        MEON_BUILD_VERSION="${PROJECT_VERSION}"
        MEON_DEV_OPTIONS=${MEON_DEV_OPTIONS})
    target_link_libraries(MEON_Tests PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_cryptography
        MEON_Fixtures
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    if (APPLE)
        target_link_libraries(MEON_Tests PRIVATE "-framework Security")
    endif()
endif()

# Mobile targets
#sono_add_custom_plugin_target(SonoBusMobile "AUv3 Standalone" FALSE "NBus")

# (SonoBus 의 VSTi 변형 타깃은 MEON 에서 제외)

