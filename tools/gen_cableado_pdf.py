"""Genera el PDF de cableado PCB FA-10T H1 ↔ Header J8 del Waveshare."""
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle,
    PageBreak, KeepTogether, HRFlowable,
)
from reportlab.lib.enums import TA_LEFT, TA_CENTER

OUTPUT = r"C:\Users\Emilio\FA-10T-firmware\docs\cableado_PCB_J8.pdf"

# --- Estilos ---
styles = getSampleStyleSheet()
title_style = ParagraphStyle(
    "Title", parent=styles["Title"], fontSize=18, leading=22,
    alignment=TA_CENTER, textColor=colors.HexColor("#2E9E3B"), spaceAfter=4,
)
subtitle_style = ParagraphStyle(
    "Subtitle", parent=styles["Normal"], fontSize=10, leading=14,
    alignment=TA_CENTER, textColor=colors.HexColor("#666666"), spaceAfter=14,
)
h1_style = ParagraphStyle(
    "H1", parent=styles["Heading1"], fontSize=14, leading=18,
    textColor=colors.HexColor("#2196F3"), spaceBefore=12, spaceAfter=6,
)
h2_style = ParagraphStyle(
    "H2", parent=styles["Heading2"], fontSize=11, leading=14,
    textColor=colors.HexColor("#333333"), spaceBefore=10, spaceAfter=4,
)
body = ParagraphStyle(
    "Body", parent=styles["Normal"], fontSize=10, leading=14,
    alignment=TA_LEFT, spaceAfter=4,
)
note = ParagraphStyle(
    "Note", parent=styles["Normal"], fontSize=9, leading=12,
    textColor=colors.HexColor("#555555"), leftIndent=12, spaceAfter=3,
)
mono = ParagraphStyle(
    "Mono", parent=styles["Code"], fontSize=8, leading=11,
    backColor=colors.HexColor("#F4F4F4"), borderColor=colors.HexColor("#DDD"),
    borderWidth=0.5, borderPadding=6, leftIndent=0, rightIndent=0,
)

# --- Helpers ---
def section_header(txt, color):
    return Paragraph(txt, ParagraphStyle(
        "SH", parent=styles["Normal"], fontSize=11, leading=14,
        textColor=colors.white, alignment=TA_CENTER,
        backColor=colors.HexColor(color), borderPadding=4,
    ))

# --- Construcción del doc ---
doc = SimpleDocTemplate(
    OUTPUT, pagesize=A4,
    leftMargin=18*mm, rightMargin=18*mm,
    topMargin=15*mm, bottomMargin=15*mm,
    title="Control SPE32-S3 v3.0 — Cableado PCB H1 ↔ Waveshare J8",
    author="Bio Origen", subject="Cableado PCB FA-10T",
)
story = []

# Header
story.append(Paragraph("Control SPE32-S3 v3.0 — Mapeo de cableado", title_style))
story.append(Paragraph(
    "PCB FA-10T H1 &harr; Header J8 del Waveshare ESP32-S3-Touch-LCD-3.5"
    "<br/><b>Bio Origen</b> &middot; firmware fase 4 &middot; 2026-05-28",
    subtitle_style,
))
story.append(HRFlowable(width="100%", thickness=1, color=colors.HexColor("#DDDDDD")))
story.append(Spacer(1, 6))

# ====================================================================
# Tabla 1: orden por PCB
# ====================================================================
story.append(Paragraph("1. Cableado — orden por PCB (H1)", h1_style))
story.append(Paragraph(
    "Tabla principal: usar al fabricar el cable. Cada fila es una conexión 1:1 entre la PCB FA-10T y el header J8 del Waveshare.",
    body,
))
story.append(Spacer(1, 4))

