# 🐱 Comedero Automático Inteligente para Gatos

Proyecto TI3042 – Aplicaciones Móviles para IoT (Unidad 2).
ESP32 + HC-SR04 + SG90 + App Android (Java) conectados en tiempo real con **Firebase Realtime Database**.

```
ComederoGato/
├── README.md                          ← esta guía (Secciones 1 a 5)
├── ENTREGA.txt                        ← bloc de notas para la tarea del AAI (link a GitHub)
├── APK/ComederoGato.apk               ← app lista para instalar en un celular Android 7.0+
├── capturas/                          ← pruebas de funcionamiento (Android 16 y Android 8.0)
├── firebase/
│   ├── estructura_inicial.json        ← árbol JSON para importar en la consola
│   ├── reglas_modo_prueba.json        ← reglas públicas (.read/.write = true)
│   └── reglas_recomendadas.json       ← reglas con validación (ISO 27400)
├── firmware/ComederoGato/
│   ├── ComederoGato.ino               ← firmware ESP32 (Arduino IDE)
│   └── sketch.yaml                    ← selecciona "ESP32 Dev Module" automáticamente
├── herramientas/
│   └── simulador_esp32.py             ← simula la ESP32 para probar la app sin la placa
└── android/ComederoGato/              ← proyecto completo de Android Studio (Java)
    ├── build.gradle.kts               ← nivel proyecto
    ├── settings.gradle.kts
    ├── gradle/libs.versions.toml      ← versiones de librerías y plugins
    └── app/
        ├── build.gradle.kts           ← nivel módulo
        ├── google-services.json       ← configuración del proyecto Firebase "Comedero Gato"
        └── src/main/
            ├── AndroidManifest.xml
            ├── java/cl/inacap/comederogato/
            │   ├── MainActivity.java
            │   └── HistorialAdapter.java
            └── res/layout/
                ├── activity_main.xml
                └── item_historial.xml
```

## Estado y pruebas realizadas (29-10-2026)

| Prueba | Resultado |
|---|---|
| Compilación app Android (Gradle 9.3.1, AGP 9.1.1, Firebase BoM 35.1.0) | ✅ BUILD SUCCESSFUL, sin errores |
| Compilación firmware ESP32 (esp32 3.3.12, Firebase ESP32 Client 4.4.17, ESP32Servo 3.2.1) | ✅ Compila (94 % de la memoria de programa) |
| Firebase real: proyecto `comedero-gato-a3d10`, Realtime Database en us-central1 | ✅ Creado, reglas publicadas, app Android registrada |
| Nivel de comida en tiempo real (ESP32 → Firebase → app) | ✅ 67 % → 57 % → 47 % visto en la app al instante |
| Alimentación manual (app → Firebase → ESP32 → `false` + historial) | ✅ Registro `2026-10-28 23:01:15` |
| Horario automático en el segundo 00 | ✅ Programado 23:04 desde la app, se disparó a las `23:04:00` |
| Historial ordenado (más reciente primero) | ✅ |
| Compatibilidad / 2 dispositivos | ✅ Emulador Android 16 (API 36) y Android 8.0 (API 26) con los mismos datos |
| Seguridad (ISO 27400): reglas `firebase/reglas_recomendadas.json` publicadas | ✅ App y ESP32 escriben normal; se rechazan horas inválidas, nivel fuera de 0–100, nodos basura y borrado del historial |

> La ESP32 física no se conectó en las pruebas: se usó `herramientas/simulador_esp32.py`, que ejecuta la misma lógica que el firmware contra Firebase. Uso: `python herramientas/simulador_esp32.py https://comedero-gato-a3d10-default-rtdb.firebaseio.com 10`

**Flujo de datos**

