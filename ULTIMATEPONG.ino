/*
  Arduino Pong receiver (full-buffer variant) with OLED victory message (fixed)
  NOTA: no se modifica la lógica del sketch original, solo se añaden y sustituyen
  comentarios por su traducción y explicaciones en español línea por línea.
*/

#include <U8g2lib.h>         // Librería U8g2 para manejar pantallas OLED
#include <avr/pgmspace.h>    // Utilidades para almacenar datos en PROGMEM (memoria flash)

// Constructor de la pantalla que funcionó en tus pruebas (no modificar)
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);

// Pines empleados en la placa Arduino
const int POT_LEFT = A0;     // Canal analógico A0: potenciómetro izquierdo (si aplica)
const int POT_RIGHT = A1;    // Canal analógico A1: potenciómetro derecho (si aplica)
const int BTN1 = 2;          // Pin digital 2: botón 1 (entrada con pull-up en host)
const int BTN2 = 3;          // Pin digital 3: botón 2
const int BUZZER = 6;        // Pin digital 6: salida para el zumbador (buzzer)

// Temporización para envío de lecturas al host (si se usa)
const unsigned long SEND_INTERVAL_MS = 33UL;  // intervalo en ms (~30 Hz)
unsigned long lastSendMillis = 0;             // marca de tiempo para el envío

// Buffer serial para leer líneas entrantes (tamaño pequeño)
#define LINE_BUF_SIZE 64
char lineBuf[LINE_BUF_SIZE];   // buffer donde se almacena la línea recibida
uint8_t linePos = 0;           // posición actual dentro de lineBuf

// Estado de dibujo en pantalla (variables que se actualizan con los frames recibidos)
int lp_y = 32, rp_y = 32, ball_x = 64, ball_y = 32; // posiciones (centros) por defecto
int score_l = 0, score_r = 0;                       // marcadores izquierdo y derecho

// Melodía almacenada en PROGMEM para ahorrar RAM en AVR
const uint16_t melodyNotes[] PROGMEM = {784, 880, 1046, 784, 1046, 1175, 1568, 1760};
// Duración de cada nota en ms (también en PROGMEM)
const uint16_t melodyDur[]   PROGMEM = {140, 120, 115, 140, 120, 120, 300, 320};
const uint8_t MELODY_LEN = sizeof(melodyNotes) / sizeof(melodyNotes[0]); // longitud de la melodía

// Variables para reproducción de melodía de forma no bloqueante
bool melodyPlaying = false;     // indica si la melodía está en curso
uint8_t melodyIndex = 0;        // índice de la nota actual
unsigned long noteEndMillis = 0;// instante en que termina la nota actual
const unsigned long PAUSE_BETWEEN_NOTES = 40UL; // pausa entre notas en ms

// Control para evitar beeps repetidos (cooldown)
unsigned long lastBeepMillis = 0;
const unsigned long BEEP_COOLDOWN = 40UL; // tiempo mínimo entre beeps cortos

// Estado de la pantalla de victoria (overlay)
bool victoryActive = false;               // overlay activo o no
unsigned long victoryEndMillis = 0;       // instante en que debe ocultarse el overlay
const unsigned long VICTORY_DISPLAY_MS = 2500UL; // duración del overlay en ms
// victorySide: 0 = desconocido, 1 = izquierda, 2 = derecha
uint8_t victorySide = 0;

// Trigger para esperar un pequeño retraso tras recibir 'W' antes de decidir el ganador
bool victoryTriggered = false;            // se activó el trigger de victoria
unsigned long victoryTriggeredMillis = 0; // instante en que se activó el trigger
const unsigned long VICTORY_DELAY_MS = 1000UL; // tiempo de espera antes de decidir ganador