t1_data = [
    ["H1 PCB", "Señal", "GPIO ESP32-S3", "J8 Waveshare", "Estado"],
    ["Pin 1", "+5V",                       "—",                "Pin 2",  "medido"],
    ["Pin 2", "GND",                       "—",                "Pin 4",  "medido"],
    ["Pin 3", "+3V3",                      "—",                "Pin 31", "medido"],
    ["Pin 4", "ADC_IN PT1000",             "GPIO9 (ADC1_CH8)", "Pin 14", "firmware OK"],
    ["Pin 5", "SSR_DRV (resistencias)",    "GPIO21",           "Pin 5",  "verificado"],
    ["Pin 6", "SSR_FAN (turbinas)",        "GPIO17",           "Pin 16", "verificado"],
    ["Pin 7", "SSR_AUX (extractor 4\")",   "GPIO18",           "Pin 18", "verificado"],
    ["Pin 8", "I_FAN (no usado en v1)",    "—",                "—",      "sin conectar"],
    ["Pin 9", "I2C_SDA (SHT31)",           "GPIO8",            "Pin 28", "medido"],
    ["Pin 10","I2C_SCL (SHT31)",           "GPIO7",            "Pin 26", "medido"],
]
t1 = Table(t1_data, colWidths=[20*mm, 50*mm, 33*mm, 28*mm, 28*mm])
t1.setStyle(TableStyle([
    # header
    ("BACKGROUND", (0,0), (-1,0), colors.HexColor("#2E9E3B")),
    ("TEXTCOLOR",  (0,0), (-1,0), colors.white),
    ("FONTNAME",   (0,0), (-1,0), "Helvetica-Bold"),
    ("FONTSIZE",   (0,0), (-1,0), 10),
    ("ALIGN",      (0,0), (-1,0), "CENTER"),
    ("BOTTOMPADDING", (0,0), (-1,0), 6),
    ("TOPPADDING",    (0,0), (-1,0), 6),
    # body
    ("FONTSIZE",  (0,1), (-1,-1), 9),
    ("ALIGN",     (0,1), (0,-1), "CENTER"),
    ("ALIGN",     (3,1), (3,-1), "CENTER"),
    ("ALIGN",     (4,1), (4,-1), "CENTER"),
    ("VALIGN",    (0,0), (-1,-1), "MIDDLE"),
    ("ROWBACKGROUNDS", (0,1), (-1,-1),
       [colors.HexColor("#F8F8F8"), colors.white]),
    ("GRID",      (0,0), (-1,-1), 0.4, colors.HexColor("#CCCCCC")),
    ("BOTTOMPADDING", (0,1), (-1,-1), 4),
    ("TOPPADDING",    (0,1), (-1,-1), 4),
    # row 'sin conectar' en gris claro
    ("TEXTCOLOR", (0,8), (-1,8), colors.HexColor("#999999")),
]))
story.append(t1)
story.append(Spacer(1, 10))

# ====================================================================
# Tabla 2: orden por J8
# ====================================================================
story.append(Paragraph("2. Cableado — orden por J8 (header del Waveshare)", h1_style))
story.append(Paragraph(
    "Misma información ordenada por número de pin del header J8 — útil cuando se está mirando el header del módulo.",
    body,
))
story.append(Spacer(1, 4))

t2_data = [
    ["J8 Pin", "Señal",    "GPIO",   "Va a H1 Pin"],
    ["Pin 2",  "+5V",      "—",      "Pin 1"],
    ["Pin 4",  "GND",      "—",      "Pin 2"],
    ["Pin 5",  "SSR_DRV",  "GPIO21", "Pin 5"],
    ["Pin 14", "ADC_IN",   "GPIO9",  "Pin 4"],
    ["Pin 16", "SSR_FAN",  "GPIO17", "Pin 6"],
    ["Pin 18", "SSR_AUX",  "GPIO18", "Pin 7"],
    ["Pin 26", "I2C_SCL",  "GPIO7",  "Pin 10"],
    ["Pin 28", "I2C_SDA",  "GPIO8",  "Pin 9"],
    ["Pin 31", "+3V3",     "—",      "Pin 3"],
]
t2 = Table(t2_data, colWidths=[26*mm, 40*mm, 36*mm, 36*mm])
t2.setStyle(TableStyle([
    ("BACKGROUND", (0,0), (-1,0), colors.HexColor("#2196F3")),
    ("TEXTCOLOR",  (0,0), (-1,0), colors.white),
    ("FONTNAME",   (0,0), (-1,0), "Helvetica-Bold"),
    ("FONTSIZE",   (0,0), (-1,0), 10),
    ("ALIGN",      (0,0), (-1,-1), "CENTER"),
    ("BOTTOMPADDING", (0,0), (-1,0), 6),
    ("TOPPADDING",    (0,0), (-1,0), 6),
    ("FONTSIZE",  (0,1), (-1,-1), 9),
    ("VALIGN",    (0,0), (-1,-1), "MIDDLE"),
    ("ROWBACKGROUNDS", (0,1), (-1,-1),
       [colors.HexColor("#F8F8F8"), colors.white]),
    ("GRID",      (0,0), (-1,-1), 0.4, colors.HexColor("#CCCCCC")),
    ("BOTTOMPADDING", (0,1), (-1,-1), 4),
    ("TOPPADDING",    (0,1), (-1,-1), 4),
]))
story.append(t2)
story.append(Spacer(1, 10))

