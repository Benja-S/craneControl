/*
  ===========================================================================
  CONTROL DE GRÚA MODELO — ESP8266 vía MQTT (PlatformIO)
  ---------------------------------------------------------------------------
  Proyecto de Física — Demostración de dinámica de péndulo en grúas

  A diferencia de la versión anterior (ESP8266 como punto de acceso WiFi +
  HTTP), acá la ESP8266 se conecta a TU red WiFi normal y habla con un
  broker MQTT (ej. HiveMQ Cloud, plan gratuito) por TLS. El navegador
  también se conecta a ese mismo broker (por WebSocket seguro, wss://) y
  ambos se coordinan a través de él. Ventaja: no hay que cambiar de red
  WiFi en el celular, y hasta la página pública de GitHub Pages puede
  controlar el hardware real (wss:// no tiene el problema de "mixed
  content" que sí tenía http://).

  Servos de ROTACIÓN CONTINUA (360°): igual que antes, se controla
  velocidad/dirección, no posición (ver comentarios en el firmware HTTP
  anterior si los tenés, o el README).

  IMPORTANTE — seguridad ante pérdida de conexión:
  Con HTTP, si un comando se perdía, el operador lo notaba al toque
  (nada respondía). Con MQTT eso no está garantizado. Por eso:
    - Si no llega NINGÚN mensaje de movimiento durante WATCHDOG_MS
      mientras el eje se está moviendo, el firmware lo frena solo.
    - Se usa un "Last Will" de MQTT: si la ESP8266 pierde la conexión
      de forma anormal, el broker publica automáticamente "offline" en
      <base>/status, y el navegador lo muestra como desconectado real
      (no una suposición).

  Librerías (PlatformIO las instala solas via lib_deps):
    - PubSubClient (knolleary)
    - WiFiClientSecure (incluida en el core de ESP8266)

  CONFIGURÁ ACÁ ABAJO: WiFi, broker MQTT, y el prefijo de topics.
  ===========================================================================
*/

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Servo.h>

// ---------------------------------------------------------------------------
// CONFIGURACIÓN — completar con tus datos
// ---------------------------------------------------------------------------
const char* WIFI_SSID = "WiFi motoedge";
const char* WIFI_PASS = "12345678";

const char* MQTT_HOST = "706d9facdf63418b9eb215dcb67a829d.s1.eu.hivemq.cloud"; // tu cluster de HiveMQ Cloud
const int   MQTT_PORT = 8883; // TLS nativo (no confundir con 8884, que es WSS para navegador)
const char* MQTT_USER = "cranecontrol";
const char* MQTT_PASS = "crane1234";

const char* TOPIC_BASE = "crane/benja"; // debe coincidir con el que uses en index.html

// ---------------------------------------------------------------------------
String T_CMD_ROT, T_CMD_HOIST, T_CMD_STOP, T_CMD_SAFETY, T_STATUS, T_STATE;

WiFiClientSecure wifiClient;
PubSubClient mqtt(wifiClient);

Servo servoRot;
Servo servoHoist;
const int PIN_ROT   = 5; // D1
const int PIN_HOIST = 4; // D2

const int NEUTRAL_US = 1500;
const int RANGE_US   = 500;

struct AxisState {
  float velocity;
  float targetVelocity;
  float maxVel;
  float maxAccel;
};
AxisState rot   = {0, 0, 100, 300};
AxisState hoist = {0, 0, 100, 300};

bool safetyMode = false;
unsigned long lastUpdateMs = 0;
unsigned long lastCmdMs = 0;
const unsigned long WATCHDOG_MS = 800; // si no hay comandos en este tiempo mientras se mueve, frena solo

void stepAxis(AxisState &axis, float dt) {
  float target = constrain(axis.targetVelocity, -axis.maxVel, axis.maxVel);
  float dv = target - axis.velocity;
  float maxDv = axis.maxAccel * dt;
  dv = constrain(dv, -maxDv, maxDv);
  axis.velocity += dv;
  if (fabs(axis.velocity) < 0.5 && fabs(target) < 0.01) axis.velocity = 0;
}

void writeAxis(Servo &servo, AxisState &axis) {
  int us = NEUTRAL_US + (int)((axis.velocity / 100.0f) * RANGE_US);
  us = constrain(us, NEUTRAL_US - RANGE_US, NEUTRAL_US + RANGE_US);
  servo.writeMicroseconds(us);
}

