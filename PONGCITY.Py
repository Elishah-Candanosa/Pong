# - Modo Pantalla: visualización y sonido en el PC (no se envían frames al Arduino).
# - Modo OLED: se envían frames al Arduino; el Arduino dibuja y (opcionalmente) maneja el zumbador.

import pygame           # Biblioteca para gráficos y sonido en PC
import sys              # Acceso a funciones del sistema (salir)
import serial           # pyserial para comunicación con Arduino
import time             # temporización y sleep
import math             # funciones matemáticas (sin, sqrt, hypot, etc.)
import io               # manejo de bytes en memoria (para generar WAV)
import wave             # crear audio WAV en memoria
import array            # arrays de enteros para samples de audio

# Inicializar pygame (recurso gráfico/sonoro)
pygame.init()

# Resolución de la ventana del juego (PC)
ANCHO, ALTO = 800, 600

# Definición de colores como tuplas RGB
BLANCO = (255, 255, 255)
NEGRO = (0, 0, 0)
DORADO = (255, 215, 0)
COLOR_BOX = (70, 170, 225)

# Opciones visibles en el menú (etiqueta, valor RGB)
OPCIONES_COLOR = [
    ("BLANCO", BLANCO),
    ("ROJO", (255, 0, 0)),
    ("VERDE", (0, 255, 0)),
    ("AZUL", (40, 110, 180)),
    ("AMARILLO", DORADO)
]

# Opciones numéricas para velocidad de paleta
OPCIONES_VELOCIDAD = [4, 6, 8, 10, 12]

# Opciones para puntos máximos a ganar
OPCIONES_PUNTAJE_MAXIMO = [1, 3, 5, 7, 10]

# Opciones de sonido (texto mostrado en menú)
OPCIONES_SONIDO = ["Sí", "No"]

# Opciones de pantalla (ahora en español)
OPCIONES_PANTALLA = ["Pantalla", "OLED"]

# Fuentes tipográficas para distintos tamaños del UI
fuente_titulo   = pygame.font.SysFont("Arial Rounded MT Bold", 60)
fuente_opcion   = pygame.font.SysFont("Arial", 38)
fuente_valor    = pygame.font.SysFont("Arial", 34)
fuente_info     = pygame.font.SysFont("Arial", 24)
fuente_mensaje  = pygame.font.SysFont("Arial", 20)

# Crear la ventana principal de pygame con la resolución ANCHOxALTO
pantalla = pygame.display.set_mode((ANCHO, ALTO))

# Título de la ventana (pestaña)
pygame.display.set_caption('🍍 It\'s Ponguitime, S O B R E V I V E 🍍')

# Intento de inicializar el mezclador de sonido de pygame (si está disponible)
try:
    pygame.mixer.pre_init(frequency=44100, size=-16, channels=1)  # parámetros de audio
    pygame.mixer.init()  # inicializa subsistema audio
except Exception:
    # si falla, seguimos sin sonido en PC (no es crítico)
    pass

# Definición de la melodía usada como victoria (coincide con Arduino)
MELODY_NOTES = [784, 880, 1046, 784, 1046, 1175, 1568, 1760]   # frecuencias Hz
MELODY_DUR_MS = [140, 120, 115, 140, 120, 120, 300, 320]        # duraciones ms

# Función: generar bytes WAV para un tono senoidal simple
def make_tone_wav_bytes(freq, duration_s=0.08, sample_rate=44100, volume=0.5):
    # numero de muestras necesarias para la duración pedida
    n_samples = max(1, int(sample_rate * duration_s))
    max_amp = int(32767 * volume)                # amplitud máxima para 16-bit
    samples = array.array('h')                   # array de enteros 16-bit
    for i in range(n_samples):                   # generar muestras seno
        t = float(i) / sample_rate
        val = int(max_amp * math.sin(2.0 * math.pi * freq * t))
        samples.append(val)
    bio = io.BytesIO()                           # buffer en memoria
    with wave.open(bio, 'wb') as wf:             # escribir WAV en el buffer
        wf.setnchannels(1)                       # mono
        wf.setsampwidth(2)                       # 2 bytes por muestra (16-bit)
        wf.setframerate(sample_rate)
        wf.writeframes(samples.tobytes())        # escribir frames crudos
    return bio.getvalue()                        # devolver bytes WAV

