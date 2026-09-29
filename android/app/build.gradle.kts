// NFSMW Recompiled - APK de Android para Qualcomm Snapdragon (arm64-v8a)
//
// Antes de compilar hace falta el SDK preparado:
//     python tools/android/preparar_sdk.py
//
// Sin codigo generado (app/generated-android) sale un APK que solo lleva la
// sonda de Vulkan: se puede publicar y sirve para probar movil y driver.
// CON codigo generado, el APK lleva dentro el juego traducido: NO SE PUBLICA.

plugins {
    id("com.android.application")
}

val repo: File = rootProject.projectDir.parentFile
val sdkAndroid: File = (findProperty("nfsmw.sdk") as String?)?.let { file(it) }
    ?: File(repo.parentFile, "rexglue-sdk-android")
val conJuego = File(repo, "app/generated-android/default/sources.cmake").isFile

android {
    namespace = "io.github.nfsmwrecomp"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "io.github.nfsmwrecomp"
        // 29: memfd para adrenotools y un cargador de librerias con los espacios
        // de nombres que necesita. Por debajo no hay Snapdragon que merezca la pena.
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        buildConfigField("boolean", "CON_JUEGO", conJuego.toString())

        ndk {
            // Solo ARM64: el codigo generado, las rutas NEON y adrenotools.
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    // Release tambien en el APK de depuracion: el codigo
                    // generado a -O0 no se puede jugar.
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DNFSMW_REPO=${repo.absolutePath}",
                    "-DNFSMW_SDK=${sdkAndroid.absolutePath}",
                    "-DREX_BUILD_DIAGNOSTICS=OFF",
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    buildFeatures {
        buildConfig = true
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Firmado con la clave de depuracion local: el APK es para instalarlo
            // en TUS dispositivos, no para una tienda.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    packaging {
        jniLibs {
            // Obligatorio: adrenotools busca sus hooks en nativeLibraryDir y el
            // SDK carga librexgpu-xenos.so de ahi con dlopen. Sin esto las .so
            // se quedan dentro del APK y no existen como ficheros.
            useLegacyPackaging = true
        }
    }

    sourceSets {
        getByName("main") {
            // Las clases Java de SDL3 (SDLActivity y compania) se usan
            // directamente desde el submodulo del SDK, sin copiarlas.
            java.directories.add(
                File(sdkAndroid, "thirdparty/sdl3/android-project/app/src/main/java").absolutePath
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    dependencies {
        implementation("androidx.appcompat:appcompat:1.6.1")
        implementation("com.google.android.material:material:1.11.0")
    }
}
