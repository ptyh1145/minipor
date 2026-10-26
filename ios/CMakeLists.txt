cmake_minimum_required(VERSION 3.20)
project(voxel_preview LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Portable core: no platform dependencies, safe to compile for iOS (arm64/armv7) and host tools.
add_library(mw_core STATIC
    core/src/omod.cpp
    core/src/zip.cpp
    core/src/csv.cpp
    core/src/world.cpp
    core/src/terrain.cpp
    core/src/assets_bin.cpp
    core/src/player.cpp
)
target_include_directories(mw_core PUBLIC core/include)
target_compile_options(mw_core PRIVATE -Wall -Wextra)
# zlib is used by the data-pack reader (DEFLATE entries). Present on iOS/macOS/Linux.
find_package(ZLIB REQUIRED)
target_compile_definitions(mw_core PUBLIC MW_HAVE_ZLIB=1)
target_link_libraries(mw_core PUBLIC ZLIB::ZLIB)


# ---------------------------------------------------------------------------
# iOS: two targets, one code base.
#   mini_port_app         Metal       iOS 11+ / arm64      (CI-built, produces an IPA)
#   mini_port_app_legacy  OpenGL ES 2 iOS 6.1+ / armv7     (needs Xcode <= 5.x + iOS 6.1 SDK)
#
# Configure the legacy target for a real iOS 6 artifact with:
#   -DMW_IOS_LEGACY=ON -DCMAKE_OSX_ARCHITECTURES="armv7" -DCMAKE_OSX_DEPLOYMENT_TARGET=6.1
# Without MW_IOS_LEGACY the legacy target still builds with the modern SDK, which is only a
# compile check for the GL ES 2 backend.
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)

    set(MW_IOS_LEGACY OFF CACHE BOOL "Build the iOS 6 / armv7 / GL ES 2 target for real")

    # ---- modern: Metal ----
    add_executable(mini_port_app MACOSX_BUNDLE
        ios/app/main.mm
        ios/app/geometry.cpp
        ios/app/AssetModel.mm
        ios/app/TerrainScene.mm
        ios/app/Log.mm
        ios/metal/MetalBackend.mm
    )
    target_include_directories(mini_port_app PRIVATE core/include ios/app)
    target_compile_definitions(mini_port_app PRIVATE MW_RENDERER_METAL=1)
    target_link_libraries(mini_port_app PRIVATE mw_core)
    target_link_libraries(mini_port_app PRIVATE
        "-framework UIKit"
        "-framework Foundation"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework ImageIO"
    )
    set_target_properties(mini_port_app PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/app/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "MiniWorld"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.miniport.miniworld"
        MACOSX_BUNDLE_EXECUTABLE_NAME "MiniWorld"
        OUTPUT_NAME "MiniWorld"
        XCODE_ATTRIBUTE_PRODUCT_NAME "MiniWorld"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.miniport.miniworld"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    )

    # ---- legacy: OpenGL ES 2 ----
    add_executable(mini_port_app_legacy MACOSX_BUNDLE
        ios/app/main.mm
        ios/app/geometry.cpp
        ios/app/Log.mm
        ios/gles2/GLES2Backend.mm
    )
    target_include_directories(mini_port_app_legacy PRIVATE core/include ios/app)
    target_compile_definitions(mini_port_app_legacy PRIVATE MW_RENDERER_GLES2=1)
    target_link_libraries(mini_port_app_legacy PRIVATE mw_core)
    target_link_libraries(mini_port_app_legacy PRIVATE
        "-framework UIKit"
        "-framework Foundation"
        "-framework OpenGLES"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework ImageIO"
    )
    set_target_properties(mini_port_app_legacy PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/app/Info-legacy.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "MiniWorld"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.miniport.miniworld.legacy"
        MACOSX_BUNDLE_EXECUTABLE_NAME "MiniWorld-Legacy"
        OUTPUT_NAME "MiniWorld-Legacy"
        XCODE_ATTRIBUTE_PRODUCT_NAME "MiniWorld-Legacy"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.miniport.miniworld.legacy"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    )
    if(MW_IOS_LEGACY)
        set_target_properties(mini_port_app_legacy PROPERTIES
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "6.1"
            XCODE_ATTRIBUTE_ARCHS "armv7"
            XCODE_ATTRIBUTE_VALID_ARCHS "armv7"
            XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO"
        )
    endif()
endif()