```
 App Android ──(true)──────────▶ /control/activarManual ──stream──▶ ESP32 ──▶ Servo 0°→90°→0°
 App Android ──("08:05")───────▶ /control/horarioFijo   ──stream──▶ ESP32 (compara con hora NTP)
 ESP32 ──(cada 5 s, %)─────────▶ /monitoreo/nivelComida ──listener─▶ App (indicador del depósito)
 ESP32 ──push("AAAA-MM-DD HH:MM:SS")▶ /historial_comidas ─listener─▶ App (lista, más reciente primero)
 ESP32 ──(false)───────────────▶ /control/activarManual  (evita que se repita la orden)
```

---

## SECCIÓN 1: Configuración de Google Firebase

### 1.1 Crear el proyecto y la Realtime Database en modo prueba

1. Entra a <https://console.firebase.google.com> con tu cuenta de Google.
2. Clic en **Crear un proyecto** (o *Agregar proyecto*).
3. Nombre: `comedero-gato` → **Continuar**.
4. Google Analytics: puedes **desactivarlo** (no se usa) → **Crear proyecto** → **Continuar**.
5. En el menú izquierdo: **Compilación (Build) → Realtime Database**.
6. Clic en **Crear base de datos**.
7. Ubicación: deja **Estados Unidos (us-central1)** → **Siguiente**.
8. Selecciona **Comenzar en modo de prueba** → **Habilitar**.
9. Ve a la pestaña **Reglas**. La consola pone reglas de prueba que **vencen a los 30 días** (`"now < 17…"`). Reemplázalas por las del archivo `firebase/reglas_modo_prueba.json`:

   ```json
   {
     "rules": {
       ".read": true,
       ".write": true
     }
   }
   ```

   Clic en **Publicar**. (Firebase mostrará una advertencia de seguridad; es esperado en modo prueba. Ver punto 1.4.)
10. Vuelve a la pestaña **Datos** y copia el enlace de arriba, por ejemplo
    `https://comedero-gato-1a2b3-default-rtdb.firebaseio.com/`.
    Lo usarás en el firmware (Sección 4) **sin** `https://` y **sin** la `/` final.

### 1.2 Estructura del árbol JSON

En la pestaña **Datos**: menú **⋮** (tres puntos) → **Importar JSON** → elige `firebase/estructura_inicial.json`.

```json
{
  "control": {
    "activarManual": false,
    "horarioFijo": "08:00"
  },
  "monitoreo": {
    "nivelComida": 0
  },
  "historial_comidas": {
    "-ComederoEjemplo001": "2026-10-28 08:00:00",
    "-ComederoEjemplo002": "2026-10-28 13:30:12"
  }
}
```

| Nodo | Tipo | Quién escribe | Quién lee |
|---|---|---|---|
| `/control/activarManual` | Booleano | App (`true`) y ESP32 (`false`) | ESP32 (stream) y App |
| `/control/horarioFijo` | String `HH:MM` (24 h) | App | ESP32 (stream) y App |
| `/monitoreo/nivelComida` | Entero 0–100 (%) | ESP32 cada 5 s | App |
| `/historial_comidas/<llave push>` | String `AAAA-MM-DD HH:MM:SS` | ESP32 en cada comida | App |

> Las llaves de `/historial_comidas` las genera `push()` automáticamente (ej. `-OaB3xY...`) y son cronológicas. Los dos registros de ejemplo puedes borrarlos cuando quieras. Firebase no muestra nodos vacíos: si borras todo el historial, el nodo desaparece hasta la próxima comida (es normal).

### 1.3 Descargar `google-services.json`

> Importante: crea la Realtime Database **antes** de descargar el archivo, así el archivo incluye el enlace de la base de datos.

1. En la consola, arriba a la izquierda: ⚙️ **Configuración del proyecto**.
2. En **Tus apps**, clic en el ícono de **Android**.
3. **Nombre del paquete de Android**: `cl.inacap.comederogato` (debe ser exacto).
4. Apodo: `Comedero Gato` → **Registrar app**.
5. Clic en **Descargar google-services.json**.
6. Pega el archivo en la carpeta **`android/ComederoGato/app/`** (al lado de `app/build.gradle.kts`).
   En Android Studio cambia la vista del panel izquierdo de **Android** a **Project** para verla: `ComederoGato → app → google-services.json`.
