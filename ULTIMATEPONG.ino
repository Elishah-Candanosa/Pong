#include <U8g2lib.h>         // Librería U8g2 para manejar pantallas OLED. Según stack overflow, funciona.
#include <avr/pgmspace.h>    // Utilidades para almacenar datos en PROGMEM (memoria flash)

//Constructor del Display. Qué cosa más tediosa fue encontrar cuál servía.
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);

// Pines empleados en la placa Arduino
const int POT_IZQ = A0;     
const int POT_DER = A1;    
const int BTN1 = 2;          
const int BTN2 = 3;          
const int BUZZER = 6;        

// Temporización para envío de lecturas a la lap.
const unsigned long Mandar_Intervalo_MS = 33UL;  // intervalo en ms (~30 Hz)
unsigned long lastSendMillis = 0;             // marca de tiempo para el envío

// Buffer serial para leer líneas entrantes (pa la info del juego)
#define LINE_BUF_SIZE 64
char lineBuf[LINE_BUF_SIZE];   // buffer donde se almacena la línea recibida
uint8_t linePos = 0;           // posición actual dentro de lineBuf

// Estado de dibujo en pantalla (variables que se actualizan con los frames recibidos)
int lp_y = 32, rp_y = 32, ball_x = 64, ball_y = 32; // posiciones (centros) por defecto. ESto es lo que leo debe verificar
int puntaje_izq = 0, puntaje_der = 0;                       //Marcadores de ambos jugadores. Parecen bolitas, pero están bn.

// Melodía almacenada en PROGMEM para ahorrar RAM en AVR
const uint16_t melodiaNotas[] PROGMEM = {784, 880, 1046, 784, 1046, 1175, 1568, 1760};
// Duración de cada nota en ms (también en PROGMEM)
const uint16_t melodiaDuracion[]   PROGMEM = {140, 120, 115, 140, 120, 120, 300, 320};
const uint8_t melodialongitud = sizeof(melodiaNotas) / sizeof(melodiaNotas[0]); // longitud de la melodía

// Variables para reproducción de melodía de forma no bloqueante
bool aromperlabocina = false;     
uint8_t indicemelodia = 0;        
unsigned long noteEndMillis = 0;// instante en que termina la nota actual
const unsigned long pausa_entre_notuelas = 40UL; 

// Control para evitar beeps repetidos (cooldown)
unsigned long ultiBeepMilis = 0;
const unsigned long Para_de_beepear = 40UL; // tiempo mínimo entre beeps cortos

// Estado de la pantalla de victoria (overlay)
bool ya_se_gano = false;               // overlay activo o no
unsigned long victoriaEndMillis = 0;       // instante en que debe ocultarse el overlay
const unsigned long Muestra_Ganadores_YA = 2500UL; // duración del overlay en ms

// 0 = desconocido, 1 = izquierda, 2 = derecha
uint8_t decision_del_destino = 0;

// Trigger para esperar un pequeño retraso tras recibir 'W' antes de decidir el ganador. DE repente la pantallita sólo decía
//GANA: "" y ps qué es eso ajsjajsjasj
bool triger_victoria = false;            // se activó el trigger de victoria
unsigned long triger_victoriaMillis = 0; // instante en que se activó el trigger
const unsigned long Delay_Victoria_MS = 1000UL; // tiempo de espera antes de decidir ganador

// Función setup() => inicialización de pines, serial y pantalla
void setup() {
  pinMode(BTN1, INPUT_PULLUP);    // configurar botón 1 como INPUT_PULLUP
  pinMode(BTN2, INPUT_PULLUP);    // configurar botón 2 como INPUT_PULLUP
  pinMode(BUZZER, OUTPUT);      
  pinMode(LED_BUILTIN, OUTPUT);   // Para saber si sí está recibiendo info, si parpadea mal, es que algo está mal

  Serial.begin(115200);           // iniciar comunicación serial a 115200 baudios
  u8g2.begin();                   // inicializar la pantalla U8g2

  lineBuf[0] = '\0';              // limpiar buffer de línea
  linePos = 0;                    // reset posición del buffer

  // Pantalla inicial simple para indicar que el programa arrancó
  u8g2.clearBuffer();             // limpiar buffer de la pantalla
  u8g2.setFont(u8g2_font_6x10_tr); // seleccionar fuente pequeña
  u8g2.drawStr(8, 30, "PONG READY");
  u8g2.sendBuffer();              
  delay(50);                      
}

