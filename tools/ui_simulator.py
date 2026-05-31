#!/usr/bin/env python3
# Control SPE32-S3 v3.0 — visual UI mock of the LVGL screens.
#
# Renders the eight on-device screens as designed in main/screens/ at the
# native 480×320 resolution using tkinter (Python stdlib — no external deps).
# Navigation works: click the left-panel buttons to switch screens. The "func"
# screens animate live data so you can see the layout in motion.
#
#   py -3 tools/ui_simulator.py
#
# Colors match ui_common.h. Layouts are approximations of the LVGL builds in
# main/screens/screen_*.c — useful for visualising the UI without flashing
# firmware.

from __future__ import annotations

import math
import random
import time
import tkinter as tk

# --------------------------------------------------------------------------
#   Palette — same hex values as ui_common.h
# --------------------------------------------------------------------------
COL_PANEL_BG   = "#111111"
COL_LEFT_BG    = "#FFFFFF"
COL_ORANGE     = "#E87A20"
COL_GREEN      = "#2E9E3B"
COL_GREEN_DEEP = "#2E7D32"
COL_BLUE       = "#2196F3"
COL_RED        = "#D32F2F"
COL_GREY       = "#888888"
COL_TEMP_RED   = "#D03000"
COL_TIME_BLUE  = "#1565C0"
COL_YELLOW     = "#FDD835"
COL_CYAN       = "#4FC3F7"
COL_WHITE      = "#FFFFFF"
COL_BLACK      = "#000000"

W, H = 480, 320
LEFT_W = 120

# Try to use a clean sans font when available; fall back to system default.
F_XS  = ("Arial",  8)
F_SM  = ("Arial", 10)
F_MD  = ("Arial", 12)
F_LG  = ("Arial", 14, "bold")
F_XL  = ("Arial", 18, "bold")
F_XXL = ("Arial", 22, "bold")
F_HUGE = ("Arial", 36, "bold")


# --------------------------------------------------------------------------
#   Shared simulation state — the values the "func" screens animate.
# --------------------------------------------------------------------------
class SimState:
    def __init__(self):
        self.t_start = time.monotonic()
        self.temp = 25.0
        self.setpoint = 60.0
        self.humidity = 70.0
        self.fan_current = 0.45
        self.fan_nominal = 0.45
        self.session_total_s = 30 * 60
        self.session_elapsed_s = 0
        self.pid_out = 0.0
        self.ssr_drv = 0.0
        self.ssr_fan = 1.0
        self.ssr_aux = 0.0
        self.run_state = "RUNNING"     # IDLE / RUNNING / PAUSED / COMPLETED / ALARMA
        self.etapa_activa = 0
        self.etapas = [(50, 15 * 60), (60, 10 * 60), (70, 5 * 60)]
        self.nombre_programa = "DEMO"
        self.t_min = 25.0
        self.t_max = 25.0
        self.faults = 0
        self.modelo = "IND-26MTO"
        self.manual_sp_cfg = 60.0
        self.manual_time_cfg = 60 * 60   # 1h

    def tick(self, dt: float):
        # Simple thermal lag toward setpoint
        if self.run_state == "RUNNING":
            err = self.setpoint - self.temp
            self.pid_out = max(0.0, min(1.0, 0.7 * err / 5.0))
            self.ssr_drv = self.pid_out
            self.temp += (err / 80.0) * dt + random.uniform(-0.02, 0.02)
            self.humidity = 25.0 + (70.0 - 25.0) * math.exp(-self.session_elapsed_s / (12 * 60))
            self.fan_current = self.fan_nominal * (1.0 + random.uniform(-0.03, 0.03))
            self.session_elapsed_s += dt
        else:
            self.ssr_drv = 0.0
        self.t_min = min(self.t_min, self.temp)
        self.t_max = max(self.t_max, self.temp)


# --------------------------------------------------------------------------
#   Main UI app
# --------------------------------------------------------------------------
SCREENS = (
    "splash", "inicio",
    "prog_manual", "func_manual",
    "prog_programas", "func_programas",
    "tecnica", "alarma",
)