7. El nombre debe quedar exactamente `google-services.json` (sin `(1)` ni `(2)`).
8. Los pasos "Agregar SDK de Firebase" del asistente ya están hechos en los archivos Gradle de este proyecto: solo presiona **Siguiente** hasta terminar.

> **Alternativa vista en clase (Tools → Firebase):** en Android Studio, **Tools → Firebase → Realtime Database → Get started → Connect to Firebase** descarga el `google-services.json` en el lugar correcto. Si después presionas *Add the Realtime Database SDK*, el asistente puede duplicar líneas en `app/build.gradle.kts`; si pasa, deja solo las que trae este proyecto.

### 1.4 Seguridad (ISO 27400) – reglas recomendadas para la entrega

El modo prueba deja la base abierta a cualquiera que conozca el enlace. Cuando todo funcione, publica `firebase/reglas_recomendadas.json`. Sigue sin pedir usuario/clave (la ESP32 y la app siguen funcionando igual), pero:

- Solo permite escribir en `control`, `monitoreo` e `historial_comidas` (cualquier otro nodo se rechaza).
- Valida tipos: `activarManual` booleano, `horarioFijo` con formato `HH:MM` válido, `nivelComida` entre 0 y 100.
- El historial solo acepta registros **nuevos** con formato `AAAA-MM-DD HH:MM:SS`: nadie puede editarlo ni borrarlo desde un dispositivo (sí desde la consola).

Otras medidas aplicadas: toda la comunicación con Firebase va cifrada por HTTPS/TLS; la app pide confirmación antes de dispensar; el firmware valida el horario recibido y nunca dispensa dos veces en el mismo minuto; las credenciales Wi-Fi están en un solo lugar del firmware (**bórralas antes de subir el código a GitHub**).

---

## SECCIÓN 2: Entorno de desarrollo Android Studio

### 2.1 Descarga e instalación

1. Entra a <https://developer.android.com/studio> → **Download Android Studio** → acepta los términos → descarga el `.exe` para Windows.
2. Ejecuta el instalador con las opciones por defecto: deja marcados **Android Studio** y **Android Virtual Device** → **Next** → carpeta por defecto → **Install** → **Finish**.
3. Primer inicio: **Do not import settings** → **Next** → tipo de instalación **Standard** → tema a elección → **Next**.
4. Acepta **todas** las licencias (selecciona cada licencia de la lista y marca *Accept*) → **Finish**. Espera a que descargue el SDK (varios minutos).

### 2.2 SDK y emulador (AVD)

**SDK Manager** (`Tools → SDK Manager` o ⚙️ → *SDK Manager*):

1. Pestaña **SDK Platforms**: marca la versión más reciente de Android (API 36/37).
2. Pestaña **SDK Tools**: verifica marcados **Android SDK Build-Tools**, **Android Emulator**, **Android SDK Platform-Tools** y **Google Play services**.
3. **Apply** → **OK**.

**Device Manager** (`Tools → Device Manager`):

1. Clic en **+** → **Create Virtual Device**.
2. Categoría **Phone** → **Pixel 8** (o similar) → **Next**.
3. Imagen del sistema: la más reciente con **Google Play** (descárgala con la flecha ⬇ si hace falta) → **Next**.
4. Nombre `Pixel_Comedero` → **Finish**.
5. Presiona ▶ para encenderlo. El emulador ya tiene Internet, necesario para Firebase.

> Para el requisito de **dos dispositivos** de la guía: ejecuta la app en el emulador y en tu celular (activa *Opciones de desarrollador → Depuración USB*). Ambos verán los mismos datos en tiempo real.

### 2.3 Abrir el proyecto y archivos Gradle

