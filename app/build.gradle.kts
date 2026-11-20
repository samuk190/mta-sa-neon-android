import java.io.FileInputStream
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Load .env from root project if present
val envProperties = Properties()
val envFile = rootProject.file(".env")
if (envFile.exists()) {
    FileInputStream(envFile).use { envProperties.load(it) }
}
val launcherTitle = envProperties.getProperty("LAUNCHER_TITLE", "MTA:SA Neon Android")
val launcherPlayButtonText = envProperties.getProperty("LAUNCHER_PLAY_BUTTON_TEXT", "Play")
val defaultServerName = envProperties.getProperty("DEFAULT_SERVER_NAME", "MTA Neon Server")
val defaultServerIp = envProperties.getProperty("DEFAULT_SERVER_IP", "127.0.0.1")
val defaultServerPort = envProperties.getProperty("DEFAULT_SERVER_PORT", "22003")
val defaultServerLogoUrl = envProperties.getProperty("DEFAULT_SERVER_LOGO_URL", "")
val uiDisconnectUrl = envProperties.getProperty("UI_DISCONNECT_URL", "")
val defaultPlayerNick = envProperties.getProperty("DEFAULT_PLAYER_NICK", "Player")
val gameAssetsDir = envProperties.getProperty("GAME_ASSETS_DIR", "./game_assets")
val deviceAssetsPath = envProperties.getProperty("DEVICE_ASSETS_PATH", "/sdcard/Android/data/com.worldrpg.neonlauncher/files/game_assets")
val cdnRootUrl = envProperties.getProperty("CDN_ROOT_URL", "")

android {
    namespace = "com.worldrpg.neonlauncher"
    compileSdk = 34
    ndkVersion = "25.1.8937393"

    defaultConfig {
        applicationId = "com.worldrpg.neonlauncher"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0-neon-alpha"

        resValue("string", "app_name", launcherTitle)
        resValue("string", "connect", launcherPlayButtonText)

        // Restrict packaged resources to active locales. No density filter: a
        // single density bucket made every other screen scale one bitmap set.
        resourceConfigurations += listOf("en", "pt")

        ndk {
            // Somente arm64-v8a: o libGTASA.so do GTA SA Mobile 2.10 (War Drum) e'
            // exclusivamente aarch64. Incluir armeabi-v7a geraria APK com ABI
            // sem as libs nativas, que falha em runtime ao dlopen.
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
                arguments += listOf(
                    // Static STL: no libc++_shared.so in the APK. The NDK 25 copy of
                    // that library has 4 KB LOAD segments, which the 16 KB audit rejects.
                    // libGTASA.so does not link libc++, so no STL object crosses DSOs.
                    "-DANDROID_STL=c++_static",
                    "-DANDROID_ARM_NEON=TRUE"
                )
            }
        }

        buildConfigField("String", "LAUNCHER_TITLE", "\"${launcherTitle.replace("\"", "\\\"")}\"")
        buildConfigField("String", "LAUNCHER_PLAY_BUTTON_TEXT", "\"${launcherPlayButtonText.replace("\"", "\\\"")}\"")
        buildConfigField("String", "DEFAULT_SERVER_NAME", "\"${defaultServerName.replace("\"", "\\\"")}\"")
        buildConfigField("String", "DEFAULT_SERVER_IP", "\"${defaultServerIp}\"")
        buildConfigField("String", "DEFAULT_SERVER_LOGO_URL", "\"${defaultServerLogoUrl.replace("\"", "\\\"")}\"")
        buildConfigField("int", "DEFAULT_SERVER_PORT", defaultServerPort)
        buildConfigField("String", "UI_DISCONNECT_URL", "\"${uiDisconnectUrl}\"")
        buildConfigField("String", "DEFAULT_PLAYER_NICK", "\"${defaultPlayerNick.replace("\"", "\\\"")}\"")
        buildConfigField("String", "GAME_ASSETS_DIR", "\"${gameAssetsDir.replace("\\", "\\\\")}\"")
        buildConfigField("String", "DEVICE_ASSETS_PATH", "\"${deviceAssetsPath.replace("\\", "\\\\")}\"")
        buildConfigField("String", "CDN_ROOT_URL", "\"${cdnRootUrl.replace("\\", "\\\\")}\"")
    }

    // Release signing from keystore.properties (git-ignored): storeFile, storePassword, keyAlias, keyPassword.
    val keystoreFile = rootProject.file("keystore.properties")
    val releaseSigning = if (keystoreFile.exists()) {
        val props = Properties().apply { FileInputStream(keystoreFile).use { load(it) } }
        signingConfigs.create("release") {
            storeFile = file(props.getProperty("storeFile"))
            storePassword = props.getProperty("storePassword")
            keyAlias = props.getProperty("keyAlias")
            keyPassword = props.getProperty("keyPassword")
        }
    } else null

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (releaseSigning != null) signingConfig = releaseSigning
        }
        debug {
            isMinifyEnabled = false
            isDebuggable = true
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        viewBinding = true
        buildConfig = true
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
        jniLibs {
            // AGP 8.2 does not guarantee 16 KB ZIP offsets for stored libraries.
            // Extract compressed ELF files instead; their LOADs still need 16 KB alignment.
            useLegacyPackaging = true
        }
    }
}

// The public release APK carries no game code: libGTASA.so, libSCAnd.so and libOpenAL64.so are
// GTA San Andreas Mobile 2.10 files. Players copy them into the game folder (GameLibraries.java).
// The same game files sit in src/main/assets (american.gxt, fonts, scache lists, menu sounds,
// mainV1.scm, menu.pvr); the engine reads the game folder first (EngineAssetLoader), so the release
// leaves them out too. -PnoGameFiles applies the same rules to a debug build, for testing.
val noGameFiles = project.hasProperty("noGameFiles")
androidComponents {
    onVariants { variant ->
        if (variant.buildType != "release" && !noGameFiles) return@onVariants
        variant.packaging.jniLibs.excludes.addAll(
            listOf("**/libGTASA.so", "**/libSCAnd.so", "**/libOpenAL64.so")
        )
        variant.androidResources.ignoreAssetsPatterns.addAll(
            listOf("<dir>Text", "<dir>Textures", "<dir>audio", "<dir>data", "<dir>texdb",
                   "assetfile.txt", "scache*")
        )
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.11.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
    // SVG renderer for the svgCreate family (MtaDxView.renderSvg). Android has none built in.
    implementation("com.caverock:androidsvg-aar:1.4")
}

tasks.register<Exec>("pushAssetsToDevice") {
    group = "mta"
    description = "Pushes GTA Mobile 2.10 assets from GAME_ASSETS_DIR to connected Android device via ADB"
    commandLine("bash", "${rootProject.projectDir}/tools/sync_local_assets.sh")
}
