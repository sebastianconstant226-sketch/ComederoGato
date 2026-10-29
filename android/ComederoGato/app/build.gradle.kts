// Archivo build.gradle.kts de nivel MÓDULO (app).
plugins {
    alias(libs.plugins.android.application)
    // Lee app/google-services.json y configura Firebase automáticamente
    alias(libs.plugins.google.services)
}

android {
    namespace = "cl.inacap.comederogato"
    compileSdk {
        version = release(37) {
            minorApiLevel = 1
        }
    }

    defaultConfig {
        applicationId = "cl.inacap.comederogato"
        minSdk = 24
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    // Java 11 (cumple el requisito de Java 8 o superior)
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
}

dependencies {
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    implementation(libs.androidx.activity)

    // Firebase: el BoM fija versiones compatibles entre sí
    implementation(platform(libs.firebase.bom))
    implementation(libs.firebase.database)
}