1. **File → Open** → selecciona la carpeta `ComederoGato/android/ComederoGato` → **Trust Project**.
2. Copia `google-services.json` en `app/` (Sección 1.3).
3. Clic en **Sync Now** (o el ícono del elefante 🐘). Luego ▶ **Run 'app'**.

El proyecto usa el mismo formato que la plantilla de clase (Gradle Kotlin DSL `.kts` + catálogo `libs.versions.toml`, AGP 9.1.1, Gradle 9.3.1).

**`gradle/libs.versions.toml`** (catálogo de versiones):

```toml
[versions]
agp = "9.1.1"
googleServices = "4.5.0"
firebaseBom = "35.1.0"
appcompat = "1.8.0"
material = "1.14.0"
activity = "1.13.0"

[libraries]
androidx-appcompat = { group = "androidx.appcompat", name = "appcompat", version.ref = "appcompat" }
material = { group = "com.google.android.material", name = "material", version.ref = "material" }
androidx-activity = { group = "androidx.activity", name = "activity", version.ref = "activity" }
firebase-bom = { group = "com.google.firebase", name = "firebase-bom", version.ref = "firebaseBom" }
firebase-database = { group = "com.google.firebase", name = "firebase-database" }

[plugins]
android-application = { id = "com.android.application", version.ref = "agp" }
google-services = { id = "com.google.gms.google-services", version.ref = "googleServices" }
```

**`build.gradle.kts` (Project-level)** — el plugin de Google Services aquí reemplaza al antiguo `classpath`:

```kotlin
plugins {
    alias(libs.plugins.android.application) apply false
    // Plugin de Google Services (equivale al antiguo classpath "com.google.gms:google-services")
    alias(libs.plugins.google.services) apply false
}
```

**`app/build.gradle.kts` (Module-level)**:

```kotlin
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
```

> La versión de `firebase-database` la elige el BoM 35.1.0 (la estable actual, más nueva que 20.3.0). Si tu profesor exige escribir la versión a mano, cambia las dos líneas de Firebase por `implementation("com.google.firebase:firebase-database:20.3.0")`.

<details>
<summary><b>Alternativa en Groovy (si tu proyecto usa <code>build.gradle</code> sin <code>.kts</code> y <code>classpath</code>)</b></summary>

`build.gradle` (Project-level):

```groovy
buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        // Classpath de Google Services
        classpath 'com.google.gms:google-services:4.5.0'
    }
}

plugins {
    id 'com.android.application' version '9.1.1' apply false
}
```

`app/build.gradle` (Module-level):

```groovy
plugins {
    id 'com.android.application'
    id 'com.google.gms.google-services'
}

android {
    namespace 'cl.inacap.comederogato'
    compileSdk 36

    defaultConfig {
        applicationId 'cl.inacap.comederogato'
        minSdk 24
        targetSdk 36
        versionCode 1
        versionName '1.0'
    }

    compileOptions {
        sourceCompatibility JavaVersion.VERSION_11
        targetCompatibility JavaVersion.VERSION_11
    }
}

dependencies {
    implementation 'androidx.appcompat:appcompat:1.8.0'
    implementation 'com.google.android.material:material:1.14.0'
    implementation 'androidx.activity:activity:1.13.0'
    implementation platform('com.google.firebase:firebase-bom:35.1.0')
    implementation 'com.google.firebase:firebase-database'
}
```

</details>

**`AndroidManifest.xml`**: incluye el permiso de Internet (como en el PDF de clase):

```xml
<uses-permission android:name="android.permission.INTERNET" />
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
```

**Errores comunes al sincronizar**

| Error | Solución |
|---|---|
| `File google-services.json is missing` | Falta el archivo en `app/` (Sección 1.3). |
| `No matching client found for package name` | El paquete registrado en Firebase no es `cl.inacap.comederogato`. Registra otra app Android con ese nombre y vuelve a descargar el JSON. |
| `Failed to get FirebaseDatabase instance` | El JSON se descargó antes de crear la base. Pega el enlace en `DATABASE_URL` de `MainActivity.java` o descarga el JSON de nuevo. |
| `minSdk` o versión de Kotlin (PDF de clase) | Este proyecto ya usa `minSdk = 24` y es 100 % Java, no necesita ajustar Kotlin. |

