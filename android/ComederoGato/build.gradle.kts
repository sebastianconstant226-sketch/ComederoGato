// Archivo build.gradle.kts de nivel PROYECTO.
// Declara los plugins sin aplicarlos; cada módulo (app) los aplica después.
plugins {
    alias(libs.plugins.android.application) apply false
    // Plugin de Google Services (equivale al antiguo classpath "com.google.gms:google-services")
    alias(libs.plugins.google.services) apply false
}