// ---------------------------------------------------------------------------
// MQTT callback — llega un mensaje en algún topic suscripto
// ---------------------------------------------------------------------------
void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  String t = String(topic);

  lastCmdMs = millis();

  if (t == T_CMD_ROT) {
    rot.targetVelocity = constrain(msg.toFloat(), -1, 1) * rot.maxVel;
  } else if (t == T_CMD_HOIST) {
    hoist.targetVelocity = constrain(msg.toFloat(), -1, 1) * hoist.maxVel;
  } else if (t == T_CMD_STOP) {
    rot.targetVelocity = 0;
    hoist.targetVelocity = 0;
  } else if (t == T_CMD_SAFETY) {
    // formato: "on,maxVel,maxAccel"  ej: "1,50.0,30.0"
    int c1 = msg.indexOf(',');
    int c2 = msg.indexOf(',', c1 + 1);
    if (c1 > 0 && c2 > c1) {
      safetyMode = msg.substring(0, c1).toInt() == 1;
      float mv = constrain(msg.substring(c1 + 1, c2).toFloat(), 0, 100);
      float ma = max(1.0f, msg.substring(c2 + 1).toFloat());
      rot.maxVel = mv; hoist.maxVel = mv;
      rot.maxAccel = ma; hoist.maxAccel = ma;
    }
  }
}

void mqttReconnect() {
  while (!mqtt.connected()) {
    Serial.print("Conectando a MQTT...");
    String clientId = "grua-" + String(ESP.getChipId());
    // Last Will: si se cae la conexión de forma anormal, el broker publica esto solo
    if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                      T_STATUS.c_str(), 1, true, "offline")) {
      Serial.println(" conectado.");
      mqtt.publish(T_STATUS.c_str(), "online", true);
      mqtt.subscribe(T_CMD_ROT.c_str());
      mqtt.subscribe(T_CMD_HOIST.c_str());
      mqtt.subscribe(T_CMD_STOP.c_str());
      mqtt.subscribe(T_CMD_SAFETY.c_str());
      lastCmdMs = millis();
    } else {
      Serial.print(" fallo, rc="); Serial.print(mqtt.state()); Serial.println(" reintentando en 2s");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  T_CMD_ROT    = String(TOPIC_BASE) + "/cmd/rot";
  T_CMD_HOIST  = String(TOPIC_BASE) + "/cmd/hoist";
  T_CMD_STOP   = String(TOPIC_BASE) + "/cmd/stop";
  T_CMD_SAFETY = String(TOPIC_BASE) + "/cmd/safety";
  T_STATUS     = String(TOPIC_BASE) + "/status";
  T_STATE      = String(TOPIC_BASE) + "/state";

  servoRot.attach(PIN_ROT);
  servoHoist.attach(PIN_HOIST);
  servoRot.writeMicroseconds(NEUTRAL_US);
  servoHoist.writeMicroseconds(NEUTRAL_US);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println();
  Serial.print("WiFi OK. IP local: ");
  Serial.println(WiFi.localIP());

  // Simplificación para proyecto de clase: no valida el certificado del
  // broker. Para producción real conviene setTrustAnchors() con el cert
  // de HiveMQ Cloud en vez de setInsecure().
  wifiClient.setInsecure();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);

  lastUpdateMs = millis();
  lastCmdMs = millis();
}

void loop() {
  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();

  unsigned long now = millis();

  // watchdog: si algo se está moviendo pero hace rato no llega ningún
  // comando, frenamos por las dudas (conexión perdida / mensaje perdido)
  bool moving = fabs(rot.targetVelocity) > 0.01 || fabs(hoist.targetVelocity) > 0.01;
  if (moving && (now - lastCmdMs > WATCHDOG_MS)) {
    rot.targetVelocity = 0;
    hoist.targetVelocity = 0;
  }

  float dt = (now - lastUpdateMs) / 1000.0f;
  if (dt >= 0.02f) {
    stepAxis(rot, dt);
    stepAxis(hoist, dt);
    writeAxis(servoRot, rot);
    writeAxis(servoHoist, hoist);
    lastUpdateMs = now;
  }

  // telemetría liviana, 1 vez por segundo
  static unsigned long lastTelemetry = 0;
  if (now - lastTelemetry > 1000) {
    lastTelemetry = now;
    String state = "{\"rotVel\":" + String(rot.velocity, 1) +
                    ",\"hoistVel\":" + String(hoist.velocity, 1) +
                    ",\"safety\":" + String(safetyMode ? "true" : "false") + "}";
    mqtt.publish(T_STATE.c_str(), state.c_str());
  }
}
