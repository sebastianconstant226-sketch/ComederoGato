/*
 * =====================================================================
 *  COMEDERO AUTOMÁTICO INTELIGENTE PARA GATOS - FIRMWARE ESP32
 *  Asignatura TI3042 - Aplicaciones Móviles para IoT
 * =====================================================================
 *  Placa:      NodeMCU ESP32 (38 pines) -> en Arduino IDE: "ESP32 Dev Module"
 *  Sensores:   HC-SR04 (Trig G12, Echo G13)
 *  Actuador:   Servo SG90 (Señal G14)
 *  Nube:       Firebase Realtime Database
 *
 *  Librerías necesarias (Arduino IDE > Herramientas > Administrar bibliotecas):
 *    - "Firebase ESP32 Client" de Mobizt (versión 4.4.17)
 *    - "ESP32Servo" de Kevin Harrington / John K. Bennett (versión 3.x)
 *  Gestor de placas: "esp32" de Espressif Systems.
 *
 *  Nodos usados en Firebase:
 *    /control/activarManual     (bool)    App -> ESP32
 *    /control/horarioFijo       ("HH:MM") App -> ESP32
 *    /monitoreo/nivelComida     (int %)   ESP32 -> App
 *    /historial_comidas/<push>  ("AAAA-MM-DD HH:MM:SS") ESP32 -> App
 * =====================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <ESP32Servo.h>
#include <time.h>

// ---------------------------------------------------------------------
// 1) CREDENCIALES WI-FI  (la ESP32 solo funciona con redes de 2.4 GHz)
// ---------------------------------------------------------------------
#define WIFI_SSID     "NOMBRE_DE_TU_RED"   // <-- Escribe aquí el nombre exacto de tu red Wi-Fi
#define WIFI_PASSWORD "CLAVE_DE_TU_RED"    // <-- Escribe aquí la contraseña de tu red Wi-Fi

// ---------------------------------------------------------------------
// 2) ENLACE DE FIREBASE REALTIME DATABASE
//    Cópialo desde Firebase Console > Realtime Database > pestaña "Datos".
//    Escríbelo SIN "https://" y SIN "/" final. Ejemplos válidos:
//      comedero-gato-1a2b3-default-rtdb.firebaseio.com
//      comedero-gato-1a2b3-default-rtdb.southamerica-east1.firebasedatabase.app
// ---------------------------------------------------------------------
#define DATABASE_URL "comedero-gato-a3d10-default-rtdb.firebaseio.com"   // <-- Enlace de TU base de datos (proyecto "Comedero Gato")

// ---------------------------------------------------------------------
// 3) ZONA HORARIA Y SERVIDORES NTP
//    Formato POSIX: "<-03>3" = UTC-3 fijo.
//    Para Chile continental con cambio de horario automático usa:
//      "<-04>4<-03>3,M9.1.6/24,M4.1.6/24"
// ---------------------------------------------------------------------
#define ZONA_HORARIA "<-03>3"
#define NTP_SERVIDOR_1 "pool.ntp.org"
#define NTP_SERVIDOR_2 "time.google.com"

// ---------------------------------------------------------------------
// 4) PINES (respetan el cableado de la Sección 3)
// ---------------------------------------------------------------------
const uint8_t PIN_TRIG  = 12;   // HC-SR04 Trig  -> G12
const uint8_t PIN_ECHO  = 13;   // HC-SR04 Echo  -> G13
const uint8_t PIN_SERVO = 14;   // SG90 señal    -> G14

// ---------------------------------------------------------------------
// 5) PARÁMETROS DEL SISTEMA
// ---------------------------------------------------------------------
const float DIST_VACIO_CM = 20.0;                 // 20 cm = depósito vacío (0 %)
const float DIST_LLENO_CM = 4.0;                  // 4 cm  = depósito lleno (100 %)
const int   ANGULO_CERRADO = 0;                   // Compuerta cerrada
const int   ANGULO_ABIERTO = 90;                  // Compuerta abierta
const unsigned long TIEMPO_APERTURA_MS   = 1500;  // Tiempo que la compuerta queda abierta
const unsigned long INTERVALO_NIVEL_MS   = 5000;  // Subir nivel de comida cada 5 s
const unsigned long INTERVALO_STREAM_MS  = 10000; // Reintento de inicio del stream
const unsigned long ESPERA_RECONEXION_MS = 30000; // Forzar reconexión Wi-Fi tras 30 s sin red
const unsigned long INTERVALO_REINTENTO_MS = 3000; // Reintento de escrituras pendientes
const int   TOLERANCIA_SEGUNDOS = 5;              // El horario se dispara en el segundo 00 (con margen de 5 s si el ciclo estaba ocupado)

// Rutas de Firebase
const char *RUTA_CONTROL   = "/control";
const char *RUTA_MANUAL    = "/control/activarManual";
const char *RUTA_NIVEL     = "/monitoreo/nivelComida";
const char *RUTA_HISTORIAL = "/historial_comidas";

// ---------------------------------------------------------------------
// OBJETOS GLOBALES
// ---------------------------------------------------------------------
FirebaseData fbdo;        // Conexión para escrituras (nivel, historial, reset)
FirebaseData fbdoStream;  // Conexión dedicada a la escucha en tiempo real de /control
FirebaseAuth auth;
FirebaseConfig config;
Servo servoCompuerta;

// Datos compartidos entre el callback del stream (otra tarea) y el loop
volatile bool solicitudManual = false;
portMUX_TYPE muxHorario = portMUX_INITIALIZER_UNLOCKED;
char horarioFijo[6] = "";   // "HH:MM" recibido desde la app

// Máquina de estados del dispensador (no bloquea la placa)
enum EstadoDispensador { REPOSO, ABIERTO };
EstadoDispensador estadoDispensador = REPOSO;
unsigned long inicioApertura = 0;
const char *origenDispensado = "";

// Tareas pendientes de escritura en Firebase (por si se cae la red)
bool pendienteApagarManual = false;
const int MAX_COLA_HISTORIAL = 10;
char colaHistorial[MAX_COLA_HISTORIAL][20];
int  totalColaHistorial = 0;

// Control de tiempos
unsigned long ultimoEnvioNivel = 0;
unsigned long ultimoIntentoStream = 0;
unsigned long ultimoReintento = 0;
unsigned long inicioDesconexion = 0;
bool streamActivo = false;
bool wifiEstabaConectado = false;
long ultimaClaveProgramada = -1;   // Evita alimentar dos veces en el mismo minuto

// =====================================================================
//  WI-FI
// =====================================================================
void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("[WiFi] Conectando a ");
  Serial.print(WIFI_SSID);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 20000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiEstabaConectado = true;
    Serial.print("[WiFi] Conectado. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Sin conexión por ahora. Se seguirá reintentando en segundo plano.");
    inicioDesconexion = millis();
  }
}

// Reconexión automática sin congelar el loop
void vigilarWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiEstabaConectado) {
      wifiEstabaConectado = true;
      Serial.print("[WiFi] Reconectado. IP: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }

  if (wifiEstabaConectado) {
    wifiEstabaConectado = false;
    inicioDesconexion = millis();
    Serial.println("[WiFi] Señal perdida. Intentando reconectar...");
  }

  if (millis() - inicioDesconexion >= ESPERA_RECONEXION_MS) {
    Serial.println("[WiFi] Forzando reconexión...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    inicioDesconexion = millis();
  }
}

// =====================================================================
//  HORA (NTP)
// =====================================================================
bool obtenerHora(struct tm &t) {
  return getLocalTime(&t, 10);   // Espera máx. 10 ms: no bloquea
}

// Devuelve "AAAA-MM-DD HH:MM:SS" o "" si aún no hay hora sincronizada
String fechaHoraActual() {
  struct tm t;
  if (!obtenerHora(t)) {
    return "";
  }
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &t);
  return String(buffer);
}

// =====================================================================
//  SENSOR ULTRASÓNICO HC-SR04
// =====================================================================
float medirDistanciaCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long duracion = pulseIn(PIN_ECHO, HIGH, 30000UL);   // Máx. 30 ms (~5 m)
  if (duracion == 0) {
    return -1.0;   // Sin eco
  }
  return (duracion * 0.0343f) / 2.0f;
}

// Promedio de 3 lecturas válidas para filtrar ruido
float medirDistanciaPromedio() {
  float suma = 0;
  int validas = 0;
  for (int i = 0; i < 3; i++) {
    float d = medirDistanciaCm();
    if (d > 0) {
      suma += d;
      validas++;
    }
    delay(30);
  }
  if (validas == 0) {
    return -1.0;
  }
  return suma / validas;
}

// 20 cm = 0 %, 4 cm = 100 %
int calcularPorcentaje(float distancia) {
  if (distancia <= DIST_LLENO_CM) return 100;
  if (distancia >= DIST_VACIO_CM) return 0;
  float porcentaje = (DIST_VACIO_CM - distancia) * 100.0f / (DIST_VACIO_CM - DIST_LLENO_CM);
  return (int)lroundf(porcentaje);
}

void enviarNivelComida() {
  float distancia = medirDistanciaPromedio();
  if (distancia < 0) {
    Serial.println("[Sensor] Sin lectura del HC-SR04. Revisa el cableado.");
    return;
  }

  int porcentaje = calcularPorcentaje(distancia);
  String hora = fechaHoraActual();
  Serial.printf("[Sensor] %s | Distancia: %.1f cm | Nivel: %d %%\n",
                hora.length() ? hora.c_str() : "sin hora", distancia, porcentaje);

  if (!Firebase.ready()) {
    return;
  }
  if (!Firebase.setInt(fbdo, RUTA_NIVEL, porcentaje)) {
    Serial.printf("[Firebase] Error al subir nivel: %s\n", fbdo.errorReason().c_str());
  }
}

// =====================================================================
//  DISPENSADOR (SERVO SG90)
// =====================================================================

// Inicia la apertura. El cierre lo hace actualizarDispensador() a los 1.5 s.
void dispensarComida(const char *origen) {
  if (estadoDispensador != REPOSO) {
    Serial.println("[Servo] Ya se está dispensando; se ignora la nueva orden.");
    return;
  }
  origenDispensado = origen;
  Serial.printf("[Servo] Dispensando comida (origen: %s). Abriendo a %d°\n", origen, ANGULO_ABIERTO);
  servoCompuerta.write(ANGULO_ABIERTO);
  inicioApertura = millis();
  estadoDispensador = ABIERTO;
}

void encolarHistorial(const String &fechaHora) {
  if (fechaHora.length() == 0) {
    Serial.println("[Historial] No hay hora NTP sincronizada; no se registra esta comida.");
    return;
  }
  if (totalColaHistorial >= MAX_COLA_HISTORIAL) {
    Serial.println("[Historial] Cola llena; se descarta el registro más antiguo.");
    for (int i = 1; i < MAX_COLA_HISTORIAL; i++) {
      strcpy(colaHistorial[i - 1], colaHistorial[i]);
    }
    totalColaHistorial = MAX_COLA_HISTORIAL - 1;
  }
  strncpy(colaHistorial[totalColaHistorial], fechaHora.c_str(), sizeof(colaHistorial[0]) - 1);
  colaHistorial[totalColaHistorial][sizeof(colaHistorial[0]) - 1] = '\0';
  totalColaHistorial++;
}

// Escribe en Firebase lo pendiente: reset de activarManual y registros de historial
void procesarPendientes() {
  if (!Firebase.ready()) {
    return;
  }

  if (pendienteApagarManual) {
    if (Firebase.setBool(fbdo, RUTA_MANUAL, false)) {
      pendienteApagarManual = false;
      Serial.println("[Firebase] /control/activarManual = false");
    } else {
      Serial.printf("[Firebase] Error al reiniciar activarManual: %s\n", fbdo.errorReason().c_str());
      return;
    }
  }

  while (totalColaHistorial > 0) {
    if (Firebase.pushString(fbdo, RUTA_HISTORIAL, colaHistorial[0])) {
      Serial.printf("[Historial] Registrado %s (llave %s)\n", colaHistorial[0], fbdo.pushName().c_str());
      for (int i = 1; i < totalColaHistorial; i++) {
        strcpy(colaHistorial[i - 1], colaHistorial[i]);
      }
      totalColaHistorial--;
    } else {
      Serial.printf("[Historial] Error al registrar: %s\n", fbdo.errorReason().c_str());
      return;
    }
  }
}

// Cierra la compuerta cuando pasan 1.5 s, sin usar delay()
void actualizarDispensador() {
  if (estadoDispensador != ABIERTO) {
    return;
  }
  if (millis() - inicioApertura < TIEMPO_APERTURA_MS) {
    return;
  }

  servoCompuerta.write(ANGULO_CERRADO);
  estadoDispensador = REPOSO;
  Serial.printf("[Servo] Compuerta cerrada (%d°). Origen: %s\n", ANGULO_CERRADO, origenDispensado);

  pendienteApagarManual = true;           // Volver activarManual a false para evitar bucles
  encolarHistorial(fechaHoraActual());    // Registrar "AAAA-MM-DD HH:MM:SS"
  procesarPendientes();                   // Se envía de inmediato si hay conexión
}

// =====================================================================
//  HORARIO AUTOMÁTICO
// =====================================================================
void guardarHorario(String valor) {
  valor.trim();
  // Acepta "8:05" y lo normaliza a "08:05"
  if (valor.length() == 4 && valor.charAt(1) == ':') {
    valor = "0" + valor;
  }
  bool valido = valor.length() == 5 && valor.charAt(2) == ':' &&
                isDigit(valor.charAt(0)) && isDigit(valor.charAt(1)) &&
                isDigit(valor.charAt(3)) && isDigit(valor.charAt(4));
  if (!valido) {
    valor = "";
  }

  portENTER_CRITICAL(&muxHorario);
  strncpy(horarioFijo, valor.c_str(), sizeof(horarioFijo) - 1);
  horarioFijo[sizeof(horarioFijo) - 1] = '\0';
  portEXIT_CRITICAL(&muxHorario);
}

void verificarHorario() {
  struct tm t;
  if (!obtenerHora(t)) {
    return;
  }

  char programado[6];
  portENTER_CRITICAL(&muxHorario);
  strcpy(programado, horarioFijo);
  portEXIT_CRITICAL(&muxHorario);

  if (programado[0] == '\0') {
    return;
  }

  char actual[6];
  snprintf(actual, sizeof(actual), "%02d:%02d", t.tm_hour, t.tm_min);

  // Clave única por minuto (año + día del año + minuto) para no repetir
  long claveMinuto = (long)(t.tm_year % 100) * 527040L + (long)t.tm_yday * 1440L + t.tm_hour * 60L + t.tm_min;

  if (strcmp(actual, programado) == 0 && t.tm_sec <= TOLERANCIA_SEGUNDOS && claveMinuto != ultimaClaveProgramada) {
    ultimaClaveProgramada = claveMinuto;
    Serial.printf("[Horario] Son las %s:%02d. Hora programada alcanzada.\n", actual, t.tm_sec);
    dispensarComida("HORARIO");
  }
}

// =====================================================================
//  FIREBASE: ESCUCHA EN TIEMPO REAL (STREAM) DE /control
// =====================================================================
bool jsonEsVerdadero(FirebaseJsonData &dato) {
  if (dato.typeNum == FirebaseJson::JSON_BOOL) {
    return dato.to<bool>();
  }
  if (dato.typeNum == FirebaseJson::JSON_STRING) {
    return dato.to<String>() == "true";
  }
  return false;
}

// Se ejecuta en otra tarea: solo actualiza variables, el trabajo pesado lo hace loop()
void streamCallback(StreamData data) {
  String ruta = data.dataPath();
  String tipo = data.dataType();

  if (ruta == "/") {
    // Primer evento (o reemplazo completo de /control): llega todo el nodo como JSON
    if (tipo == "json") {
      FirebaseJson *json = data.to<FirebaseJson *>();
      FirebaseJsonData dato;

      json->get(dato, "activarManual");
      if (dato.success && jsonEsVerdadero(dato)) {
        solicitudManual = true;
      }

      json->get(dato, "horarioFijo");
      if (dato.success) {
        guardarHorario(dato.to<String>());
      }
    } else if (tipo == "null") {
      guardarHorario("");
    }
  } else if (ruta == "/activarManual") {
    if ((tipo == "boolean" && data.to<bool>()) || (tipo == "string" && data.to<String>() == "true")) {
      solicitudManual = true;
    }
  } else if (ruta == "/horarioFijo") {
    if (tipo == "string") {
      guardarHorario(data.to<String>());
    } else {
      guardarHorario("");
    }
  }

  Serial.printf("[Stream] Cambio en /control%s (%s)\n", ruta.c_str(), tipo.c_str());
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("[Stream] Tiempo de espera agotado, reanudando...");
  }
  if (!fbdoStream.httpConnected()) {
    Serial.printf("[Stream] Error %d: %s\n", fbdoStream.httpCode(), fbdoStream.errorReason().c_str());
  }
}

void iniciarStreamSiFalta() {
  if (streamActivo || !Firebase.ready()) {
    return;
  }
  if (ultimoIntentoStream != 0 && millis() - ultimoIntentoStream < INTERVALO_STREAM_MS) {
    return;
  }
  ultimoIntentoStream = millis();

  if (Firebase.beginStream(fbdoStream, RUTA_CONTROL)) {
    Firebase.setStreamCallback(fbdoStream, streamCallback, streamTimeoutCallback);
    streamActivo = true;
    Serial.println("[Stream] Escuchando /control en tiempo real.");
  } else {
    Serial.printf("[Stream] No se pudo iniciar: %s\n", fbdoStream.errorReason().c_str());
  }
}

void iniciarFirebase() {
  config.database_url = DATABASE_URL;   // Enlace de la base de datos (definido arriba)
  config.signer.test_mode = true;       // Modo prueba: sin autenticación (reglas .read/.write en true)

  Firebase.reconnectNetwork(true);      // La librería también ayuda a recuperar el Wi-Fi

  fbdo.setBSSLBufferSize(2048, 1024);
  fbdoStream.setBSSLBufferSize(2048, 1024);
  fbdoStream.keepAlive(5, 5, 1);        // Detecta rápido si se corta la conexión del stream

  Firebase.begin(&config, &auth);
  Serial.printf("[Firebase] Cliente v%s iniciado.\n", FIREBASE_CLIENT_VERSION);
}

// =====================================================================
//  SETUP Y LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Comedero Automático para Gatos ===");

  // Sensor ultrasónico
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  // Servo: 50 Hz, pulso 500-2400 µs (rango típico del SG90)
  ESP32PWM::allocateTimer(0);
  servoCompuerta.setPeriodHertz(50);
  servoCompuerta.attach(PIN_SERVO, 500, 2400);
  servoCompuerta.write(ANGULO_CERRADO);

  conectarWiFi();

  // Hora local por NTP (la sincronización sigue en segundo plano)
  configTzTime(ZONA_HORARIA, NTP_SERVIDOR_1, NTP_SERVIDOR_2);

  iniciarFirebase();
  iniciarStreamSiFalta();
}

void loop() {
  vigilarWiFi();
  iniciarStreamSiFalta();

  // Orden manual recibida desde la app
  if (solicitudManual) {
    solicitudManual = false;
    dispensarComida("MANUAL");
  }

  verificarHorario();
  actualizarDispensador();

  // Nivel del depósito cada 5 segundos
  if (millis() - ultimoEnvioNivel >= INTERVALO_NIVEL_MS) {
    ultimoEnvioNivel = millis();
    enviarNivelComida();
  }

  // Reintenta cada 3 s las escrituras que fallaron por falta de red
  if ((pendienteApagarManual || totalColaHistorial > 0) && millis() - ultimoReintento >= INTERVALO_REINTENTO_MS) {
    ultimoReintento = millis();
    procesarPendientes();
  }

  delay(5);
}