// Lectura segura desde PROGMEM (helper)
static inline uint16_t pgm_read_u16_safe(const uint16_t *p) {
  return (uint16_t)pgm_read_word(p); // leer palabra de PROGMEM de forma segura
}

// Iniciar la melodía (modo no bloqueante)
// Esta función arranca la reproducción de la primera nota y prepara el temporizador
void Hora_de_la_Orquesta() {
  aromperlabocina = true;                           
  indicemelodia = 0;                                // empezar por la nota 0
  uint16_t n = pgm_read_u16_safe(&melodiaNotas[indicemelodia]); // leer frecuencia
  tone(BUZZER, n);                                
  uint16_t d = pgm_read_u16_safe(&melodiaDuracion[indicemelodia]);  // leer duración
  noteEndMillis = millis() + d;                   // calcular cuándo termina esta nota
}

// Actualiza la melodía (debe llamarse frecuentemente desde loop())
// Gestiona cambio de nota y finalización sin bloquear el loop principal
//No creo que se arme, pero Héctor quiere meter música. No sabe lo que sufrí haciendo esto ajsjasjajs.
void Actualiza_Melodia() {
  if (!aromperlabocina) return;                     // si no hay melodía, salir rápido
  unsigned long now = millis();                   // instante actual
  if (now >= noteEndMillis) {                     // si la nota actual terminó
    noTone(BUZZER);                               // detener la nota actual
    indicemelodia++;                                // pasar a la siguiente nota
    if (indicemelodia >= melodialongitud) {              // si ya no quedan notas
      aromperlabocina = false;                      // marcar como finalizada
      return;
    }
    // leer frecuencia y duración de la siguiente nota y programarla
    uint16_t n = pgm_read_u16_safe(&melodiaNotas[indicemelodia]);
    uint16_t d = pgm_read_u16_safe(&melodiaDuracion[indicemelodia]);
    noteEndMillis = now + pausa_entre_notuelas + d; // programar fin de la nueva nota con pequeña pausa
    tone(BUZZER, n);                              // iniciar nueva nota
  }
}

// Reproducir melodía en modo bloqueante (uso especial, bloquea el loop)
//Sugerencia de Stack Overflow. Por alguna razón sirve
void playBlockingMelody() {
  for (uint8_t i = 0; i < melodialongitud; ++i) {
    uint16_t n = pgm_read_u16_safe(&melodiaNotas[i]); // frecuencia
    uint16_t d = pgm_read_u16_safe(&melodiaDuracion[i]);   // duración
    tone(BUZZER, n);          // emitir la nota
    delay(d);                 // esperar la duración (bloqueante)
    noTone(BUZZER);           // parar la nota
    delay(30);                // pequeño retardo entre notas
  }
}

// Beep corto no bloqueante con protección de cooldown
void Beepea() {
  unsigned long now = millis();                     // instante actual
  if (now - ultiBeepMilis < Para_de_beepear) return; // si aún estamos en cooldown, salir
  ultiBeepMilis = now;                             // actualizar marca de tiempo
  tone(BUZZER, 900, 80);                            // tocar tono breve (duración en ms)
}

// Función auxiliar para limitar un entero a un rango [a,b]
//Robado de Stack Overflow
int clampInt(int v, int a, int b) {
  if (v < a) return a;
  if (v > b) return b;
  return v;
}

// Procesar una línea recibida por serial (comandos y frames CSV)
// Recibe la línea en 'buf' ya terminada ('\0') y actúa en consecuencia.
void destila_informacion(char *buf) {
  // quitar espacios iniciales (trim left básico)
  while (*buf == ' ' || *buf == '\t') buf++;
  if (*buf == '\0') return; // línea vacía -> nada que hacer

  if ((buf[0] == 'B' || buf[0] == 'b') && buf[1] == '\0') { Beepea(); return; }

  if ((buf[0] == 'W' || buf[0] == 'w') && buf[1] == '\0') {
    Hora_de_la_Orquesta();                            // iniciar melodía de victoria de inmediato
    triger_victoria = true;                  // activar trigger para decidir ganador más tarde
    triger_victoriaMillis = millis();        // marcar el instante de activación
    // No se establece decision_del_destino aquí: se esperará al frame final para ello
    return;
  }

  // Comando corto: 'V' = reproducir melodía en modo bloqueante
  if ((buf[0] == 'V' || buf[0] == 'v') && buf[1] == '\0') { playBlockingMelody(); return; }

  // Si no es comando simple, asumimos que es un frame CSV: lp_y,rp_y,bx,by[,puntaje_izq,puntaje_der].Así viene de PongCity en Python
  const int MAXV = 6;                // máximo de valores CSV esperados (pq los puntajes vienen al final, por eso 6)
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
    if (idx >= 6) { puntaje_izq = vals[4]; puntaje_der = vals[5]; }
  }
}

