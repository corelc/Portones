// =================================================================================
// 0. INCLUDES
// =================================================================================
#include <Arduino.h>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <time.h>

// === Config ===
#include "Config.h"
#include "Config_Hardware.h"
#include "secrets.h"

// === Servicios ===
#include "WiFiManager.h"
#include "WebUI.h"
#include "Memoria.h"
#include "RoleManager.h"

// =================================================================================
// 1. PROTOTIPOS
// =================================================================================
void registrarEvento(String msg, String userForzado = "");

// === Core ===
void actualizarEstadoPorton();
void gestionarPulso();

// === Entradas / seguridad ===
void procesarEntradasUsuario();
void procesarBarrera();
void procesarSeguridad();

// === Actuadores / UI ===
void gestionarSirena();
void gestionarSemaforo();
void gestionarLedsPlaca();
void gestionarLedConfig();
void procesarBuzzer();
void beep(uint8_t veces);



// =================================================================================
// 2. HELPERS
// =================================================================================
inline bool entradaActiva(uint8_t pin) {
  return digitalRead(pin) == LOW;
}

// =================================================================================
// 3. VARIABLES Y ESTADOS
// =================================================================================

// ===================== ESTADOS DEL PORTÓN =================
enum EstadoPorton {
  ESTADO_DESCONOCIDO,
  ESTADO_CERRADO,
  ESTADO_ABIERTO,
  ESTADO_ABRIENDO,
  ESTADO_CERRANDO,
  ESTADO_ERROR_SENSORES,
  ESTADO_FALLA_MECANICA
};

// ===================== ESTADOS DE SEGURIDAD ===============
enum EstadoSeguridad {
  SEG_NORMAL,
  SEG_DISPARADA,
  SEG_LATENTE,
  SEG_OBSTACULO
};

// ===================== ESTADOS DE SIRENA ==================
enum EstadoSirena {
  SIR_APAGADA,
  SIR_SONANDO,
  SIR_PAUSA,
  SIR_BEEP_ERROR
};

// ===================== LED CONFIG =========================
enum LedConfigModo {
  LEDCFG_IDLE,
  LEDCFG_CONFIRM_1S,
  LEDCFG_CONFIRM_5S,
  LEDCFG_CONFIRM_10S,
  LEDCFG_CONFIRM_15S,
  LEDCFG_LEARN,
  LEDCFG_EXIT_FLASH
};

// ===================== ESTADOS PRINCIPALES ================
EstadoPorton    estadoPortonActual = ESTADO_DESCONOCIDO;
EstadoPorton    estadoPortonPrevio = ESTADO_DESCONOCIDO;
EstadoSeguridad estadoSeguridad    = SEG_NORMAL;
EstadoSirena    estadoSirena       = SIR_APAGADA;

int estadoPortonUI    = 0;
int estadoSeguridadUI = 0;

// ===================== FLAGS ==============================
bool sistemaInicializado = false;
bool solicitudPulso      = false;
bool emergenciaActiva    = false;
bool panicoEnclavado     = false;
bool panicoDisparadoEnEstaPulsacion = false;
bool modoMantenimiento  = false;
bool beepPendiente      = false;
bool portonCerradoEstable = false;
bool portonEstuvoCerradoEstable = false;

// ===================== BOTONES ============================
bool botonPresionado = false;
unsigned long tInicioPresion = 0;

// ===================== TIMERS =============================
unsigned long tUltimoPulsoEnviado      = 0;
unsigned long tUltimoComandoAutorizado = 0;
unsigned long tCambioEstadoPorton      = 0;
unsigned long tInicioMovimiento        = 0;
unsigned long tVisualObstaculo         = 0;
unsigned long tFCAbiertoDesde          = 0;

// ===================== SIRENA =============================
unsigned long tSirena     = 0;
unsigned long tSilenciado = 0;

// ===================== BUZZER =============================
bool buzzerActivo          = false;
uint8_t buzzerRepeticiones = 0;
uint8_t buzzerContador     = 0;
unsigned long tBuzzer      = 0;
bool estadoBuzzer          = false;

// ===================== LED CONFIG =========================
LedConfigModo ledConfigModo = LEDCFG_IDLE;
bool progPresionado = false;
uint8_t nivelProg   = 0;
unsigned long tProgInicio  = 0;
unsigned long tLedConfig   = 0;
unsigned long tLearnInicio = 0;