# Función: crear un objeto Sound de pygame a partir de un tono
def make_sound_from_tone(freq, duration_s=0.08, volume=0.5):
    try:
        wav_bytes = make_tone_wav_bytes(freq, duration_s, sample_rate=44100, volume=volume)
        bio = io.BytesIO(wav_bytes)
        return pygame.mixer.Sound(file=bio)     # crea Sound desde archivo en memoria
    except Exception:
        try:
            wav_bytes = make_tone_wav_bytes(freq, duration_s, sample_rate=44100, volume=volume)
            return pygame.mixer.Sound(buffer=wav_bytes)  # alternativa si la anterior falla
        except Exception:
            return None                           # si no se puede, retornamos None

# Precrear un sonido corto para colisiones (si el mixer está inicializado)
COLLISION_BEEP = None
try:
    if pygame.mixer.get_init() is not None:
        COLLISION_BEEP = make_sound_from_tone(800, 0.06, volume=0.5)
except Exception:
    COLLISION_BEEP = None

# Reproducir melodía de victoria en PC (bloqueante la reproducción de la melodía)
def play_victory_pc():
    if pygame.mixer.get_init() is None:
        return   # si no hay soporte de audio, salir
    # Reproducir cada nota con la duración especificada
    for freq, dur in zip(MELODY_NOTES, MELODY_DUR_MS):
        snd = make_sound_from_tone(freq, dur/1000.0, volume=0.5)
        if snd:
            snd.play()
            time.sleep(dur/1000.0 + 0.02)   # esperar la duración + pequeño gap
        else:
            try:
                sys.stdout.write('\a'); sys.stdout.flush()  # fallback: beep del terminal
            except Exception:
                pass

