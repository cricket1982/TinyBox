plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.tinybox.app"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.tinybox.app"
        minSdk = 26          // AAudio
        targetSdk = 35
        versionCode = 8
        // a release tag like "v0.3" becomes the version name on GitHub builds
        versionName = System.getenv("VERSION_NAME")?.removePrefix("v")?.ifEmpty { null } ?: "0.8"
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
        externalNativeBuild { cmake { cppFlags += "-std=c++17" } }
    }

    // One signing key for every build, so GitHub builds install as updates over
    // the app you installed from Android Studio (and keep your saved projects).
    // On your PC this is Android Studio's own debug key; on GitHub it comes from
    // the SIGNING_KEYSTORE_B64 secret (see GITHUB_SETUP.md).
    signingConfigs {
        create("shared") {
            val ks = System.getenv("SIGNING_KEYSTORE_PATH")
            storeFile = if (ks != null) file(ks) else file("${System.getProperty("user.home")}/.android/debug.keystore")
            storePassword = System.getenv("SIGNING_STORE_PASSWORD")?.ifEmpty { null } ?: "android"
            keyAlias = System.getenv("SIGNING_KEY_ALIAS")?.ifEmpty { null } ?: "androiddebugkey"
            keyPassword = System.getenv("SIGNING_KEY_PASSWORD")?.ifEmpty { null } ?: "android"
        }
    }
    buildTypes {
        debug {
            signingConfig = signingConfigs.getByName("shared")
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("shared")
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
    kotlinOptions { jvmTarget = "17" }
}
