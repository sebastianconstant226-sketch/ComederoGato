"""
Simulador de la ESP32 del Comedero (para probar la app sin la placa).

Hace lo mismo que firmware/ComederoGato/ComederoGato.ino, pero usando la
API REST de Firebase Realtime Database:
  - Sube /monitoreo/nivelComida cada 5 s (el nivel baja con cada comida).
  - Revisa /control cada 1 s: si activarManual == true, "dispensa".
  - Si la hora actual (UTC-3) coincide con /control/horarioFijo en el segundo 00, "dispensa".
  - Al dispensar: espera 1,5 s, vuelve activarManual a false y agrega
    "AAAA-MM-DD HH:MM:SS" en /historial_comidas (push).

Uso (solo librería estándar de Python 3):
    python simulador_esp32.py https://TU-PROYECTO-default-rtdb.firebaseio.com [minutos]
"""

import json
import sys
import time
import urllib.request
from datetime import datetime, timedelta, timezone

ZONA = timezone(timedelta(hours=-3))      # UTC-3, igual que el firmware
DIST_VACIO_CM = 20.0
DIST_LLENO_CM = 4.0
INTERVALO_NIVEL_S = 5
TOLERANCIA_SEG = 5


def pedir(base, ruta, metodo="GET", datos=None):
    url = f"{base}/{ruta}.json"
    cuerpo = None if datos is None else json.dumps(datos).encode("utf-8")
    req = urllib.request.Request(url, data=cuerpo, method=metodo,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read().decode("utf-8"))


def porcentaje(distancia):
    if distancia <= DIST_LLENO_CM:
        return 100
    if distancia >= DIST_VACIO_CM:
        return 0
    return round((DIST_VACIO_CM - distancia) * 100 / (DIST_VACIO_CM - DIST_LLENO_CM))


def log(msg):
    print(f"[{datetime.now(ZONA):%H:%M:%S}] {msg}", flush=True)


def dispensar(base, origen, estado):
    log(f"[Servo] Dispensando comida (origen: {origen}). Abriendo a 90°")
    time.sleep(1.5)
    log("[Servo] Compuerta cerrada (0°)")
    pedir(base, "control/activarManual", "PUT", False)
    log("[Firebase] /control/activarManual = false")
    fecha = datetime.now(ZONA).strftime("%Y-%m-%d %H:%M:%S")
    llave = pedir(base, "historial_comidas", "POST", fecha)["name"]
    log(f"[Historial] Registrado {fecha} (llave {llave})")
    estado["distancia"] = min(DIST_VACIO_CM, estado["distancia"] + 1.6)  # baja la comida


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    base = sys.argv[1].rstrip("/")
    minutos = float(sys.argv[2]) if len(sys.argv) > 2 else 10
    fin = time.time() + minutos * 60

    estado = {"distancia": 9.3}
    ultimo_nivel = 0.0
    ultima_clave_horario = None
    log(f"=== Simulador ESP32 conectado a {base} (durante {minutos:g} min) ===")

    while time.time() < fin:
        ahora = datetime.now(ZONA)

        if time.time() - ultimo_nivel >= INTERVALO_NIVEL_S:
            ultimo_nivel = time.time()
            pct = porcentaje(estado["distancia"])
            pedir(base, "monitoreo/nivelComida", "PUT", pct)
            log(f"[Sensor] Distancia: {estado['distancia']:.1f} cm | Nivel: {pct} %")

        control = pedir(base, "control") or {}
        if control.get("activarManual") in (True, "true"):
            dispensar(base, "MANUAL", estado)

        horario = str(control.get("horarioFijo", ""))
        clave = ahora.strftime("%Y-%m-%d %H:%M")
        if horario == ahora.strftime("%H:%M") and ahora.second <= TOLERANCIA_SEG and clave != ultima_clave_horario:
            ultima_clave_horario = clave
            log(f"[Horario] Son las {ahora:%H:%M:%S}. Hora programada alcanzada.")
            dispensar(base, "HORARIO", estado)

        time.sleep(1)

    log("=== Simulador detenido ===")


if __name__ == "__main__":
    main()