// Función setup() => inicialización de pines, serial y pantalla
void setup() {
  pinMode(BTN1, INPUT_PULLUP);    // configurar botón 1 como INPUT_PULLUP
  pinMode(BTN2, INPUT_PULLUP);    // configurar botón 2 como INPUT_PULLUP
  pinMode(BUZZER, OUTPUT);        // pin del buzzer como salida
  pinMode(LED_BUILTIN, OUTPUT);   // LED incorporado como salida (lo usamos como heartbeat)

  Serial.begin(115200);           // iniciar comunicación serial a 115200 baudios
  u8g2.begin();                   // inicializar la pantalla U8g2

  lineBuf[0] = '\0';              // limpiar buffer de línea
  linePos = 0;                    // reset posición del buffer

  // Pantalla inicial simple para indicar que el programa arrancó
  u8g2.clearBuffer();             // limpiar buffer de la pantalla
  u8g2.setFont(u8g2_font_6x10_tr); // seleccionar fuente pequeña
  u8g2.drawStr(8, 30, "PONG READY"); // dibujar texto fijo (no se modifica)
  u8g2.sendBuffer();              // enviar buffer a la pantalla
  delay(50);                      // pequeño retardo para estabilizar
}

// Lectura segura desde PROGMEM (helper)
static inline uint16_t pgm_read_u16_safe(const uint16_t *p) {
  return (uint16_t)pgm_read_word(p); // leer palabra de PROGMEM de forma segura
}

// Iniciar la melodía (modo no bloqueante)
// Esta función arranca la reproducción de la primera nota y prepara el temporizador
void startMelody() {
  melodyPlaying = true;                           // marcar que se está reproduciendo
  melodyIndex = 0;                                // empezar por la nota 0
  uint16_t n = pgm_read_u16_safe(&melodyNotes[melodyIndex]); // leer frecuencia
  tone(BUZZER, n);                                // empezar a emitir la frecuencia en el buzzer
  uint16_t d = pgm_read_u16_safe(&melodyDur[melodyIndex]);  // leer duración
  noteEndMillis = millis() + d;                   // calcular cuándo termina esta nota
}

// Actualiza la melodía (debe llamarse frecuentemente desde loop())
// Gestiona cambio de nota y finalización sin bloquear el loop principal
void updateMelody() {
  if (!melodyPlaying) return;                     // si no hay melodía, salir rápido
  unsigned long now = millis();                   // instante actual
  if (now >= noteEndMillis) {                     // si la nota actual terminó
    noTone(BUZZER);                               // detener la nota actual
    melodyIndex++;                                // pasar a la siguiente nota
    if (melodyIndex >= MELODY_LEN) {              // si ya no quedan notas
      melodyPlaying = false;                      // marcar como finalizada
      return;
    }
    // leer frecuencia y duración de la siguiente nota y programarla
    uint16_t n = pgm_read_u16_safe(&melodyNotes[melodyIndex]);
    uint16_t d = pgm_read_u16_safe(&melodyDur[melodyIndex]);
    noteEndMillis = now + PAUSE_BETWEEN_NOTES + d; // programar fin de la nueva nota con pequeña pausa
    tone(BUZZER, n);                              // iniciar nueva nota
  }
}

// Reproducir melodía en modo bloqueante (uso especial, bloquea el loop)
void playBlockingMelody() {
  for (uint8_t i = 0; i < MELODY_LEN; ++i) {
    uint16_t n = pgm_read_u16_safe(&melodyNotes[i]); // frecuencia
    uint16_t d = pgm_read_u16_safe(&melodyDur[i]);   // duración
    tone(BUZZER, n);          // emitir la nota
    delay(d);                 // esperar la duración (bloqueante)
    noTone(BUZZER);           // parar la nota
    delay(30);                // pequeño retardo entre notas
  }
}

// Beep corto no bloqueante con protección de cooldown
void playBeep() {
  unsigned long now = millis();                     // instante actual
  if (now - lastBeepMillis < BEEP_COOLDOWN) return; // si aún estamos en cooldown, salir
  lastBeepMillis = now;                             // actualizar marca de tiempo
  tone(BUZZER, 900, 80);                            // tocar tono breve (duración en ms)
}

// Función auxiliar para limitar un entero a un rango [a,b]
int clampInt(int v, int a, int b) {
  if (v < a) return a;
  if (v > b) return b;
  return v;
}

