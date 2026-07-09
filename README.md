# Control de Grúa Modelo — Física de Péndulo

Proyecto de clase de Física: control remoto de una grúa modelo (ESP8266 + 2
servos de rotación continua) con una interfaz web que simula, en tiempo
real, el comportamiento de péndulo de la carga suspendida.

## Servos de rotación continua — por qué el control es distinto

Los dos servos son de **rotación continua (360°)**, no de posición. Un
servo de posición entiende "andá al ángulo 45°"; uno continuo solo entiende
"girá a tal velocidad, en tal sentido" (como un motor). Por eso:

- El control es de **velocidad/dirección** (botones de mantener presionado),
  no de sliders de ángulo.
- No hay sensor de posición. El ángulo del boom y el largo de cable que ves
  en pantalla son **estimados**, integrando en el tiempo la velocidad que
  le pediste al servo — es un gemelo digital de lazo abierto, no una
  medición real. Con el uso se puede desviar de la posición real; para
  eso está el botón "Reiniciar posición" (recalibra contra una posición
  física conocida, por ejemplo boom apuntando al frente y carga arriba).
- Hay dos constantes de **calibración** en el panel ("Velocidad del boom a
  100%" y "Velocidad del cable a 100%") que tenés que medir en tu grúa real
  con un cronómetro: hacé girar el boom a velocidad máxima, medí cuántos
  grados por segundo hace (o cuánto tarda una vuelta completa y convertí),
  y lo mismo con el cable subiendo/bajando en metros por segundo.

## Conexión: MQTT en vez de WiFi propia

La ESP8266 ya no crea su propio punto de acceso. Se conecta a tu WiFi
normal y habla con un **broker MQTT** (ej. [HiveMQ Cloud](https://www.hivemq.com/mqtt-cloud-broker/),
plan gratuito) por TLS. La página web se conecta al mismo broker por
WebSocket seguro (`wss://`) usando [MQTT.js](https://github.com/mqttjs/MQTT.js).
Ninguno de los dos necesita saber la IP del otro — ambos hablan a través
del broker.

Ventajas sobre el modo AP anterior:
- No hay que cambiar de red WiFi en el celular/laptop.
- Como es `wss://` (seguro), la página pública de **GitHub Pages** también
  puede controlar el hardware real — ya no hay bloqueo de "mixed content".

**Configurar la ESP8266** — editar al principio de `src/main.cpp`:
```cpp
const char* WIFI_SSID = "TU_WIFI";
const char* WIFI_PASS = "TU_PASSWORD";
const char* MQTT_HOST = "xxxxxxxx.s2.eu.hivemq.cloud";
const char* MQTT_USER = "tu_usuario_mqtt";
const char* MQTT_PASS = "tu_password_mqtt";
const char* TOPIC_BASE = "crane/benja";
```

**Configurar la página web** — en el panel "Conexión con la grúa (MQTT)",
completar Host, Puerto (8884 para WebSocket seguro — distinto del 8883 que
usa la ESP8266 para MQTT nativo), Usuario, Contraseña y el mismo prefijo de
topics. Se guardan en este navegador (`localStorage`) para no tener que
escribirlos cada vez.

### Confiabilidad — por qué hay un botón STOP y un watchdog

A diferencia de HTTP (donde una petición que falla se nota al instante),
MQTT no garantiza que cada mensaje llegue de forma visible para el
operador. Con un servo de rotación continua, un comando de "parar" perdido
significa que el motor sigue girando sin que se note en la interfaz. Por
eso:
- Mientras mantenés presionado un botón de movimiento, el comando se
  reenvía cada 300 ms (no una sola vez).
- La ESP8266 tiene un **watchdog**: si no le llega ningún mensaje de
  movimiento en 800 ms mientras algo se está moviendo, frena sola.
- Se usa el **Last Will** de MQTT: si la ESP8266 se desconecta de forma
  anormal, el broker publica automáticamente "offline" en
  `<prefijo>/status`, y la página lo muestra como desconexión confirmada,
  no como una suposición.

## Estructura del repositorio

```
platformio.ini         -> dos entornos: firmware real y test de servos
src/main.cpp             -> firmware de la ESP8266 (control por velocidad)
src/servo_test.cpp        -> test aislado de los servos, sin WiFi
docs/index.html            -> interfaz web (GitHub Pages + uso local)
```

## Cómo correr el proyecto

1. **Probar los servos primero** (recomendado antes de tocar el firmware
   completo — descarta problemas de cableado/alimentación):
   ```
   pio run -e servotest -t upload
   pio device monitor -b 115200
   ```
2. Crear un cluster gratis en [HiveMQ Cloud](https://console.hivemq.cloud/)
   (o cualquier broker MQTT que soporte WebSocket + TLS), y anotar host,
   usuario y contraseña.
3. Completar esos datos en `src/main.cpp` (WiFi + broker) y en el panel de
   la página web.
4. **Firmware real:**
   ```
   pio run -e nodemcuv2 -t upload
   pio device monitor -b 115200   # confirma que se conectó a WiFi y al broker
   ```
5. Abrir `docs/index.html` (local o vía GitHub Pages), completar los datos
   del broker y tocar "Conectar".

Si tu placa no es una NodeMCU v2, cambiá `board` en `platformio.ini` (por
ejemplo `d1_mini`) en los dos entornos.

## Dimensiones medidas de la grúa

| Medida | Valor | Uso en el modelo |
|---|---|---|
| Altura del mástil | ~27 cm | referencia (altura máxima de la carga) |
| Pivote → eje de la carga (radio del boom, R) | ~12 cm | aceleración tangencial `a_t = R·α` |
| Largo del brazo superior | ~28 cm | dimensión documentada, no usada directamente (incluye contrapeso) |

## Modelo físico

La carga se modela como un péndulo cuyo punto de anclaje (la punta del
boom) acelera tangencialmente cuando el boom gira:

```
θ'' = -(g/L)·sin(θ) - (a_t/L)·cos(θ) - c·θ'
```

donde `a_t = R·α` (α = aceleración angular del boom, estimada de la misma
rampa de velocidad que se le pide al servo).

### Mecanismo de seguridad

No es un simple interruptor: calcula cuánta aceleración angular puede
tolerar el sistema según el torque disponible del motor y la inercia total
(boom + carga):

```
I_total = I_boom + m·R²
α_max = τ_motor / I_total
```

Con masa fija, más torque permite acelerar más rápido; con torque fijo, más
masa obliga a acelerar más despacio — es la razón física real detrás de por
qué las grúas reales derratean la velocidad según el peso de la carga. El
factor de conservadurismo agrega margen extra sobre ese límite físico. Con
el mecanismo apagado se usa un límite fijo que ignora la masa —
intencionalmente, para poder comparar y mostrar por qué hace falta el
mecanismo.

La energía mecánica mostrada (`PE + KE`) usa la masa configurada — es útil
para comparar relativamente entre modo normal y modo seguro, no es una
medición de laboratorio en Joules.