// ===================== USUARIO ============================
char ultimoUsuario[20] = "Sistema";

// =================================================================================
// 4. ACTUALIZAR ESTADO DEL PORTÓN
// =================================================================================
void actualizarEstadoPorton() {

  bool fcCerrado = entradaActiva(PIN_FC_CERRADO);
  bool fcAbierto = entradaActiva(PIN_FC_ABIERTO);
  unsigned long ahora = millis();

  static unsigned long tInicioErrorSensores = 0;
  static bool errorDetectadoPrevio = false;

  EstadoPorton nuevoEstado = estadoPortonActual;

  if (fcCerrado && fcAbierto) {
    if (!errorDetectadoPrevio) {
      tInicioErrorSensores = ahora;
      errorDetectadoPrevio = true;
    } else if (ahora - tInicioErrorSensores > 200) {
      nuevoEstado = ESTADO_ERROR_SENSORES;
    }
  } else {
    errorDetectadoPrevio = false;
    tInicioErrorSensores = 0;

    if (fcCerrado) {
      nuevoEstado = ESTADO_CERRADO;
      tInicioMovimiento = 0;
    } else if (fcAbierto) {
      nuevoEstado = ESTADO_ABIERTO;
      tInicioMovimiento = 0;
    } else {
      if (estadoPortonActual == ESTADO_CERRADO &&
          (millis() - tUltimoComandoAutorizado < 500)) {
        nuevoEstado = ESTADO_ABRIENDO;
        tInicioMovimiento = ahora;
        portonEstuvoCerradoEstable = false;
      } else if (estadoPortonActual == ESTADO_ABIERTO) {
        nuevoEstado = ESTADO_CERRANDO;
        tInicioMovimiento = ahora;
        portonEstuvoCerradoEstable = false;
      } else if (estadoPortonActual == ESTADO_ERROR_SENSORES) {
        nuevoEstado = ESTADO_DESCONOCIDO;
        tInicioMovimiento = ahora;
      }

      if (tInicioMovimiento > 0 &&
          (ahora - tInicioMovimiento > MAX_TIEMPO_MOVIMIENTO)) {
        if (nuevoEstado != ESTADO_FALLA_MECANICA) {
          nuevoEstado = ESTADO_FALLA_MECANICA;
          registrarEvento("FALLA: Tiempo excedido", "Sistema");
        }
      }
    }
  }

  if (nuevoEstado != estadoPortonActual) {
    estadoPortonPrevio = estadoPortonActual;
    estadoPortonActual = nuevoEstado;
    tCambioEstadoPorton = ahora;
  }

  if (estadoPortonActual == ESTADO_CERRADO &&
      (ahora - tCambioEstadoPorton) >= 5000) {
    portonCerradoEstable = true;
    portonEstuvoCerradoEstable = true;
  } else {
    portonCerradoEstable = false;
  }

  switch (estadoPortonActual) {
    case ESTADO_CERRADO:        estadoPortonUI = 1; break;
    case ESTADO_ABIERTO:        estadoPortonUI = 2; break;
    case ESTADO_ABRIENDO:       estadoPortonUI = 3; break;
    case ESTADO_ERROR_SENSORES: estadoPortonUI = 4; break;
    case ESTADO_FALLA_MECANICA: estadoPortonUI = 5; break;
    case ESTADO_CERRANDO:       estadoPortonUI = 6; break;
    default:                    estadoPortonUI = 0; break;
  }
}