# Dibujar un fondo degradado vertical (para estética)
def fondo_degradado():
    for y in range(ALTO):
        intensidad = int(30 + 110 * y / ALTO)  # intensidad variable con y
        pygame.draw.line(pantalla, (intensidad, intensidad, 120 + y // 14), (0, y), (ANCHO, y))

# Dibujar el estado del juego en pantalla (PC) con las paletas, pelota y puntajes
def dibuja_juego(rect_izq, rect_der, rect_pelota, color_paleta, color_pelota, marca_izq, marca_der, puntaje_ganador):
    fondo_degradado()
    pygame.draw.ellipse(pantalla, color_paleta, rect_izq)   # paleta izquierda
    pygame.draw.ellipse(pantalla, color_paleta, rect_der)   # paleta derecha
    pygame.draw.ellipse(pantalla, color_pelota, rect_pelota) # pelota
    # línea punteada central
    for y in range(0, ALTO, 14):
        pygame.draw.line(pantalla, BLANCO, (ANCHO//2, y), (ANCHO//2, y+7))
    texto_izq = fuente_opcion.render(str(marca_izq), True, color_paleta)
    texto_der = fuente_opcion.render(str(marca_der), True, color_paleta)
    texto_gana = fuente_info.render(f"Puntaje para ganar: {puntaje_ganador}", True, DORADO)
    pantalla.blit(texto_izq, (ANCHO//4, 14))               # mostrar marcador izquierdo
    pantalla.blit(texto_der, (ANCHO*3//4, 14))             # mostrar marcador derecho
    pantalla.blit(texto_gana, (ANCHO//2 - texto_gana.get_width()//2, 65))  # objetivo de puntaje
    texto_mensaje = fuente_mensaje.render("Botón: Cierra el Juego. ¡Esperamos que hayas disfrutado!", True, BLANCO)
    pantalla.blit(texto_mensaje, (ANCHO - texto_mensaje.get_width() - 24, ALTO - texto_mensaje.get_height() - 14))
    pygame.display.flip()                                  # actualizar pantalla completa

# Leer potenciómetros y botones desde Arduino: devuelve 4 valores o None si no hay datos
def leer_potenciometros_y_botones(ser):
    if ser is None:
        return None, None, None, None    # si no hay serial, nada que leer
    try:
        if getattr(ser, 'in_waiting', 0) == 0:   # si no hay bytes disponibles
            return None, None, None, None
        linea = ser.readline().decode(errors='ignore').strip()  # leer línea serial
        datos = linea.split(",")                   # esperaremos CSV pL,pR,b1,b2
        if len(datos) >= 4:
            return int(datos[0]), int(datos[1]), int(datos[2]), int(datos[3])
    except Exception:
        pass
    return None, None, None, None

# Mapear coordenadas del PC a resolución OLED (0..127, 0..63) y enviar CSV al Arduino
def enviar_frame_display(ser, lpaddle, rpaddle, bx, by, score_l, score_r):
    # función auxiliar de mapeo lineal entre rangos
    def mapa_int(val, inmin, inmax, outmin, outmax):
        if inmax == inmin:
            return outmin
        v = max(inmin, min(inmax, val))
        t = float(v - inmin) / float(inmax - inmin)
        return int(round(outmin + t * (outmax - outmin)))
    lp_y = mapa_int(lpaddle.centery, 0, ALTO, 0, 63)   # convertir centro paleta izquierda
    rp_y = mapa_int(rpaddle.centery, 0, ALTO, 0, 63)   # convertir centro paleta derecha
    b_x  = mapa_int(bx, 0, ANCHO, 0, 127)             # convertir centro pelota X
    b_y  = mapa_int(by, 0, ALTO, 0, 63)               # convertir centro pelota Y
    paquete = f"{lp_y},{rp_y},{b_x},{b_y},{score_l},{score_r}\n"  # construir CSV
    try:
        ser.write(paquete.encode())                    # enviar paquete al Arduino
    except Exception:
        pass

# Detectar colisión aproximada entre un óvalo (pelota) y otro óvalo (paleta)
def colision_ovalo(rect_pelota, rect_paleta):
    x_p = rect_pelota.x + rect_pelota.width/2
    y_p = rect_pelota.y + rect_pelota.height/2
    x_r = rect_paleta.x + rect_paleta.width/2
    y_r = rect_paleta.y + rect_paleta.height/2
    a1, b1 = rect_pelota.width/2, rect_pelota.height/2
    a2, b2 = rect_paleta.width/2, rect_paleta.height/2
    dx = x_p - x_r; dy = y_p - y_r
    x_norm = dx / (a1 + a2); y_norm = dy / (b1 + b2)
    return x_norm**2 + y_norm**2 <= 1   # condición elíptica para colisión

# Reflexión física de la pelota contra la superficie elíptica de la paleta
def reflect_ball_against_ellipse(ball_rect, ellipse_rect, dx, dy, ball_radius=None):
    bx = ball_rect.x + ball_rect.width/2.0
    by = ball_rect.y + ball_rect.height/2.0
    ex = ellipse_rect.x + ellipse_rect.width/2.0
    ey = ellipse_rect.y + ellipse_rect.height/2.0
    a = ellipse_rect.width/2.0; b = ellipse_rect.height/2.0
    r = (ball_rect.width/2.0) if ball_radius is None else float(ball_radius)
    vx = bx - ex; vy = by - ey
    # protección contra división por cero
    if abs(vx) < 1e-6 and abs(vy) < 1e-6:
        vx = 0.0001; vy = 1.0
    denom = (vx*vx)/(a*a) + (vy*vy)/(b*b)
    if denom <= 0:
        return dx, dy
    t = 1.0 / math.sqrt(denom)
    px = ex + t*vx; py = ey + t*vy   # punto en la elipse
    ux = bx - px; uy = by - py
    dist = math.hypot(ux, uy)
    if dist == 0:
        nx = (px - ex)/(a*a); ny = (py - ey)/(b*b)
        norm_n = math.hypot(nx, ny)
        if norm_n != 0:
            nx /= norm_n; ny /= norm_n
            bx += nx * (r + 0.6); by += ny * (r + 0.6)
            ball_rect.center = (int(bx), int(by))
    else:
        if dist < r:
            push = (r - dist) + 0.6
            ux_unit = ux / dist; uy_unit = uy / dist
            bx += ux_unit * push; by += uy_unit * push
            ball_rect.center = (int(bx), int(by))
    nx = (px - ex)/(a*a); ny = (py - ey)/(b*b)
    norm_n = math.hypot(nx, ny)
    if norm_n == 0:
        return dx, dy
    nx /= norm_n; ny /= norm_n
    v_dot_n = dx * nx + dy * ny
    dx_new = dx - 2.0 * v_dot_n * nx
    dy_new = dy - 2.0 * v_dot_n * ny
    return dx_new, dy_new

# Función principal que ejecuta una partida
def run_game(ser, color_paleta, color_pelota, velocidad, puntaje_ganador, sonido, play_on_arduino=False):
    # si hay puerto serial, limpiar buffer de entrada
    if ser is not None:
        try:
            ser.reset_input_buffer()
        except Exception:
            pass

    # Si jugamos en OLED y tenemos Arduino: informamos si sonido debe activarse
    # Enviamos S1 para sonido activo o S0 para silenciar el Arduino.
    if play_on_arduino and ser is not None:
        try:
            if sonido:
                ser.write(b'S1\n')  # activar sonido en Arduino
            else:
                ser.write(b'S0\n')  # desactivar sonido en Arduino
            try:
                ser.flush()         # pedir al OS que vacíe buffer si es posible
            except Exception:
                pass
            time.sleep(0.02)       # breve espera para transmisión
        except Exception:
            pass

    # tamaños y rects iniciales de paletas/pelota en escala PC
    ANCHO_PALETA, ALTO_PALETA = 30, 120
    ANCHO_PELOTA, ALTO_PELOTA = 30, 30
    rect_izq = pygame.Rect(20, (ALTO - ALTO_PALETA)//2, ANCHO_PALETA, ALTO_PALETA)
    rect_der = pygame.Rect(ANCHO - 20 - ANCHO_PALETA, (ALTO - ALTO_PALETA)//2, ANCHO_PALETA, ALTO_PALETA)
    rect_pelota = pygame.Rect((ANCHO - ANCHO_PELOTA)//2, (ALTO - ANCHO_PELOTA)//2, ANCHO_PELOTA, ALTO_PELOTA)

    # contadores de puntaje
    marca_izq = marca_der = 0

    # velocidad x/y inicial de la pelota
    dx, dy = 5.0, 5.0

    # Reloj para controlar FPS
    reloj = pygame.time.Clock()
    corriendo = True
    ult_col_rebote = False

    # valores iniciales para potenciómetros remotos (si los hay)
    last_pot1 = 512
    last_pot2 = 512
    last_pot_time = 0.0
    POT_TIMEOUT = 1.0  # tiempo máximo para considerar potes válidos

    # debouncing de botón (botón 1)
    btn1_stable = 1
    btn1_candidate = 1
    btn1_change_time = 0.0
    DEBOUNCE_SEC = 0.05

    # intervalo de envío de frames al Arduino (mejor mantener 60Hz)
    SEND_INTERVAL = 1.0 / 60.0
    last_send = time.time()

    kb_speed = velocidad  # velocidad por teclado igual a la opción elegida

    # mostrar pantalla de victoria en PC (bloqueante por 2.5s o hasta tecla)
    def show_victory_screen_pc(texto):
        end_t = time.time() + 2.5
        while time.time() < end_t:
            for ev in pygame.event.get():
                if ev.type == pygame.QUIT:
                    pygame.quit(); sys.exit()
                if ev.type == pygame.KEYDOWN:
                    return
            pantalla.fill(BLANCO)
            txt = fuente_titulo.render(texto, True, DORADO)
            pantalla.blit(txt, (ANCHO//2 - txt.get_width()//2, ALTO//2 - txt.get_height()//2))
            sub = fuente_info.render("Presiona cualquier tecla para volver al menú", True, BLANCO)
            pantalla.blit(sub, (ANCHO//2 - sub.get_width()//2, ALTO//2 + txt.get_height()//2 + 10))
            pygame.display.flip()
            reloj.tick(30)

    # Bucle principal del juego
    while corriendo:
        colision_rebote = False

        # eventos de pygame (teclado, cerrar ventana)
        for evento in pygame.event.get():
            if evento.type == pygame.QUIT:
                pygame.quit(); sys.exit()
            if evento.type == pygame.KEYDOWN:
                if evento.key == pygame.K_ESCAPE:
                    corriendo = False
                    break

        # leer potenciómetros y botones del Arduino si hay datos
        r_pot1, r_pot2, r_btn1, r_btn2 = leer_potenciometros_y_botones(ser)

        # actualizar valores suavizados de potenciómetros si hay lectura
        if r_pot1 is not None:
            last_pot1 = int(0.6 * last_pot1 + 0.4 * r_pot1)
            last_pot_time = time.time()
        if r_pot2 is not None:
            last_pot2 = int(0.6 * last_pot2 + 0.4 * r_pot2)
            last_pot_time = time.time()

        now = time.time()
        # debouncing del botón 1 remoto (si existe)
        if r_btn1 is not None:
            if r_btn1 != btn1_candidate:
                btn1_candidate = r_btn1
                btn1_change_time = now
            else:
                if now - btn1_change_time >= DEBOUNCE_SEC and btn1_candidate != btn1_stable:
                    prev_btn = btn1_stable
                    btn1_stable = btn1_candidate
                    if prev_btn == 1 and btn1_stable == 0:
                        corriendo = False
                        break

        # Mover paletas: preferir potenciómetros remotos si datos recientes, sino teclado
        if (ser is not None) and (time.time() - last_pot_time) < POT_TIMEOUT:
            y_izq = int((last_pot1 / 1023.0) * (ALTO - ALTO_PALETA))
            y_der = int((last_pot2 / 1023.0) * (ALTO - ALTO_PALETA))
            rect_izq.y = max(0, min(ALTO - ALTO_PALETA, y_izq))
            rect_der.y = max(0, min(ALTO - ALTO_PALETA, y_der))
        else:
            teclas = pygame.key.get_pressed()
            if teclas[pygame.K_w] and rect_izq.top > 0:
                rect_izq.y -= kb_speed
            if teclas[pygame.K_s] and rect_izq.bottom < ALTO:
                rect_izq.y += kb_speed
            if teclas[pygame.K_UP] and rect_der.top > 0:
                rect_der.y -= kb_speed
            if teclas[pygame.K_DOWN] and rect_der.bottom < ALTO:
                rect_der.y += kb_speed

        # iterar pasos pequeños si la velocidad es alta para evitar "atravesar" objetos
        max_step = max(1, int(max(abs(dx), abs(dy)) / 4))
        for _ in range(max_step):
            rect_pelota.x += dx / max_step
            rect_pelota.y += dy / max_step

            # rebote en paredes superior/inferior
            if rect_pelota.top <= 0 or rect_pelota.bottom >= ALTO:
                dy *= -1
                colision_rebote = True

            # colisiones con paleta izquierda (óvalo vs óvalo)
            if colision_ovalo(rect_pelota, rect_izq):
                bx, by = rect_pelota.center
                ex, ey = rect_izq.center
                approach = (bx - ex) * dx + (by - ey) * dy
                if approach < 0:
                    # reflejar vector usando la función física
                    dx, dy = reflect_ball_against_ellipse(rect_pelota, rect_izq, dx, dy, ball_radius=ANCHO_PELOTA/2)
                    colision_rebote = True

            # colisiones con paleta derecha
            if colision_ovalo(rect_pelota, rect_der):
                bx, by = rect_pelota.center
                ex, ey = rect_der.center
                approach = (bx - ex) * dx + (by - ey) * dy
                if approach < 0:
                    dx, dy = reflect_ball_against_ellipse(rect_pelota, rect_der, dx, dy, ball_radius=ANCHO_PELOTA/2)
                    colision_rebote = True

        # Sonido de colisión:
        # - En Portátil: reproducir COLLISION_BEEP local si usuario activó sonido.
        # - En OLED: enviar 'B' al Arduino solo si usuario activó sonido (Arduino decide reproducir o no).
        if colision_rebote and not ult_col_rebote:
            if not play_on_arduino and sonido and COLLISION_BEEP:
                try:
                    COLLISION_BEEP.play()
                except Exception:
                    pass
            elif play_on_arduino and sonido:
                try:
                    ser.write(b'B\n')  # pedir beep al Arduino
                except Exception:
                    pass
        ult_col_rebote = colision_rebote

        # Chequear goles y actualizar marcadores; detectar ganador
        winner = None
        if rect_pelota.left <= 0:
            marca_der += 1
            rect_pelota.x = (ANCHO - rect_pelota.width)//2
            rect_pelota.y = (ALTO - rect_pelota.height)//2
            dx = abs(dx)
            if marca_der >= puntaje_ganador:
                winner = "Derecha"
        if rect_pelota.right >= ANCHO:
            marca_izq += 1
            rect_pelota.x = (ANCHO - rect_pelota.width)//2
            rect_pelota.y = (ALTO - rect_pelota.height)//2
            dx = -abs(dx)
            if marca_izq >= puntaje_ganador:
                winner = "Izquierda"

        # Si hay ganador, manejamos la finalización: en OLED informamos Arduino enviando primero el frame final
        if winner is not None:
            if play_on_arduino and ser is not None:
                try:
                    # enviar el frame final con puntajes actualizados antes de orden de victoria
                    enviar_frame_display(ser, rect_izq, rect_der, rect_pelota.centerx, rect_pelota.centery, marca_izq, marca_der)
                    try:
                        ser.flush()   # intentar vaciar buffer de salida
                    except Exception:
                        pass
                    time.sleep(0.06)   # breve espera para transmisión (ajustable)
                    # enviar comando de victoria; Arduino mostrará overlay y (si está habilitado) melodía
                    ser.write(b'W\n')
                    try:
                        ser.flush()
                    except Exception:
                        pass
                except Exception:
                    pass
                # Mostrar también la pantalla local del PC con el mensaje de ganador
                try:
                    pygame.display.get_surface().fill(BLANCO)
                    texto = fuente_titulo.render(f"{winner} gana!", True, DORADO)
                    pantalla.blit(texto, (ANCHO//2 - texto.get_width()//2, ALTO//2 - texto.get_height()//2))
                    pygame.display.flip()
                except Exception:
                    pass
            else:
                # Modo pantalla: reproducir melodía local en la laptop siempre (incluso si 'Sonido' = "No")
                try:
                    play_victory_pc()
                except Exception:
                    pass
                show_victory_screen_pc(f"{winner} gana!")

            # terminar la partida
            corriendo = False
            break

        # Envío periódico de frames al Arduino (solo si estamos en modo OLED)
        now2 = time.time()
        if play_on_arduino and (now2 - last_send >= SEND_INTERVAL):
            enviar_frame_display(ser, rect_izq, rect_der, rect_pelota.centerx, rect_pelota.centery, marca_izq, marca_der)
            last_send = now2

        # Dibujar: modo PC muestra el juego; modo OLED muestra una pantalla indicativa
        if not play_on_arduino:
            dibuja_juego(rect_izq, rect_der, rect_pelota, color_paleta, color_pelota, marca_izq, marca_der, puntaje_ganador)
        else:
            pantalla.fill((0,0,0))
            info = fuente_info.render("Jugando en OLED (pantalla y sonido en Arduino)", True, BLANCO)
            pantalla.blit(info, (ANCHO//2 - info.get_width()//2, ALTO//2 - info.get_height()//2))
            pygame.display.flip()

        reloj.tick(60)  # limitar a ~60 FPS

    # Antes de salir de la función: si usamos OLED, reenviar S1 (activar sonido) para restaurar estado por defecto
    # Esto es opcional: sirve para que si el usuario salió con sonido desactivado no deje Arduino en silencio permanente.
    if play_on_arduino and ser is not None:
        try:
            ser.write(b'S1\n')
            try:
                ser.flush()
            except Exception:
                pass
        except Exception:
            pass

    return

# Menú controlado por potenciómetros: devuelve la configuración elegida
def menu_con_pots(ser):
    N_OPC = 7
    idx_paleta = idx_pelota = 0
    idx_velocidad = 1
    idx_puntaje = 2
    idx_sonido = 0
    idx_pantalla = 0
    indice_menu = 0

    last_pot1 = 512
    last_pot2 = 512
    btn1_stable = 1
    btn1_candidate = 1
    btn1_change_time = 0.0
    DEBOUNCE_SEC = 0.05
    clock = pygame.time.Clock()

    while True:
        pantalla.fill(NEGRO)
        fondo_degradado()
        sombra = fuente_titulo.render("¡Bienvenido a PongCity!", True, (0, 0, 0))
        pantalla.blit(sombra, (ANCHO//2 - sombra.get_width()//2 + 4, 44))
        titulo = fuente_titulo.render("¡Bienvenido a Pongcity!", True, DORADO)
        pantalla.blit(titulo, (ANCHO//2 - titulo.get_width()//2, 40))

        elementos = [
            ("Pantalla", OPCIONES_PANTALLA[idx_pantalla], (40,110,180)),
            ("Color de Paleta", OPCIONES_COLOR[idx_paleta][0], OPCIONES_COLOR[idx_paleta][1]),
            ("Color de Pelota", OPCIONES_COLOR[idx_pelota][0], OPCIONES_COLOR[idx_pelota][1]),
            ("Velocidad Paleta", str(OPCIONES_VELOCIDAD[idx_velocidad]), (180, 255, 160)),
            ("Máximo Puntaje", str(OPCIONES_PUNTAJE_MAXIMO[idx_puntaje]), (255, 230, 180)),
            ("Sonido", OPCIONES_SONIDO[idx_sonido], DORADO),
            ("¡JUGAR!", "", DORADO)
        ]
        inicio_y = 100; espaciado = 62
        for i, (nombre, valor, color) in enumerate(elementos):
            y = inicio_y + i*espaciado
            box_rect = pygame.Rect(ANCHO//2 - 220, y-15, 440, 54)
            if i == indice_menu:
                pygame.draw.rect(pantalla, COLOR_BOX, box_rect, border_radius=16)
                pygame.draw.rect(pantalla, BLANCO, box_rect, 3, border_radius=16)
            else:
                pygame.draw.rect(pantalla, (70,70,100), box_rect, 1, border_radius=12)
            if i < 6 and i == indice_menu:
                pygame.draw.polygon(pantalla, DORADO, [(box_rect.left+16, y+12), (box_rect.left+32, y+2), (box_rect.left+32, y+24)])
                pygame.draw.polygon(pantalla, DORADO, [(box_rect.right-16, y+12), (box_rect.right-32, y+2), (box_rect.right-32, y+24)])
            texto_opc = fuente_opcion.render(nombre, True, color)
            pantalla.blit(texto_opc, (ANCHO//2 - 210, y))
            if valor:
                texto_val = fuente_valor.render(valor, True, BLANCO)
                pantalla.blit(texto_val, (ANCHO//2 + 85, y+1))

        pygame.display.flip()

        for evento in pygame.event.get():
            if evento.type == pygame.QUIT:
                pygame.quit(); sys.exit()
            if evento.type == pygame.KEYDOWN and evento.key == pygame.K_RETURN:
                if indice_menu == 6:
                    # Devolver configuración elegida: pantalla, color paleta, color pelota, velocidad, puntaje, sonido(bool)
                    return (OPCIONES_PANTALLA[idx_pantalla],
                            OPCIONES_COLOR[idx_paleta][1],
                            OPCIONES_COLOR[idx_pelota][1],
                            OPCIONES_VELOCIDAD[idx_velocidad],
                            OPCIONES_PUNTAJE_MAXIMO[idx_puntaje],
                            OPCIONES_SONIDO[idx_sonido] == "Sí")

        r_pot1, r_pot2, r_btn1, r_btn2 = leer_potenciometros_y_botones(ser)
        if r_pot1 is not None:
            last_pot1 = r_pot1
        if r_pot2 is not None:
            last_pot2 = r_pot2

        now = time.time()
        if r_btn1 is not None:
            if r_btn1 != btn1_candidate:
                btn1_candidate = r_btn1
                btn1_change_time = now
            else:
                if now - btn1_change_time >= DEBOUNCE_SEC and btn1_candidate != btn1_stable:
                    prev_btn1 = btn1_stable
                    btn1_stable = btn1_candidate
                    if prev_btn1 == 1 and btn1_stable == 0:
                        if indice_menu == 6:
                            return (OPCIONES_PANTALLA[idx_pantalla],
                                    OPCIONES_COLOR[idx_paleta][1],
                                    OPCIONES_COLOR[idx_pelota][1],
                                    OPCIONES_VELOCIDAD[idx_velocidad],
                                    OPCIONES_PUNTAJE_MAXIMO[idx_puntaje],
                                    OPCIONES_SONIDO[idx_sonido] == "Sí")

        # Mapear potenciómetro 1 al índice del menú (fila seleccionada)
        indice_menu = int((last_pot1 / 1023.0) * (N_OPC - 1))
        # pot2 controla el valor dentro de la fila (mapeado a 0..100)
        val = int((last_pot2 / 1023.0) * 100)
        if indice_menu == 0:
            idx_pantalla = 0 if val < 50 else 1
        elif indice_menu == 1:
            idx_paleta = min(len(OPCIONES_COLOR)-1, int(val / (100 / len(OPCIONES_COLOR))))
        elif indice_menu == 2:
            idx_pelota = min(len(OPCIONES_COLOR)-1, int(val / (100 / len(OPCIONES_COLOR))))
        elif indice_menu == 3:
            idx_velocidad = min(len(OPCIONES_VELOCIDAD)-1, int(val / (100 / len(OPCIONES_VELOCIDAD))))
        elif indice_menu == 4:
            idx_puntaje = min(len(OPCIONES_PUNTAJE_MAXIMO)-1, int(val / (100 / len(OPCIONES_PUNTAJE_MAXIMO))))
        elif indice_menu == 5:
            idx_sonido = 0 if val < 50 else 1

        clock.tick(60)

# Punto de entrada principal
def main():
    print("Conecta el Arduino y cierra el Monitor Serial antes de continuar.")
    puerto = "/dev/ttyACM0"   # cambiar si tu Arduino está en otro puerto (ej. COM3 en Windows)
    ser = None
    try:
        ser = serial.Serial(puerto, 115200, timeout=0.03)  # abrir puerto serie
        print("Puerto serie abierto:", ser.name)
    except Exception as e:
        print("Advertencia: no se pudo abrir puerto", puerto, "-> usando teclado solamente:", e)
        ser = None

    try:
        # Mostrar menú y obtener configuración
        modo_pantalla, color_paleta, color_pelota, velocidad, puntaje_ganador, sonido = menu_con_pots(ser)
        play_on_arduino = (modo_pantalla == "OLED") and (ser is not None)
        # Ejecutar la partida con la configuración elegida
        run_game(ser, color_paleta, color_pelota, velocidad, puntaje_ganador, sonido, play_on_arduino=play_on_arduino)
    except KeyboardInterrupt:
        print("Interrumpido por usuario.")
    except Exception as e:
        print("Error en el juego:", e)
    finally:
        try:
            if ser is not None:
                ser.close()  # cerrar puerto al terminar
        except Exception:
            pass

if __name__ == "__main__":
    main()