# ====================================================================
# Resumen de cables
# ====================================================================
story.append(Paragraph("3. Resumen — 9 cables a fabricar", h1_style))
res_data = [
    ["Tipo", "Cables", "Detalle"],
    ["Alimentación",  "3", "+5V, GND, +3V3"],
    ["Entrada ADC",   "1", "ADC_IN (PT1000 vía divisor)"],
    ["Salidas SSR",   "3", "DRV (resistencias), FAN (turbinas), AUX (extractor 4\")"],
    ["Bus I2C",       "2", "SDA + SCL (SHT31 humedad)"],
    ["",              "9", "TOTAL (Pin 8 I_FAN queda sin usar en v1)"],
]
res_tbl = Table(res_data, colWidths=[34*mm, 18*mm, 90*mm])
res_tbl.setStyle(TableStyle([
    ("BACKGROUND", (0,0), (-1,0), colors.HexColor("#E87A20")),
    ("TEXTCOLOR",  (0,0), (-1,0), colors.white),
    ("FONTNAME",   (0,0), (-1,0), "Helvetica-Bold"),
    ("FONTSIZE",   (0,0), (-1,0), 10),
    ("ALIGN",      (0,0), (-1,0), "CENTER"),
    ("BOTTOMPADDING", (0,0), (-1,0), 6),
    ("TOPPADDING",    (0,0), (-1,0), 6),
    ("FONTSIZE",  (0,1), (-1,-1), 9),
    ("VALIGN",    (0,0), (-1,-1), "MIDDLE"),
    ("ALIGN",     (1,1), (1,-1), "CENTER"),
    ("GRID",      (0,0), (-1,-1), 0.4, colors.HexColor("#CCCCCC")),
    ("BOTTOMPADDING", (0,1), (-1,-1), 4),
    ("TOPPADDING",    (0,1), (-1,-1), 4),
    ("FONTNAME",  (0,-1), (-1,-1), "Helvetica-Bold"),
    ("BACKGROUND", (0,-1), (-1,-1), colors.HexColor("#FFF4E5")),
]))
story.append(res_tbl)
story.append(PageBreak())

# ====================================================================
# Notas
# ====================================================================
story.append(Paragraph("4. Notas importantes para el cableado", h1_style))

notas = [
    ("Bus I2C compartido",
     "El bus I2C (GPIO7/SCL + GPIO8/SDA) es el MISMO que usan los periféricos onboard del Waveshare: "
     "touch FT6336, codec audio ES8311, expansor TCA9554, PMU AXP2101, RTC PCF85063, IMU QMI8658. "
     "El SHT31 de la PCB FA-10T responde en dirección <b>0x44</b>, que no colisiona con ningún device del bus. "
     "Velocidad del bus: 400 kHz, manejado por sht31_init()."),

    ("Compensación del PT1000 en GPIO9",
     "GPIO9 tiene un pull-up de 10 k&Omega; a 3V3 onboard del Waveshare (es el SD_MISO del socket microSD). "
     "Ese pull-up queda en paralelo con el R3=2.2 k&Omega; de la PCB FA-10T: "
     "R_eff = 2200 || 10000 &asymp; 1803 &Omega;. "
     "El firmware ya está compensado: <b>PT1000_R_TOP_OHMS = 1803.0f</b> en app_config.h."),

    ("GND común",
     "El GND del header J8 (Pin 4) es el mismo que el del USB-C del board. "
     "Si la PCB FA-10T se alimenta independientemente del Waveshare, UNIR los GND de ambas placas "
     "para que la referencia de los SSR sea válida. Sin GND común los opto-acopladores PC817 no conmutan "
     "correctamente."),

    ("Pull-down externo en los SSR",
     "Cada línea de SSR (DRV, FAN, AUX) en la PCB FA-10T debe tener un pull-down de <b>10 k&Omega; a GND</b> "
     "para que los opto PC817 queden OFF durante el boot (los primeros ~700 ms hasta que gpio_config() "
     "toma el control del pin). Sin este pull-down, los SSR pueden quedar momentáneamente energizados "
     "al encender el equipo."),

    ("Modo de prueba con sensores simulados",
     "El firmware tiene un flag <b>SENSORS_SIMULATION</b> en app_config.h que cuando vale 1 hace que los "
     "drivers de sensores (PT1000, SHT31, ACS712) devuelvan datos sintéticos. La UI sigue funcionando "
     "contra el display y audio reales. Útil para probar la pantalla sin necesidad de tener la PCB "
     "conectada. Antes de cablear la PCB definitiva, pasar este flag a 0 y re-flashear."),
]
for title, txt in notas:
    story.append(Paragraph(f"<b>{title}</b>", h2_style))
    story.append(Paragraph(txt, note))
    story.append(Spacer(1, 4))

