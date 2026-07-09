/*
  ===========================================================================
  TEST DE SERVOS — sin WiFi, sin servidor web
  ---------------------------------------------------------------------------
  Objetivo: confirmar que el cableado, la alimentación y los dos servos
  funcionan ANTES de meter la capa de WiFi/HTTP en la ecuación. Si esto
  no mueve los servos correctamente, el problema es de hardware, no del
  firmware principal.

  Cómo correr SOLO este archivo (sin tocar main.cpp):
      pio run -e servotest -t upload
      pio device monitor -b 115200

  IMPORTANTE — servos de rotación continua (360°): acá "0..180" no es un
  ángulo de posición, es una intensidad de velocidad. Vas a ver:
    - write(0)   -> gira a velocidad máxima en un sentido
    - write(90)  -> debería quedar DETENIDO (si no queda perfectamente
                    quieto, tu servo necesita calibración de neutro)
    - write(180) -> gira a velocidad máxima en el otro sentido

  Qué hace este test:
    1) Barre el servo de ROTACIÓN de "0" a "180" y de vuelta, despacio.
    2) Barre el servo de IZAJE/ALTURA de "0" a "180" y de vuelta.
    3) Los mueve juntos.
    4) Imprime cada valor por Serial para que veas que el comando
       efectivamente se está enviando.

  Si los servos NO se mueven o tiemblan/resetean la placa:
    - Es casi siempre alimentación. Un puerto USB entrega ~500 mA; dos
      servos bajo carga + la ESP8266 pueden pedir bastante más en los
      picos de arranque. Alimentá los servos con una fuente externa de
      5V (no el pin 5V/VIN de la USB) y ANIMÁ el GND de esa fuente con
      el GND de la ESP8266 (tierra común — esto es obligatorio, si no
      el PWM no tiene referencia).
    - Confirmá que los servos están en D1 (GPIO5) y D2 (GPIO4), o
      cambiá los pines abajo si los cableaste distinto.
    - Si un servo se mueve y el otro no, probá cambiarlos de pin para
      descartar que sea un servo dañado vs. un pin dañado.
  ===========================================================================
*/

#include <Arduino.h>
#include <Servo.h>

const int PIN_ROT   = 5; // D1
const int PIN_HOIST = 4; // D2

Servo servoRot;
Servo servoHoist;

void sweep(Servo &s, const char* label, int delayMs) {
  for (int a = 0; a <= 180; a += 2) {
    s.write(a);
    Serial.print(label); Serial.print(" -> "); Serial.println(a);
    delay(delayMs);
  }
  for (int a = 180; a >= 0; a -= 2) {
    s.write(a);
    Serial.print(label); Serial.print(" -> "); Serial.println(a);
    delay(delayMs);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== TEST DE SERVOS (sin WiFi) ===");

  servoRot.attach(PIN_ROT);
  servoHoist.attach(PIN_HOIST);

  servoRot.write(90);
  servoHoist.write(90);
  Serial.println("Ambos servos centrados en 90°. Empezando en 2s...");
  delay(2000);
}

void loop() {
  Serial.println("--- Barrido: servo ROTACION (D1) ---");
  sweep(servoRot, "ROT", 15);
  servoRot.write(90);
  delay(500);

  Serial.println("--- Barrido: servo IZAJE/ALTURA (D2) ---");
  sweep(servoHoist, "HOIST", 15);
  servoHoist.write(90);
  delay(500);

  Serial.println("--- Ambos servos juntos ---");
  for (int a = 0; a <= 180; a += 2) {
    servoRot.write(a);
    servoHoist.write(180 - a);
    Serial.print("ROT -> "); Serial.print(a);
    Serial.print("  HOIST -> "); Serial.println(180 - a);
    delay(15);
  }
  servoRot.write(90);
  servoHoist.write(90);

  Serial.println("Ciclo completo. Repitiendo en 3s...");
  delay(3000);
}
