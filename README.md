# Portones
Firmware ESP32 para portones automáticos, con seguridad, WebUI y arquitectura por estados, orientado a producto profesional.
# Portones

Firmware profesional para control de portones automáticos basado en **ESP32**, diseñado con criterios industriales, foco en seguridad y mantenibilidad, y orientado a producto vendible.

## 🎯 Objetivo del proyecto

Desarrollar un firmware robusto y escalable para automatización de portones, con:
- Estados bien definidos
- Seguridad prioritaria
- Interfaz web integrada
- Código claro, auditable y mantenible en el tiempo

El proyecto **no es un sketch experimental**, sino una base sólida para uso real y comercial.

---

## ⚙️ Características principales

- Control por **máquina de estados**
- Seguridad integrada:
  - Barrera óptica
  - Detección de sabotaje
  - Pánico enclavado
- **WebUI** para monitoreo y control
- **WiFi Manager** (AP / STA)
- Sirena, buzzer, semáforo y LEDs de estado
- Registro de eventos
- Arquitectura por **bloques numerados** (estilo industrial)
- Pensado para ESP32 + framework Arduino (PlatformIO)

---

## 🧱 Arquitectura del firmware

El código está organizado en bloques claramente identificados:

- Configuración e includes
- Prototipos
- Helpers
- Variables y estados
- Lógica de estado del portón
- Gestión de pulso
- Entradas de usuario
- Barrera de seguridad
- Lógica de seguridad / alarmas
- Actuadores (sirena, semáforo, buzzer)
- Indicadores (LEDs)
- Servicios (WiFi, WebUI)
- Loop orquestador por prioridad

Cada bloque cumple una única responsabilidad.

---

## 🔐 Filosofía de desarrollo

Reglas del proyecto:

- ❌ No inventar lógica
- ❌ No borrar código funcional
- ❌ No refactors “porque sí”
- ✅ Cambios de a uno
- ✅ Todo cambio debe ser entendible por alguien que **no programa**
- ✅ Código explícito > código “inteligente”
- ✅ Seguridad primero

---

## 🛠️ Entorno de desarrollo

- ESP32
- PlatformIO
- Framework Arduino
- C++ (estilo firmware, no académico)

---

## 📌 Estado actual

- Migrado a PlatformIO
- Lógica principal operativa
- En proceso de ordenamiento, validación y blindaje para uso productivo

---

## 📄 Licencia

Sin licencia definida por el momento.