// =================================================================================
// 5. GESTIÓN DE PULSO
// =================================================================================
void gestionarPulso() {

  static bool pulsoActivo = false;
  static unsigned long tInicioPulso = 0;

  unsigned long ahora = millis();
  bool barreraCortada = (digitalRead(PIN_BARRERA) == HIGH);

  if (pulsoActivo) {
    if (ahora - tInicioPulso >= DURACION_PULSO_MS) {
      digitalWrite(PIN_RELE_PULSO, LOW);
      pulsoActivo = false;
      tUltimoPulsoEnviado = ahora;
    }
    return;
  }

  if (solicitudPulso && strcmp(ultimoUsuario, "Sistema") == 0) {
    strcpy(ultimoUsuario, "Web Admin");
    registrarEvento("Comando remoto");
  }

  if (!solicitudPulso) return;

  if (estadoPortonActual == ESTADO_FALLA_MECANICA ||
      estadoPortonActual == ESTADO_ERROR_SENSORES) {
    beepPendiente = true;
    const char* msg =
      (estadoPortonActual == ESTADO_ERROR_SENSORES) ?
      "Mover con ERROR SENSORES" :
      "Mover con FALLA MECÁNICA";
    registrarEvento(msg);
  }

  if (barreraCortada && estadoPortonActual == ESTADO_ABIERTO) {
    solicitudPulso = false;
    return;
  }

  if (ahora - tUltimoPulsoEnviado < SEPARACION_PULSOS_MS) {
    solicitudPulso = false;
    return;
  }

  digitalWrite(PIN_RELE_PULSO, HIGH);
  pulsoActivo = true;
  tInicioPulso = ahora;
  tUltimoComandoAutorizado = ahora;
  solicitudPulso = false;
}

// =================================================================================
// 6. SETUP
// =================================================================================
void setup() {

  Serial.begin(115200);
  delay(200);

  // -----------------------
  // Pines (v18)
  // -----------------------
  pinMode(PIN_RELE_PULSO, OUTPUT);
  digitalWrite(PIN_RELE_PULSO, LOW);

  pinMode(PIN_BARRERA, INPUT_PULLUP);
  pinMode(PIN_FC_CERRADO, INPUT_PULLUP);
  pinMode(PIN_FC_ABIERTO, INPUT_PULLUP);

  // -----------------------
  // Estados iniciales
  // -----------------------
  estadoPortonActual  = ESTADO_DESCONOCIDO;
  estadoPortonPrevio  = ESTADO_DESCONOCIDO;
  estadoSeguridad     = SEG_NORMAL;
  estadoSirena        = SIR_APAGADA;

  solicitudPulso      = false;
  emergenciaActiva    = false;
  modoMantenimiento   = false;
  sistemaInicializado = true;

  strcpy(ultimoUsuario, "Sistema");

  // -----------------------
  // Servicios
  // -----------------------
  WiFiManager_begin();
  iniciarWeb();

  Serial.println("Sistema iniciado");
}


// =================================================================================
// 7. LOOP PRINCIPAL – ORQUESTADOR
// =================================================================================
void loop() {

  // 1. Entradas
  procesarEntradasUsuario();

  // 2. Barrera
  procesarBarrera();

  // 3. Estado del portón
  actualizarEstadoPorton();

  // 4. Seguridad
  procesarSeguridad();

  // 5. Actuadores
  gestionarPulso();
  gestionarSirena();
  gestionarSemaforo();

  // 6. Indicadores
  gestionarLedsPlaca();
  gestionarLedConfig();
  procesarBuzzer();

  // 7. Servicios
  WiFiManager_loop();
  loopWeb();
}

// =================================================================================
// 8. STUBS TEMPORALES (v19 – se completan luego)
// =================================================================================

void registrarEvento(String msg, String userForzado) {
  // TODO: implementar en módulo Memoria
}