// Dibujar el overlay de victoria en la pantalla OLED
void dibuja_pantalla_exitosa() {
  u8g2.clearBuffer(); // limpiar buffer de dibujo

  // dibujar rectángulo relleno como fondo del mensaje (área centrada)
  u8g2.drawBox(0, 18, 128, 28);

  // usar color inverso para que el texto aparezca claro sobre fondo oscuro
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_6x10_tr); // fuente legible

  // elegir texto de la primera línea según side (1=Izquierda, 2=Derecha)
  const char *sideText;
  if (decision_del_destino == 1) sideText = "Izquierda";
  else if (decision_del_destino == 2) sideText = "Derecha";
  else sideText = "GANA";

  const char *line2 = "gana!"; // segunda línea fija

  int w1 = u8g2.getStrWidth(sideText); // ancho del texto 1
  int w2 = u8g2.getStrWidth(line2);    // ancho del texto 2

  u8g2.drawStr((128 - w1) / 2, 28, sideText); // dibujar primera línea centrada
  u8g2.drawStr((128 - w2) / 2, 40, line2);    // dibujar segunda línea centrada

  // restaurar color de dibujo normal y mostrar puntajes debajo
  u8g2.setDrawColor(1);
  char s[20];
  snprintf(s, sizeof(s), "%d  -  %d", puntaje_izq, puntaje_der); // formatear puntaje
  int sw = u8g2.getStrWidth(s);
  u8g2.drawStr((128 - sw) / 2, 56, s); // dibujar puntaje centrado en la parte inferior

  u8g2.sendBuffer(); // enviar buffer a la pantalla
}

// loop principal: lee serial, actualiza melodía y dibuja la pantalla
void loop() {
  unsigned long now = millis(); // instante actual

  // Envío periódico de lecturas a la lap. COmunicación Tierra-Cielo
  if (now - lastSendMillis >= Mandar_Intervalo_MS) {
    int pL = analogRead(POT_IZQ);   // leer pot izquierdo
    int pR = analogRead(POT_DER);  // leer pot derecho
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
      if (linePos > 0) destila_informacion(lineBuf); // procesar línea completa
      linePos = 0;                  // reset posición del buffer
      lineBuf[0] = '\0';            // limpiar primer carácter
    } else {
      if (linePos < LINE_BUF_SIZE - 1) lineBuf[linePos++] = c; // acumular carácter
      else { linePos = 0; lineBuf[0] = '\0'; } // overflow: descartar línea
    }
  }

  // Actualizar reproducción de melodía (no bloqueante)
  Actualiza_Melodia();

  // Si se activó el trigger de victoria y ha pasado el delay, decidir ganador y activar overlay
  if (triger_victoria && (millis() - triger_victoriaMillis >= Delay_Victoria_MS)) {
    // decidir ganador en base a los puntajes más recientes
    if (puntaje_izq > puntaje_der) decision_del_destino = 1;
    else if (puntaje_der > puntaje_izq) decision_del_destino = 2;
    else decision_del_destino = 0;
    ya_se_gano = true;                          // activar overlay de victoria
    victoriaEndMillis = millis() + Muestra_Ganadores_YA; // programar fin del overlay
    triger_victoria = false;                      // limpiar trigger
  }

  // Si el overlay de victoria está activo, dibujarlo y volver (seguimos leyendo serial y actualizando melodía)
  if (ya_se_gano) {
    dibuja_pantalla_exitosa(); // dibuja la pantalla de victoria
    if (millis() >= victoriaEndMillis) { // si ya expiró el tiempo de mostrar overlay
      ya_se_gano = false;            // desactivar overlay
      decision_del_destino = 0;                  // reset del lado ganador
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
  snprintf(b, sizeof(b), "%d", puntaje_izq);        // convertir marcador izquierdo a string
  u8g2.drawStr(4, 12, b);                       // dibujar marcador izquierdo
  snprintf(b, sizeof(b), "%d", puntaje_der);        // convertir marcador derecho a string
  u8g2.drawStr(118 - u8g2.getStrWidth(b), 12, b); // dibujar marcador derecho (alineado a la derecha)

  u8g2.sendBuffer(); // enviar buffer a la OLED (refrescar pantalla)

  delay(1); // pausa mínima cooperativa antes de la siguiente iteración del loop
}