// Procesar una línea recibida por serial (comandos y frames CSV)
// Recibe la línea en 'buf' ya terminada ('\0') y actúa en consecuencia.
void processLine(char *buf) {
  // quitar espacios iniciales (trim left básico)
  while (*buf == ' ' || *buf == '\t') buf++;
  if (*buf == '\0') return; // línea vacía -> nada que hacer

  // Comando corto: 'B' = beep corto
  if ((buf[0] == 'B' || buf[0] == 'b') && buf[1] == '\0') { playBeep(); return; }

  // Comando corto: 'W' = señal de victoria desde el host
  if ((buf[0] == 'W' || buf[0] == 'w') && buf[1] == '\0') {
    // El host indica victoria: en esta versión se INICIA la melodía inmediatamente
    // y además se activa un trigger para esperar un frame final. (lógica original)
    startMelody();                            // iniciar melodía de victoria de inmediato
    victoryTriggered = true;                  // activar trigger para decidir ganador más tarde
    victoryTriggeredMillis = millis();        // marcar el instante de activación
    // No se establece victorySide aquí: se esperará al frame final para ello
    return;
  }

  // Comando corto: 'V' = reproducir melodía en modo bloqueante
  if ((buf[0] == 'V' || buf[0] == 'v') && buf[1] == '\0') { playBlockingMelody(); return; }

  // Si no es comando simple, asumimos que es un frame CSV: lp_y,rp_y,bx,by[,score_l,score_r]
  const int MAXV = 6;                // máximo de valores CSV esperados
  int vals[MAXV] = {0,0,0,0,0,0};    // array para almacenar valores parseados
  int idx = 0;
  char *token = strtok(buf, ",");    // tokenizar por comas
  while (token != NULL && idx < MAXV) {
    vals[idx++] = atoi(token);       // convertir token a entero y almacenar
    token = strtok(NULL, ",");       // siguiente token
  }
  if (idx >= 4) {
    // actualizar el estado de dibujo con los valores recibidos
    lp_y = clampInt(vals[0], 0, 63);
    rp_y = clampInt(vals[1], 0, 63);
    ball_x = clampInt(vals[2], 0, 127);
    ball_y = clampInt(vals[3], 0, 63);
    // si vienen puntajes, actualizarlos también
    if (idx >= 6) { score_l = vals[4]; score_r = vals[5]; }
  }
}

// Dibujar el overlay de victoria en la pantalla OLED
void drawVictoryScreen() {
  u8g2.clearBuffer(); // limpiar buffer de dibujo

  // dibujar rectángulo relleno como fondo del mensaje (área centrada)
  u8g2.drawBox(0, 18, 128, 28);

  // usar color inverso para que el texto aparezca claro sobre fondo oscuro
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_6x10_tr); // fuente legible

  // elegir texto de la primera línea según side (1=Izquierda, 2=Derecha)
  const char *sideText;
  if (victorySide == 1) sideText = "Izquierda";
  else if (victorySide == 2) sideText = "Derecha";
  else sideText = "GANA";

  const char *line2 = "gana!"; // segunda línea fija

  int w1 = u8g2.getStrWidth(sideText); // ancho del texto 1
  int w2 = u8g2.getStrWidth(line2);    // ancho del texto 2

  u8g2.drawStr((128 - w1) / 2, 28, sideText); // dibujar primera línea centrada
  u8g2.drawStr((128 - w2) / 2, 40, line2);    // dibujar segunda línea centrada

  // restaurar color de dibujo normal y mostrar puntajes debajo
  u8g2.setDrawColor(1);
  char s[20];
  snprintf(s, sizeof(s), "%d  -  %d", score_l, score_r); // formatear puntaje
  int sw = u8g2.getStrWidth(s);
  u8g2.drawStr((128 - sw) / 2, 56, s); // dibujar puntaje centrado en la parte inferior

  u8g2.sendBuffer(); // enviar buffer a la pantalla
}