void procesarEntradasUsuario() {

  if (emergenciaActiva) return;

  bool btnManual = entradaActiva(PIN_BTN_MANUAL);  // Pulsador físico
  bool btnRF     = entradaActiva(PIN_RF_RX);       // Control remoto
  bool btnProg   = entradaActiva(PIN_BTN_PROG);    // Botón configuración

  unsigned long ahora = millis();

  // ======================================================
  // BOTÓN MANUAL + RF
  // ======================================================
  if (btnManual || btnRF) {

    if (!botonPresionado) {
      botonPresionado = true;
      tInicioPresion = ahora;
      panicoDisparadoEnEstaPulsacion = false;

      if (btnManual) strcpy(ultimoUsuario, "Boton Fisico");
      else           strcpy(ultimoUsuario, "Control RF");
    }
    else {
      // Solo botón manual puede disparar pánico por tiempo
      if (btnManual && !panicoEnclavado &&
          (ahora - tInicioPresion >= TIEMPO_PANICO_MS)) {

        panicoEnclavado = true;
        panicoDisparadoEnEstaPulsacion = true;
        estadoSeguridad = SEG_DISPARADA;
        estadoSirena = SIR_SONANDO;
        tSirena = ahora;

        registrarEvento("Alarma por PÁNICO");
      }
    }

    return;
  }

  // ------------------------------------------------------
  // SOLTAR BOTÓN MANUAL / RF
  // ------------------------------------------------------
  if (botonPresionado) {

    unsigned long dur = ahora - tInicioPresion;
    botonPresionado = false;

    if (dur < TIEMPO_REBOTE_MS) return;

    // Si disparó pánico, no genera pulso
    if (panicoDisparadoEnEstaPulsacion) {
      strcpy(ultimoUsuario, "Sistema");
      return;
    }

    // Si estaba enclavado en pánico → apagar
    if (panicoEnclavado) {
      panicoEnclavado = false;
      estadoSeguridad = SEG_NORMAL;
      estadoSirena = SIR_APAGADA;
      digitalWrite(PIN_SIRENA, LOW);
      strcpy(ultimoUsuario, "Sistema");
      return;
    }

    // Caso normal → pulso
    solicitudPulso = true;
    return;
  }

  // ======================================================
  // BOTÓN PROG (CONFIGURACIÓN)
  // ======================================================
  if (btnProg) {

    if (!progPresionado) {
      progPresionado = true;
      tProgInicio = ahora;
      nivelProg = 0;
    }

    unsigned long dur = ahora - tProgInicio;

    if (dur >= 1000  && nivelProg == 0) { nivelProg = 1; ledConfigModo = LEDCFG_CONFIRM_1S;  beep(1); }
    if (dur >= 5000  && nivelProg == 1) { nivelProg = 2; ledConfigModo = LEDCFG_CONFIRM_5S;  beep(2); }
    if (dur >= 10000 && nivelProg == 2) { nivelProg = 3; ledConfigModo = LEDCFG_CONFIRM_10S; beep(3); }
    if (dur >= 15000 && nivelProg == 3) { nivelProg = 4; ledConfigModo = LEDCFG_CONFIRM_15S; beep(4); }

    return;
  }

  // ------------------------------------------------------
  // SOLTAR BOTÓN PROG
  // ------------------------------------------------------
  if (progPresionado) {
    progPresionado = false;

    switch (nivelProg) {

      case 1: // LEARN
        ledConfigModo = LEDCFG_LEARN;
        tLearnInicio = ahora;
        registrarEvento("Modo LEARN iniciado", "Sistema");
        break;

      case 2: // RESET WIFI
        ledConfigModo = LEDCFG_EXIT_FLASH;
        tLedConfig = ahora;
        registrarEvento("Reset WiFi ejecutado", "Sistema");
        WiFiManager_resetCredentials();
        break;

      case 3: // RESET DB
        ledConfigModo = LEDCFG_EXIT_FLASH;
        tLedConfig = ahora;
        registrarEvento("Reset DB ejecutado", "Sistema");
        break;

      case 4: // FACTORY RESET
        ledConfigModo = LEDCFG_EXIT_FLASH;
        tLedConfig = ahora;
        registrarEvento("Factory Reset ejecutado", "Sistema");
        break;
    }

    nivelProg = 0;
 
  }
}
 
 void beep(uint8_t veces) {
  // TODO: implementar buzzer (v18)
}

void procesarBarrera() {

  bool barreraCortada = (digitalRead(PIN_BARRERA) == HIGH);  // NC → HIGH = cortada
  static bool estadoPrevioBarrera = false;

  // Guardar momento de obstáculo para visualización en UI
  if (barreraCortada) {
    tVisualObstaculo = millis();
  }

  // Solo actúa cuando el portón está cerrando
  if (estadoPortonActual != ESTADO_CERRANDO) {
    estadoPrevioBarrera = barreraCortada;
    return;
  }

  // Flanco de activación de barrera durante cierre
  if (barreraCortada && !estadoPrevioBarrera) {
    solicitudPulso = true;          // Orden de reapertura
    tUltimoPulsoEnviado = 0;        // Fuerza aceptación inmediata
    strcpy(ultimoUsuario, "Sensores");
    registrarEvento("Barrera activada", "Sensores");
  }

  estadoPrevioBarrera = barreraCortada;
}