class UISim:
    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("Control SPE32-S3 v3.0 — UI mock (480×320)")
        root.resizable(False, False)

        self.cv = tk.Canvas(root, width=W, height=H, bg=COL_PANEL_BG,
                            highlightthickness=0)
        self.cv.pack()

        self.cv.bind("<Button-1>", self._on_click)
        self.clickable: list[tuple[tuple[int, int, int, int], callable]] = []

        self.state = SimState()
        self.current = "splash"
        self.splash_t0 = time.monotonic()

        self._draw()
        self._tick()

    # ------------------------------------------------------------------
    #   Helpers
    # ------------------------------------------------------------------
    def _clear(self):
        self.cv.delete("all")
        self.clickable.clear()

    def _box(self, x, y, w, h, bg=COL_PANEL_BG, border=COL_GREY, radius=0):
        if radius:
            self._rounded_rect(x, y, x + w, y + h, radius, fill=bg, outline=border, width=2)
        else:
            self.cv.create_rectangle(x, y, x + w, y + h, fill=bg, outline=border, width=2)

    def _rounded_rect(self, x1, y1, x2, y2, r, **kw):
        # Tkinter has no native rounded rectangles — approximate with polygon.
        pts = [
            x1 + r, y1,
            x2 - r, y1,
            x2, y1,
            x2, y1 + r,
            x2, y2 - r,
            x2, y2,
            x2 - r, y2,
            x1 + r, y2,
            x1, y2,
            x1, y2 - r,
            x1, y1 + r,
            x1, y1,
        ]
        return self.cv.create_polygon(pts, smooth=True, **kw)

    def _label(self, x, y, txt, font=F_MD, fg=COL_WHITE, anchor="nw"):
        return self.cv.create_text(x, y, text=txt, fill=fg, font=font, anchor=anchor)

    def _button(self, x, y, w, h, txt, bg, on_click,
                fg=COL_WHITE, font=F_MD, radius=4):
        self._rounded_rect(x, y, x + w, y + h, radius, fill=bg, outline=bg)
        self._label(x + w // 2, y + h // 2, txt, font=font, fg=fg, anchor="center")
        self.clickable.append(((x, y, x + w, y + h), on_click))

    def _on_click(self, ev):
        for (x1, y1, x2, y2), cb in self.clickable:
            if x1 <= ev.x <= x2 and y1 <= ev.y <= y2:
                cb()
                return

    def _show(self, name: str):
        self.current = name
        if name == "splash":
            self.splash_t0 = time.monotonic()
        self._draw()

    # ------------------------------------------------------------------
    #   Left navigation panel
    # ------------------------------------------------------------------
    def _draw_left_panel(self, active: str):
        # White background strip
        self.cv.create_rectangle(0, 0, LEFT_W, H, fill=COL_LEFT_BG, outline=COL_LEFT_BG)

        # Logo placeholder + model
        self.cv.create_oval(39, 8, 81, 50, fill=COL_GREY, outline=COL_GREY)
        self._label(LEFT_W // 2, 56, "MODELO", font=F_XS, fg=COL_GREY, anchor="center")
        self._label(LEFT_W // 2, 70, self.state.modelo, font=F_SM, fg=COL_BLACK, anchor="center")

        rows = [
            ("INICIO",    COL_GREY,   "inicio"),
            ("MANUAL",    COL_ORANGE, "prog_manual"),
            ("PROGRAMAS", COL_GREEN,  "prog_programas"),
            ("TECNICA",   COL_BLUE,   "tecnica"),
            ("ALARMAS",   COL_RED,    "alarma"),
        ]
        top_y = 92
        span = H - top_y - 6
        step = span // len(rows)
        for i, (label, bg, target) in enumerate(rows):
            y = top_y + i * step + (step - 32) // 2
            self._button(16, y, 92, 32, label, bg,
                         lambda t=target: self._show(t),
                         font=F_SM if target == "prog_programas" else F_MD)
            # Green triangle marker on the active row
            mapped_active = self._map_active(active)
            if mapped_active == target:
                self.cv.create_polygon(
                    2, y + 8, 12, y + 16, 2, y + 24,
                    fill=COL_GREEN_DEEP, outline=COL_GREEN_DEEP,
                )

    def _map_active(self, screen: str) -> str:
        # The two "func_*" screens share the indicator with their "prog_*" pair.
        if screen == "func_manual":     return "prog_manual"
        if screen == "func_programas":  return "prog_programas"
        return screen

    # ------------------------------------------------------------------
    #   Screen builders
    # ------------------------------------------------------------------
    def _draw(self):
        self._clear()
        builder = getattr(self, f"_screen_{self.current}", None)
        if builder is None:
            self._label(W // 2, H // 2, f"(missing {self.current})",
                        font=F_LG, fg=COL_RED, anchor="center")
            return
        if self.current != "splash":
            self._draw_left_panel(self.current)
        builder()

    # ---- SPLASH ----
    def _screen_splash(self):
        self.cv.create_rectangle(0, 0, W, H, fill=COL_WHITE, outline=COL_WHITE)
        self._label(W // 2, 50, "LOGO BIO ORIGEN", font=F_XXL, fg=COL_GREY, anchor="center")
        self._label(W // 2, 100, "www.bioorigen.com.ar  info@bioorigen.com.ar",
                    font=F_MD, fg=COL_BLUE, anchor="center")
        self._label(W // 2, 138, "MODELO", font=F_LG, fg=COL_BLACK, anchor="center")
        self._label(W // 2, 172, self.state.modelo, font=F_XXL, fg=COL_ORANGE, anchor="center")

        # Progress bar fills over 5 seconds, then jumps to INICIO.
        pct = min(1.0, (time.monotonic() - self.splash_t0) / 5.0)
        bw, bh, by = 360, 18, H - 50
        bx = (W - bw) // 2
        self._rounded_rect(bx, by, bx + bw, by + bh, 6, fill="#CCCCCC", outline="#CCCCCC")
        fw = int(bw * pct)
        if fw > 0:
            self._rounded_rect(bx, by, bx + fw, by + bh, 6,
                               fill=COL_ORANGE, outline=COL_ORANGE)
        if pct >= 1.0:
            self.root.after(50, lambda: self._show("inicio"))

    # ---- INICIO ----
    def _screen_inicio(self):
        rx = LEFT_W
        self._label(rx + (W - LEFT_W) // 2, 20, "LOGO BIO ORIGEN",
                    font=F_LG, fg=COL_WHITE, anchor="center")
        self._label(rx + (W - LEFT_W) // 2, 52, "MODELO",
                    font=F_MD, fg=COL_GREY, anchor="center")
        self._label(rx + (W - LEFT_W) // 2, 72, self.state.modelo,
                    font=F_XL, fg=COL_ORANGE, anchor="center")
        self._label(rx + (W - LEFT_W) // 2, 130, "MODOS DE OPERACION",
                    font=F_LG, fg=COL_WHITE, anchor="center")

        cx = rx + (W - LEFT_W) // 2
        self._button(cx - 150, 200, 130, 60, "MANUAL", COL_ORANGE,
                     lambda: self._show("prog_manual"), font=F_XL, radius=8)
        self._button(cx + 20,  200, 130, 60, "PROGRAMAS", COL_GREEN,
                     lambda: self._show("prog_programas"), font=F_LG, radius=8)

    # ---- PROG MANUAL ----
    def _screen_prog_manual(self):
        rx = LEFT_W
        self._label(rx + (W - LEFT_W) // 2, 20, "PROGRAMACION \"MODO MANUAL\"",
                    font=F_LG, fg=COL_ORANGE, anchor="center")

        # Temperature row
        self._label(rx + (W - LEFT_W) // 2, 52, "TEMPERATURA °C",
                    font=F_MD, fg=COL_GREY, anchor="center")
        cx = rx + (W - LEFT_W) // 2
        self._button(cx - 130, 76, 44, 44, "-", COL_TIME_BLUE,
                     lambda: self._spin("temp", -1), font=F_XXL, radius=6)
        self._label(cx, 78, f"{self.state.manual_sp_cfg:.1f}",
                    font=F_HUGE, fg=COL_TEMP_RED, anchor="center")
        self._button(cx + 86, 76, 44, 44, "+", COL_RED,
                     lambda: self._spin("temp", +1), font=F_XXL, radius=6)

        # Time row
        self._label(rx + (W - LEFT_W) // 2, 156, "TIEMPO HH:MM",
                    font=F_MD, fg=COL_GREY, anchor="center")
        self._button(cx - 130, 176, 44, 44, "-", COL_GREEN,
                     lambda: self._spin("time", -1), font=F_XXL, radius=6)
        h = self.state.manual_time_cfg // 3600
        m = (self.state.manual_time_cfg // 60) % 60
        self._label(cx, 180, f"{h:02d}:{m:02d}",
                    font=F_HUGE, fg=COL_TEMP_RED, anchor="center")
        self._button(cx + 86, 176, 44, 44, "+", COL_GREEN,
                     lambda: self._spin("time", +1), font=F_XXL, radius=6)

        # INICIAR
        self._button(cx - 130, H - 50, 260, 40, "INICIAR", COL_ORANGE,
                     self._iniciar_manual, font=F_XL, radius=8)

    def _spin(self, kind: str, dir_: int):
        if kind == "temp":
            self.state.manual_sp_cfg = max(20.0, min(80.0,
                self.state.manual_sp_cfg + dir_ * 0.5))
        else:
            self.state.manual_time_cfg = max(60, min(99 * 3600 + 59 * 60,
                self.state.manual_time_cfg + dir_ * 5 * 60))
        self._draw()

    def _iniciar_manual(self):
        self.state.setpoint = self.state.manual_sp_cfg
        self.state.session_total_s = self.state.manual_time_cfg
        self.state.session_elapsed_s = 0
        self.state.temp = 25.0
        self.state.t_min = 25.0
        self.state.t_max = 25.0
        self.state.run_state = "RUNNING"
        self.state.nombre_programa = "MANUAL"
        self._show("func_manual")

    # ---- FUNC MANUAL ----
    def _screen_func_manual(self):
        rx = LEFT_W
        s = self.state

        # Row 1: SET POINT / T. SESION / HUMEDAD / PID OUT
        self._mini_box(rx + 4,   4,  84, 36, "SET POINT",   f"{s.setpoint:.1f}",
                       COL_ORANGE, COL_ORANGE)
        self._mini_box(rx + 92,  4,  84, 36, "T. SESION",   f"{int(s.session_elapsed_s)}s",
                       COL_WHITE,  COL_GREY, fg=COL_BLACK)
        self._mini_box(rx + 180, 4,  84, 36, "HUMEDAD",     f"{s.humidity:.1f}%",
                       COL_GREEN,  COL_YELLOW)
        self._mini_box(rx + 268, 4,  84, 36, "PID OUT",     f"{s.pid_out*100:.0f}%",
                       COL_ORANGE, COL_YELLOW)

        # Row 2
        self._mini_box(rx + 4,   44, 84, 36, "RESISTENCIAS",
                       "ON" if s.ssr_drv > 0.05 else "OFF",
                       COL_PANEL_BG, COL_GREEN,
                       fg=COL_GREEN if s.ssr_drv > 0.05 else COL_RED)
        self._mini_box(rx + 92,  44, 84, 36, "TURBINAS",  f"{s.fan_current:.2f}A",
                       COL_PANEL_BG, COL_GREEN, fg=COL_GREEN)
        self._mini_box(rx + 180, 44, 84, 36, "T MIN", f"{s.t_min:.1f}",
                       COL_ORANGE, COL_YELLOW)
        self._mini_box(rx + 268, 44, 84, 36, "T MAX", f"{s.t_max:.1f}",
                       COL_ORANGE, COL_YELLOW)

        # Big values
        self._label(rx + 12, 90, f"{s.temp:.1f}", font=F_HUGE,
                    fg=COL_TEMP_RED, anchor="nw")
        remaining = max(0, s.session_total_s - int(s.session_elapsed_s))
        rh = remaining // 3600
        rm = (remaining // 60) % 60
        rs = remaining % 60
        self._label(W - 12, 90, f"{rh:02d}:{rm:02d}:{rs:02d}",
                    font=F_HUGE, fg=COL_TIME_BLUE, anchor="ne")

        # Bars
        self._bar(rx + 12, 158, 340, 12, min(100, int(s.temp)), COL_ORANGE)
        pct = 0 if s.session_total_s == 0 else \
              int(s.session_elapsed_s * 100 / s.session_total_s)
        self._bar(rx + 12, 178, 340, 12, pct, COL_CYAN)

        # State + buttons
        if s.run_state == "PAUSED":
            txt, col = "PAUSADO", COL_YELLOW
        elif s.run_state == "COMPLETED":
            txt, col = "COMPLETADO", COL_GREEN
        else:
            txt, col = "FUNCIONANDO EN MANUAL", COL_GREEN
        self._label(rx + 12, 200, txt, font=F_MD, fg=col)

        self._button(rx + 8,  H - 44, 110, 36, "PAUSAR",
                     COL_GREY if s.run_state != "RUNNING" else COL_ORANGE,
                     lambda: self._set_state("PAUSED"), font=F_MD, radius=4)
        self._button(rx + 122, H - 44, 110, 36, "REINICIAR",
                     COL_RED, lambda: self._set_state("RUNNING"), font=F_MD, radius=4)
        self._button(rx + 236, H - 44, 110, 36, "DETENER",
                     COL_RED, lambda: self._show("prog_manual"), font=F_MD, radius=4)

    def _set_state(self, st: str):
        self.state.run_state = st
        self._draw()

    def _mini_box(self, x, y, w, h, cap, val, bg, border,
                  fg=COL_WHITE, cap_fg=COL_GREY):
        self._rounded_rect(x, y, x + w, y + h, 6, fill=bg, outline=border)
        self._label(x + 4, y + 2, cap, font=F_XS, fg=cap_fg)
        self._label(x + w // 2, y + h - 4, val, font=F_MD, fg=fg, anchor="s")

    def _bar(self, x, y, w, h, pct, col):
        self.cv.create_rectangle(x, y, x + w, y + h, fill=COL_GREY, outline=COL_GREY)
        fw = max(0, int(w * pct / 100))
        if fw > 0:
            self.cv.create_rectangle(x, y, x + fw, y + h, fill=col, outline=col)

    # ---- PROG PROGRAMAS ----
    def _screen_prog_programas(self):
        rx = LEFT_W
        self._label(rx + (W - LEFT_W) // 2, 14,
                    "PROGRAMACION \"MODO PROGRAMAS\"",
                    font=F_MD, fg=COL_GREEN, anchor="center")
        # 3 stages, very simplified — display only
        for i, (sp, dur) in enumerate(self.state.etapas):
            y = 36 + i * 40
            self._label(rx + 8, y, f"ETAPA {i+1}", font=F_SM, fg=COL_YELLOW)
            self._label(rx + 70, y, f"{sp:.1f}°C",
                        font=F_LG, fg=COL_TEMP_RED, anchor="nw")
            h = dur // 3600
            m = (dur // 60) % 60
            self._label(rx + 200, y, f"{h:02d}:{m:02d}",
                        font=F_LG, fg=COL_TEMP_RED, anchor="nw")
            if i < 2:
                self.cv.create_rectangle(rx + 4, y + 26, rx + 348, y + 28,
                                         fill=COL_YELLOW, outline=COL_YELLOW)

        self._label(rx + 8, 168, "NOMBRE:", font=F_SM, fg=COL_GREY)
        self._rounded_rect(rx + 60, 162, rx + 280, 188, 4,
                           fill=COL_WHITE, outline=COL_GREY)
        self._label(rx + 64, 167, self.state.nombre_programa, font=F_MD, fg=COL_BLACK)

        self._button(rx + 12, H - 46, 140, 36, "GRABAR", COL_RED,
                     lambda: None, font=F_LG, radius=4)
        self._button(rx + 210, H - 46, 140, 36, "INICIAR", COL_ORANGE,
                     self._iniciar_programas, font=F_LG, radius=4)

    def _iniciar_programas(self):
        self.state.setpoint = self.state.etapas[0][0]
        self.state.session_total_s = sum(d for _, d in self.state.etapas)
        self.state.session_elapsed_s = 0
        self.state.etapa_activa = 0
        self.state.temp = 25.0
        self.state.t_min = 25.0
        self.state.t_max = 25.0
        self.state.run_state = "RUNNING"
        self._show("func_programas")

    # ---- FUNC PROGRAMAS ----
    def _screen_func_programas(self):
        rx = LEFT_W
        s = self.state

        # Green top banner
        self.cv.create_rectangle(rx, 0, W, 22, fill=COL_GREEN, outline=COL_GREEN)
        self._label(rx + (W - LEFT_W) // 2, 11, f"PROGRAMA: {s.nombre_programa}",
                    font=F_MD, fg=COL_WHITE, anchor="center")

        for i, (sp, dur) in enumerate(s.etapas):
            x = rx + 4 + i * 112
            bg = COL_ORANGE if (i == s.etapa_activa and s.run_state == "RUNNING") else COL_GREY
            self._rounded_rect(x, 28, x + 110, 60, 6, fill=bg, outline=COL_YELLOW)
            h = dur // 3600
            m = (dur // 60) % 60
            self._label(x + 55, 44, f"E{i+1} {sp:.1f}°C {h:02d}:{m:02d}",
                        font=F_SM, fg=COL_WHITE, anchor="center")

        # Humidity + PID OUT on the right
        self._mini_box(W - 156, 28, 70, 32, "HUM",
                       f"{s.humidity:.0f}%", COL_GREEN, COL_YELLOW)
        self._mini_box(W -  82, 28, 78, 32, "PID",
                       f"{s.pid_out*100:.0f}%", COL_ORANGE, COL_YELLOW)

        # Big values
        self._label(rx + 12, 74, f"{s.temp:.1f}", font=F_HUGE,
                    fg=COL_TEMP_RED, anchor="nw")
        remaining = max(0, s.session_total_s - int(s.session_elapsed_s))
        rh = remaining // 3600
        rm = (remaining // 60) % 60
        rs = remaining % 60
        self._label(W - 12, 74, f"{rh:02d}:{rm:02d}:{rs:02d}",
                    font=F_HUGE, fg=COL_TIME_BLUE, anchor="ne")

        self._bar(rx + 12, 142, 340, 12, min(100, int(s.temp)), COL_ORANGE)
        pct = 0 if s.session_total_s == 0 else \
              int(s.session_elapsed_s * 100 / s.session_total_s)
        self._bar(rx + 12, 162, 340, 12, pct, COL_CYAN)

        if s.run_state == "PAUSED":
            txt, col = "PAUSADO", COL_YELLOW
        elif s.run_state == "COMPLETED":
            txt, col = "COMPLETADO", COL_GREEN
        else:
            txt, col = "FUNCIONANDO EN PROGRAMAS", COL_GREEN
        self._label(rx + 12, 186, txt, font=F_MD, fg=col)

        self._button(rx + 8,   H - 44, 110, 36, "PAUSAR",
                     COL_GREY if s.run_state != "RUNNING" else COL_ORANGE,
                     lambda: self._set_state("PAUSED"), font=F_MD, radius=4)
        self._button(rx + 122, H - 44, 110, 36, "REINICIAR",
                     COL_RED, lambda: self._set_state("RUNNING"), font=F_MD, radius=4)
        self._button(rx + 236, H - 44, 110, 36, "DETENER",
                     COL_RED, lambda: self._show("prog_programas"),
                     font=F_MD, radius=4)

    # ---- TECNICA ----
    def _screen_tecnica(self):
        rx = LEFT_W
        self._label(rx + (W - LEFT_W) // 2, 14, "AREA TECNICA",
                    font=F_LG, fg=COL_BLUE, anchor="center")

        boxes_top = [
            ("HRS TOTAL",  "12h",   COL_CYAN),
            ("CICLOS SSR", "345",   COL_CYAN),
            ("DESDE SVC",  "82h",   COL_YELLOW),
            ("FALLA FAN",  "0",     COL_RED),
        ]
        boxes_bot = [
            ("Kp / Ki",     "8.0/0.15", COL_GREEN),
            ("I NOM",       "0.45 A",   COL_GREEN),
            ("T MAX HIST",  "78.2 C",   COL_ORANGE),
            ("SESIONES",    "7",        COL_CYAN),
        ]
        for i, (cap, val, border) in enumerate(boxes_top):
            self._mini_box(rx + 4 + i * 88, 30, 84, 34, cap, val,
                           COL_PANEL_BG, border, fg=COL_CYAN)
        for i, (cap, val, border) in enumerate(boxes_bot):
            self._mini_box(rx + 4 + i * 88, 70, 84, 34, cap, val,
                           COL_PANEL_BG, border, fg=COL_CYAN)

        events = [
            ("+ 0s   boot",                COL_GREEN),
            ("+ 12s  session #7",          COL_GREEN),
            ("! 145s fan fault (demo)",    COL_RED),
        ]
        for i, (msg, col) in enumerate(events):
            self._label(rx + 6, 110 + i * 14, msg, font=F_SM, fg=col)

        self._label(rx + 6, 160, "MODELO:", font=F_SM, fg=COL_GREY)
        self._rounded_rect(rx + 60, 156, rx + 190, 182, 4,
                           fill=COL_WHITE, outline=COL_GREY)
        self._label(rx + 64, 161, self.state.modelo, font=F_SM, fg=COL_BLACK)
        self._button(rx + 196, 156, 80, 26, "GUARDAR", COL_GREEN,
                     lambda: None, font=F_XS, radius=3)

        acts = [
            ("AUTOTUNE",    COL_BLUE),
            ("CALIBRAR",    COL_ORANGE),
            ("RESET SVC",   COL_GREEN),
            ("CAMBIAR PIN", COL_RED),
        ]
        for i, (txt, col) in enumerate(acts):
            self._button(rx + 4 + i * 90, H - 40, 84, 30, txt, col,
                         lambda: None, font=F_XS, radius=4)

    # ---- ALARMA ----
    def _screen_alarma(self):
        rx = LEFT_W
        self._label(rx + (W - LEFT_W) // 2, 14,
                    "⚠  ALARMA ACTIVA", font=F_XL, fg=COL_RED, anchor="center")
        self._label(rx + (W - LEFT_W) // 2, 56, "FALLA DE TURBINA",
                    font=F_XXL, fg=COL_RED, anchor="center")
        self._label(rx + (W - LEFT_W) // 2, 96, f"UPTIME {int(time.monotonic()-self.state.t_start)}s",
                    font=F_MD, fg=COL_WHITE, anchor="center")

        self._rounded_rect(rx + 8, 124, rx + 348, 162, 6,
                           fill=COL_RED, outline=COL_YELLOW)
        self._label(rx + (W - LEFT_W) // 2, 143,
                    f"I = 0.180 A   /   NOM {self.state.fan_nominal:.3f} A",
                    font=F_LG, fg=COL_WHITE, anchor="center")

        self._rounded_rect(rx + 8, 168, rx + 348, 196, 6,
                           fill=COL_GREY, outline=COL_GREY)
        self._label(rx + (W - LEFT_W) // 2, 182,
                    "RESISTENCIAS Y TURBINAS DESACTIVADAS",
                    font=F_SM, fg=COL_WHITE, anchor="center")

        self._label(rx + 12, 210, f"TEMP {self.state.temp:.1f} C",
                    font=F_MD, fg=COL_TEMP_RED)
        self._label(W - 12, 210, f"SESION {int(self.state.session_elapsed_s)}s",
                    font=F_MD, fg=COL_TIME_BLUE, anchor="ne")

        self._button(rx + (W - LEFT_W) // 2 - 140, H - 48, 280, 40,
                     "CONFIRMAR Y REINICIAR EQUIPO", COL_ORANGE,
                     lambda: self._show("inicio"), font=F_MD, radius=6)

    # ------------------------------------------------------------------
    #   Main loop
    # ------------------------------------------------------------------
    def _tick(self):
        # Advance simulation only when a "func" / splash screen is active —
        # the others are static.
        if self.current == "splash":
            self._draw()
        elif self.current in ("func_manual", "func_programas"):
            self.state.tick(0.4)
            self._draw()
        self.root.after(400, self._tick)


def main():
    root = tk.Tk()
    UISim(root)
    root.mainloop()


if __name__ == "__main__":
    main()