// loop principal: lee serial, actualiza melodía y dibuja la pantalla
void loop() {
  unsigned long now = millis(); // instante actual

  // Envío periódico de lecturas al host (opcional): potenciómetros y botones
  if (now - lastSendMillis >= SEND_INTERVAL_MS) {
    int pL = analogRead(POT_LEFT);   // leer pot izquierdo
    int pR = analogRead(POT_RIGHT);  // leer pot derecho
    int b1 = digitalRead(BTN1);      // leer botón 1
    int b2 = digitalRead(BTN2);      // leer botón 2
    Serial.print(pL); Serial.print(","); // enviar en formato CSV
    Serial.print(pR); Serial.print(",");
    Serial.print(b1); Serial.print(",");
    Serial.println(b2);
    lastSendMillis = now;             // actualizar marca temporal
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // parpadeo LED de actividad
  }

  // Leer serial de forma no bloqueante: construir líneas terminadas en '\n'
  while (Serial.available() > 0) {
    char c = (char)Serial.read();   // leer un carácter
    if (c == '\r') continue;        // ignorar retorno de carro
    if (c == '\n') {                // fin de línea detectado
      lineBuf[linePos] = '\0';      // terminar string
      if (linePos > 0) processLine(lineBuf); // procesar línea completa
      linePos = 0;                  // reset posición del buffer
      lineBuf[0] = '\0';            // limpiar primer carácter
    } else {
      if (linePos < LINE_BUF_SIZE - 1) lineBuf[linePos++] = c; // acumular carácter
      else { linePos = 0; lineBuf[0] = '\0'; } // overflow: descartar línea
    }
  }

  // Actualizar reproducción de melodía (no bloqueante)
  updateMelody();

  // Si se activó el trigger de victoria y ha pasado el delay, decidir ganador y activar overlay
  if (victoryTriggered && (millis() - victoryTriggeredMillis >= VICTORY_DELAY_MS)) {
    // decidir ganador en base a los puntajes más recientes
    if (score_l > score_r) victorySide = 1;
    else if (score_r > score_l) victorySide = 2;
    else victorySide = 0;
    victoryActive = true;                          // activar overlay de victoria
    victoryEndMillis = millis() + VICTORY_DISPLAY_MS; // programar fin del overlay
    victoryTriggered = false;                      // limpiar trigger
  }

  // Si el overlay de victoria está activo, dibujarlo y volver (seguimos leyendo serial y actualizando melodía)
  if (victoryActive) {
    drawVictoryScreen(); // dibuja la pantalla de victoria
    if (millis() >= victoryEndMillis) { // si ya expiró el tiempo de mostrar overlay
      victoryActive = false;            // desactivar overlay
      victorySide = 0;                  // reset del lado ganador
    }
    // retornar para seguir el bucle (permitir seguir recibiendo serial y actualizar melodía)
    return;
  }

  // Dibujar el frame normal del juego (pantalla durante la partida)
  u8g2.clearBuffer(); // limpiar buffer de dibujo

  // Dibujar paletas y bola con los tamaños originales usados en tu código
  u8g2.drawEllipse(8, clampInt(lp_y,0,63), 3, 6, U8G2_DRAW_ALL);   // paleta izquierda
  u8g2.drawEllipse(120, clampInt(rp_y,0,63), 3, 6, U8G2_DRAW_ALL); // paleta derecha
  u8g2.drawEllipse(clampInt(ball_x,0,127), clampInt(ball_y,0,63), 3, 3, U8G2_DRAW_ALL); // bola

  // Línea punteada central (vertical)
  for (int y = 0; y < 64; y += 8) u8g2.drawVLine(64, y, 4);

  // Mostrar puntajes en la parte superior
  u8g2.setFont(u8g2_font_6x10_tr); // fuente pequeña
  char b[16];
  snprintf(b, sizeof(b), "%d", score_l);        // convertir marcador izquierdo a string
  u8g2.drawStr(4, 12, b);                       // dibujar marcador izquierdo
  snprintf(b, sizeof(b), "%d", score_r);        // convertir marcador derecho a string
  u8g2.drawStr(118 - u8g2.getStrWidth(b), 12, b); // dibujar marcador derecho (alineado a la derecha)

  u8g2.sendBuffer(); // enviar buffer a la OLED (refrescar pantalla)

  delay(1); // pausa mínima cooperativa antes de la siguiente iteración del loop
}