void procesarSeguridad() {

  if (!sistemaInicializado) return;
  if (estadoPortonActual == ESTADO_DESCONOCIDO) return;
  if (modoMantenimiento || panicoEnclavado) return;

  // --------------------------------------------------
  // Emergencia activa: estado válido, nunca es sabotaje
  // --------------------------------------------------
  if (emergenciaActiva) {
    estadoSeguridadUI = 0;
    estadoSirena = SIR_APAGADA;
    digitalWrite(PIN_SIRENA, LOW);
    return;
  }

  unsigned long ahora = millis();
  bool fcCerrado = entradaActiva(PIN_FC_CERRADO);

  // --------------------------------------------------
  // Estado para UI
  // --------------------------------------------------
  if (estadoSeguridad == SEG_DISPARADA)           estadoSeguridadUI = 1;
  else if (estadoSeguridad == SEG_LATENTE)        estadoSeguridadUI = 2;
  else if (estadoPortonActual == ESTADO_ERROR_SENSORES) estadoSeguridadUI = 3;
  else if (ahora - tVisualObstaculo < 5000)       estadoSeguridadUI = 4;
  else                                            estadoSeguridadUI = 0;

  // --------------------------------------------------
  // Variables internas del módulo
  // --------------------------------------------------
  static unsigned long tFCAbiertoDesde = 0;
  static unsigned long tInicioLatente  = 0;

  // --------------------------------------------------
// Sabotaje: FC PC abierto en portón cerrado estable
// --------------------------------------------------
if (portonEstuvoCerradoEstable && estadoSeguridad == SEG_NORMAL) {

  if (!fcCerrado) {

    if (tFCAbiertoDesde == 0)
      tFCAbiertoDesde = ahora;

    if (ahora - tFCAbiertoDesde > 4000) {
      estadoSeguridad = SEG_DISPARADA;
      tInicioLatente = 0;
      registrarEvento("Alarma: Sabotaje FC PC", "Sistema");
    }

  } else {
    tFCAbiertoDesde = 0;
  }

} else {
  tFCAbiertoDesde = 0;
}

  // --------------------------------------------------
  // Gestión del estado LATENTE
  // --------------------------------------------------
  if (estadoSeguridad == SEG_LATENTE) {

    if (tInicioLatente == 0)
      tInicioLatente = ahora;

    if (ahora - tInicioLatente > SIRENA_OFF_TIEMPO) {

      if (fcCerrado) {
        estadoSeguridad = SEG_NORMAL;
        registrarEvento("Alarma normalizada", "Sistema");
      } else {
        estadoSeguridad = SEG_DISPARADA;
        estadoSirena = SIR_SONANDO;
        tSirena = ahora;
        registrarEvento("Alarma re-disparada por falla persistente", "Sistema");
      }

      tInicioLatente = 0;
    }

  } else {
    tInicioLatente = 0;
  }
}

void gestionarSirena() {

  unsigned long ahora = millis();

  // --------------------------------------------------
  // Beep corto por error puntual
  // --------------------------------------------------
  if (beepPendiente) {
    estadoSirena = SIR_BEEP_ERROR;
    tSirena = ahora;
    digitalWrite(PIN_SIRENA, HIGH);
    beepPendiente = false;
    return;
  }

  if (estadoSirena == SIR_BEEP_ERROR) {
    if (ahora - tSirena >= DURACION_BEEP_ERROR) {
      digitalWrite(PIN_SIRENA, LOW);
      estadoSirena = SIR_APAGADA;

      // Si sigue en alarma, vuelve a sonar normal
      if (estadoSeguridad == SEG_DISPARADA) {
        estadoSirena = SIR_SONANDO;
      }
    }
    return;
  }

  // --------------------------------------------------
  // Forzados de sistema
  // --------------------------------------------------
  if (modoMantenimiento) {
    digitalWrite(PIN_SIRENA, LOW);
    estadoSirena = SIR_APAGADA;
    return;
  }

  if (panicoEnclavado) {
    digitalWrite(PIN_SIRENA, HIGH);
    return;
  }

  // --------------------------------------------------
  // Sistema normal
  // --------------------------------------------------
  if (estadoSeguridad == SEG_NORMAL) {
    digitalWrite(PIN_SIRENA, LOW);
    estadoSirena = SIR_APAGADA;
    return;
  }

  // --------------------------------------------------
  // Gestión de alarma sonora
  // --------------------------------------------------
  if (estadoSeguridad == SEG_DISPARADA) {

    if (estadoSirena == SIR_APAGADA) {
      estadoSirena = SIR_SONANDO;
      tSirena = ahora;
    }

    if (estadoSirena == SIR_SONANDO) {
      digitalWrite(PIN_SIRENA, HIGH);

      if (ahora - tSirena >= SIRENA_ON_TIEMPO) {
        estadoSirena = SIR_PAUSA;
        tSirena = ahora;
      }
    }
    else if (estadoSirena == SIR_PAUSA) {
      digitalWrite(PIN_SIRENA, LOW);

      if (ahora - tSirena >= SIRENA_OFF_TIEMPO) {
        estadoSirena = SIR_SONANDO;
        tSirena = ahora;
      }
    }
  }
}