---

## SECCIÓN 3: Hardware y conexiones (ESP32)

### 3.1 Montaje en la protoboard de 830 puntos

- Coloca la ESP32 **cruzando el canal divisorio central**: la fila de pines izquierda en un lado del canal y la derecha en el otro. Así cada pin queda en su propia columna de 5 agujeros y no se cortocircuitan los pines opuestos.
- La placa de 38 pines es ancha: normalmente deja libre **una sola fila** de agujeros por lado. Conecta los cables en esos agujeros libres. Si no queda espacio, usa cables hembra–macho directo a los pines o dos protoboards juntas.
- Usa los rieles laterales (+ / −) para distribuir **5V** y **GND**: un cable desde el pin `5V/V5` al riel rojo y otro desde `GND` al riel azul.
- Alimenta la ESP32 por el cable USB (el pin 5V entrega los 5 V del USB).

### 3.2 Mapa de cableado

| Componente | Pin del componente | Pin ESP32 | Color sugerido |
|---|---|---|---|
| HC-SR04 | VCC | **5V (V5)** | Rojo |
| HC-SR04 | GND | **GND** | Negro |
| HC-SR04 | Trig | **GPIO 12 (G12)** | Amarillo |
| HC-SR04 | Echo | **GPIO 13 (G13)** | Verde |
| Servo SG90 | Café / negro | **GND** | (cable del servo) |
| Servo SG90 | Rojo | **5V (V5)** | (cable del servo) |
| Servo SG90 | Naranjo / amarillo (señal PWM) | **GPIO 14 (G14)** | (cable del servo) |

```
              ESP32 (38 pines)
          ┌───────────────────────┐
   5V ●───┤ 5V/V5           G12  ├───● Trig  (HC-SR04)
  GND ●───┤ GND             G13  ├───● Echo  (HC-SR04)
          │                 G14  ├───● Señal (SG90, naranjo)
          └───────────────────────┘
  Riel 5V  ──► VCC HC-SR04, rojo SG90
  Riel GND ──► GND HC-SR04, café SG90
```

### 3.3 Precauciones

- **Echo a 5 V:** el HC-SR04 alimentado con 5 V devuelve 5 V por Echo y la ESP32 trabaja a 3,3 V. Muchos kits funcionan directo, pero lo seguro es un divisor de voltaje: **Echo → resistencia 1 kΩ → G13**, y **G13 → resistencia 2 kΩ → GND**. No cambia el código.
- **GPIO 12** es un pin de arranque de la ESP32. Como solo se conecta al Trig (entrada del sensor), no afecta. Si la placa no arranca al subir el código, desconecta el cable de G12 mientras carga.
- Si la ESP32 se reinicia cuando el servo se mueve, el USB no entrega suficiente corriente: usa un cable/cargador mejor o agrega un capacitor de 470 µF entre 5V y GND del servo.
- **Calibración del depósito:** el sensor va en la tapa, mirando hacia abajo. El código asume **20 cm = vacío (0 %)** y **4 cm = lleno (100 %)**. Si tu depósito mide distinto, cambia `DIST_VACIO_CM` y `DIST_LLENO_CM`.

---

## SECCIÓN 4: Firmware de la ESP32 (Arduino IDE)

Código completo: [`firmware/ComederoGato/ComederoGato.ino`](firmware/ComederoGato/ComederoGato.ino)

### 4.1 Preparar Arduino IDE