# ====================================================================
# Estado del firmware
# ====================================================================
story.append(Paragraph("5. Estado del firmware contra esta tabla", h1_style))
story.append(Paragraph(
    "Las macros de pines en <i>main/app_config.h</i> ya coinciden con la tabla. No hace falta cambiar nada "
    "del firmware antes de cablear:",
    body,
))
story.append(Spacer(1, 4))

code = (
"#define PIN_PT1000_ADC               9       // ← H1 Pin 4  → J8 Pin 14<br/>"
"#define PIN_SSR_DRV                  21      // ← H1 Pin 5  → J8 Pin 5<br/>"
"#define PIN_SSR_FAN                  17      // ← H1 Pin 6  → J8 Pin 16<br/>"
"#define PIN_SSR_AUX                  18      // ← H1 Pin 7  → J8 Pin 18<br/>"
"#define PIN_I2C_SDA                  8       // ← H1 Pin 9  → J8 Pin 28<br/>"
"#define PIN_I2C_SCL                  7       // ← H1 Pin 10 → J8 Pin 26<br/>"
"#define PT1000_R_TOP_OHMS            1803.0f // ← 2200 || 10000 pull-up SD"
)
story.append(Paragraph(code, mono))
story.append(Spacer(1, 8))

story.append(Paragraph("Pasos al conectar la PCB FA-10T por primera vez:", h2_style))
pasos = [
    "Conectar los 9 cables siguiendo la tabla de la página 1 (verificar 2 veces antes de energizar).",
    "Editar main/app_config.h: cambiar SENSORS_SIMULATION de 1 a 0.",
    "Compilar y flashear: <i>idf.py -p COM3 flash monitor</i>.",
    "Verificar en el log de boot las 3 líneas siguientes:",
]
for p in pasos:
    story.append(Paragraph(f"&bull; {p}", note))
story.append(Spacer(1, 4))
log_code = (
"I (xxx) ssr3ch: SSR3 init: DRV=GPIO21 FAN=GPIO17 AUX=GPIO18 period=1000ms<br/>"
"I (xxx) pt1000: PT1000 init: GPIO9 (ADC1 ch8), Rtop=1803&Omega; Rser=100&Omega;<br/>"
"I (xxx) sht31: SHT31 init: SDA=8 SCL=7 addr=0x44"
)
story.append(Paragraph(log_code, mono))
story.append(Spacer(1, 6))
story.append(Paragraph(
    "Con la sonda PT1000 desconectada se va a ver <b>PT1000 fault status=0x01</b> (OPEN, esperable) "
    "y la pantalla entra en ALARMA. Al conectar la sonda y estabilizarse a temperatura ambiente "
    "debe leer ~25 °C. Si hay un offset constante, ajustarlo desde AREA TECNICA → CALIBRAR.",
    body,
))

# Footer
story.append(Spacer(1, 14))
story.append(HRFlowable(width="100%", thickness=0.5, color=colors.HexColor("#DDDDDD")))
story.append(Paragraph(
    "<i>Bio Origen &mdash; Control SPE32-S3 v3.0 &mdash; documento generado automáticamente "
    "desde tools/gen_cableado_pdf.py</i>",
    ParagraphStyle("foot", parent=styles["Normal"], fontSize=7,
                   alignment=TA_CENTER, textColor=colors.HexColor("#888888"))
))

doc.build(story)
print(f"PDF generado: {OUTPUT}")