void gestionarSemaforo() {

  // --------------------------------------------------
  // Seguridad ante estados inválidos
  // --------------------------------------------------
  if (!sistemaInicializado ||
      estadoPortonActual == ESTADO_ERROR_SENSORES ||
      estadoPortonActual == ESTADO_FALLA_MECANICA) {

    // ROJO
    digitalWrite(PIN_OUT1, HIGH);
    digitalWrite(PIN_OUT2, LOW);
    digitalWrite(PIN_OUT3, LOW);
    return;
  }

  // --------------------------------------------------
  // Portón abierto → ROJO
  // --------------------------------------------------
  if (estadoPortonActual == ESTADO_ABIERTO) {
    digitalWrite(PIN_OUT1, HIGH);
    digitalWrite(PIN_OUT2, LOW);
    digitalWrite(PIN_OUT3, LOW);
    return;
  }

  // --------------------------------------------------
  // Portón abriéndose → AMARILLO
  // --------------------------------------------------
  if (estadoPortonActual == ESTADO_ABRIENDO) {
    digitalWrite(PIN_OUT1, LOW);
    digitalWrite(PIN_OUT2, HIGH);
    digitalWrite(PIN_OUT3, LOW);
    return;
  }

  // --------------------------------------------------
  // Portón cerrado o cerrándose → VERDE
  // --------------------------------------------------
  if (estadoPortonActual == ESTADO_CERRADO ||
      estadoPortonActual == ESTADO_CERRANDO) {

    digitalWrite(PIN_OUT1, LOW);
    digitalWrite(PIN_OUT2, LOW);
    digitalWrite(PIN_OUT3, HIGH);
    return;
  }

  // --------------------------------------------------
  // Estado inesperado → por seguridad ROJO
  // --------------------------------------------------
  digitalWrite(PIN_OUT1, HIGH);
  digitalWrite(PIN_OUT2, LOW);
  digitalWrite(PIN_OUT3, LOW);
}

void gestionarLedsPlaca() {

  // ---------------------------
  // LED VERDE: heartbeat simple
  // ---------------------------
  static unsigned long tVerde = 0;
  static bool estadoVerde = false;

  if (millis() - tVerde > 1000) {   // 1 Hz heartbeat
    tVerde = millis();
    estadoVerde = !estadoVerde;
    digitalWrite(PIN_LED_VERDE, estadoVerde);
  }

  // El LED de configuración (GPIO22) se gestiona exclusivamente
  // en la función gestionarLedConfig()
}


void beep(uint8_t cantidad) {
  buzzerActivo = true;
  buzzerRepeticiones = cantidad;
  buzzerContador = 0;
  estadoBuzzer = false;
  tBuzzer = millis();
}

void procesarBuzzer() {

  if (!buzzerActivo) return;

  unsigned long ahora = millis();
  if (ahora - tBuzzer < 120) return;

  tBuzzer = ahora;
  estadoBuzzer = !estadoBuzzer;
  digitalWrite(PIN_BUZZER, estadoBuzzer);

  if (!estadoBuzzer) {
    buzzerContador++;

    if (buzzerContador >= buzzerRepeticiones) {
      buzzerActivo = false;
      buzzerContador = 0;
      digitalWrite(PIN_BUZZER, LOW);
    }
  }
}