1. Instala Arduino IDE 2 desde <https://www.arduino.cc/en/software>.
2. **Archivo → Preferencias → URLs adicionales de gestor de placas**:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. **Herramientas → Placa → Gestor de placas** → busca `esp32` → instala **esp32 de Espressif Systems**.
4. **Herramientas → Administrar bibliotecas**, instala:
   - **Firebase ESP32 Client** (autor *Mobizt*), versión 4.4.17 → aporta `<FirebaseESP32.h>`.
   - **ESP32Servo** (autor *Kevin Harrington*) → aporta `<ESP32Servo.h>`.
   - `<WiFi.h>` y `<time.h>` ya vienen con el paquete esp32.
5. **Herramientas → Placa → esp32 → ESP32 Dev Module**, y elige el **Puerto COM** de tu placa.
6. Si no aparece el puerto, instala el driver **CP210x** o **CH340** (según el chip USB de tu placa).

> Probado: compila sin errores con el paquete **esp32 3.3.12** + Firebase ESP32 Client 4.4.17 + ESP32Servo 3.2.1 (usa el 94 % de la memoria de programa). Si agregas más código y no cabe, elige **Herramientas → Partition Scheme → Huge APP**.

### 4.2 Lo único que debes editar

```cpp
#define WIFI_SSID     "NOMBRE_DE_TU_RED"   // Nombre de tu red Wi-Fi (2.4 GHz)
#define WIFI_PASSWORD "CLAVE_DE_TU_RED"    // Contraseña de tu red Wi-Fi
#define DATABASE_URL  "comedero-gato-1a2b3-default-rtdb.firebaseio.com"   // Tu enlace, sin https:// ni / final
#define ZONA_HORARIA  "<-03>3"             // UTC-3. Chile con horario de verano: "<-04>4<-03>3,M9.1.6/24,M4.1.6/24"
```

Sube el código (→) y abre el **Monitor Serie a 115200 baudios**.

### 4.3 Qué hace y cómo cumple cada requisito

| Requisito | Implementación en el firmware |
|---|---|
| 1. Wi-Fi estable con reconexión | `conectarWiFi()` + `vigilarWiFi()`: reconexión automática de la ESP32, y si pasan 30 s sin red se fuerza `WiFi.begin()` otra vez. Nunca bloquea el `loop()`. |
| 2. Hora NTP UTC-3 | `configTzTime(ZONA_HORARIA, "pool.ntp.org", "time.google.com")`. `getLocalTime(&t, 10)` espera máximo 10 ms. |
| 3. Nivel cada 5 s | `enviarNivelComida()`: promedio de 3 lecturas del HC-SR04, convierte 20 cm→0 % y 4 cm→100 % y hace `Firebase.setInt("/monitoreo/nivelComida")`. |
| 4. Escucha de `activarManual` | **Stream** en `/control` (`Firebase.beginStream` + `setStreamCallback`): la ESP32 se entera al instante, sin consultar. Al recibir `true` llama a `dispensarComida()`; al terminar escribe `false`. |
| `dispensarComida()` | Servo 0°→90°, espera 1,5 s **sin `delay()`** (máquina de estados con `millis()`), vuelve a 0° y de inmediato escribe `activarManual = false`. |
| 5. Horario fijo | `verificarHorario()` compara `HH:MM` de la hora NTP con `/control/horarioFijo` y dispensa en el **segundo 00**. Tiene 5 s de margen por si el ciclo estaba ocupado justo en ese segundo, y nunca dispensa dos veces en el mismo minuto. |
| 6. Historial | Al cerrar la compuerta obtiene `"AAAA-MM-DD HH:MM:SS"` (`strftime`) y hace `Firebase.pushString("/historial_comidas")`. Si no hay Internet en ese momento, guarda hasta 10 registros y los sube al reconectar. |

**Salida esperada en el Monitor Serie:**

```
=== Comedero Automático para Gatos ===
[WiFi] Conectando a MiRed.....
[WiFi] Conectado. IP: 192.168.1.45
[Firebase] Cliente v4.4.17 iniciado.
[Stream] Escuchando /control en tiempo real.
[Stream] Cambio en /control/ (json)
[Sensor] 2026-10-28 08:04:55 | Distancia: 9.3 cm | Nivel: 67 %
[Horario] Son las 08:05:00. Hora programada alcanzada.
[Servo] Dispensando comida (origen: HORARIO). Abriendo a 90°
[Servo] Compuerta cerrada (0°). Origen: HORARIO
[Firebase] /control/activarManual = false
[Historial] Registrado 2026-10-28 08:05:01 (llave -OaB3xY...)
```

---

## SECCIÓN 5: Aplicación Android (Java)

Código completo en `android/ComederoGato/app/src/main/`:

| Archivo | Contenido |
|---|---|
| [`res/layout/activity_main.xml`](android/ComederoGato/app/src/main/res/layout/activity_main.xml) | Título, tarjeta con **% de comida** y barra de progreso, botón grande naranjo **Alimentación manual inmediata**, **TimePicker modo spinner 24 h**, botón **Guardar horario automático**, y **ListView** con fondo suave para el historial. |
| [`java/.../MainActivity.java`](android/ComederoGato/app/src/main/java/cl/inacap/comederogato/MainActivity.java) | Lógica y conexión con Firebase. |
| [`java/.../HistorialAdapter.java`](android/ComederoGato/app/src/main/java/cl/inacap/comederogato/HistorialAdapter.java) | Adaptador personalizado (`ArrayAdapter`) que muestra cada comida como hora + fecha `DD/MM/AAAA` y marca la más reciente como **ÚLTIMA**. |
| [`res/layout/item_historial.xml`](android/ComederoGato/app/src/main/res/layout/item_historial.xml) | Diseño de cada fila del historial. |
| `res/values/` | Colores, textos y tema. |

### Cómo cumple cada requisito de `MainActivity.java`

| Requisito | Implementación |
|---|---|
| Inicializar Firebase | `FirebaseDatabase.getInstance()` (o con `DATABASE_URL` si lo completas) y un `DatabaseReference` por nodo. |
| Botón manual | Pide confirmación y hace `refActivarManual.setValue(true)`. Mientras el valor está en `true`, el botón muestra "Dispensando comida…" en **todos** los dispositivos conectados. |
| Botón de horario | Lee `getHour()`/`getMinute()` del TimePicker, formatea con `String.format(Locale.US, "%02d:%02d", …)` (ej. `08:05`) y lo guarda en `/control/horarioFijo`. |
| Nivel en tiempo real | `ValueEventListener` en `/monitoreo/nivelComida`: actualiza el texto, la barra y el color (verde ≥ 50 %, amarillo ≥ 20 %, rojo < 20 %). Acepta número o texto. |
| Historial | `ValueEventListener` en `/historial_comidas` (últimos 50), ordena con `Collections.reverseOrder()` (más reciente primero) y actualiza el `ListView`. |
| Extra | Listener de `/control/horarioFijo` para mostrar el horario guardado y precargar el TimePicker. Los listeners se activan en `onStart()` y se liberan en `onStop()`. |

---

## Prueba completa (checklist de demostración)

1. Firebase con reglas publicadas y JSON importado.
2. ESP32 conectada: el Monitor Serie muestra `[Stream] Escuchando /control...`.
3. App abierta en **dos dispositivos** (emulador + celular).
4. Acerca/aleja la mano del sensor → el % cambia en ambos celulares en ~5 s.
5. Presiona **Alimentación manual** → el servo gira, `activarManual` vuelve a `false` y aparece un registro nuevo arriba del historial en ambos celulares.
6. Programa un horario 1–2 minutos adelante → **Guardar** → al llegar el minuto, en el segundo 00, el servo gira solo y se registra en el historial.
7. Desconecta el router unos segundos → la ESP32 se reconecta sola y sube lo pendiente.

> Nota sobre la guía TI3042: la guía indica Kotlin como lenguaje; este proyecto usa **Java** (también oficial para Android y compatible con Android Studio). Confírmalo con tu docente en la revisión del Paso 1.
