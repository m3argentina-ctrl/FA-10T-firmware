#!/usr/bin/env python3
# FA-10T v3.0 — Standalone PC simulator
#
# Mimics the firmware's control loop + drivers on a Windows/Linux PC so the
# operator can validate behavior end-to-end without an ESP32. The output panel
# matches the format the firmware prints over serial.
#
# Usage:
#   python tools/simulator.py                       # 10 min @ 60 C setpoint
#   python tools/simulator.py --sp 70 --duration 1800
#   python tools/simulator.py --fast 4              # 4× real time
#
# No external deps — runs on any Python 3.8+.

from __future__ import annotations

import argparse
import math
import random
import re
import signal
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path


# --------------------------------------------------------------------------
#   Read constants from app_config.h, fall back to hardcoded defaults.
# --------------------------------------------------------------------------

DEFAULTS = {
    "OPERATING_TEMP_MIN_C": 20.0,
    "OPERATING_TEMP_MAX_C": 80.0,
    "SAFETY_TEMP_MAX_C":    85.0,
    "SAFETY_RUNAWAY_DT_C":   5.0,
    "SAFETY_RUNAWAY_WINDOW_S": 60.0,
    "SAFETY_RUNAWAY_DUTY_THR": 0.05,
    "SSR_PERIOD_MS":        1000,
    "ACS712_SENS_V_PER_A":   0.185,
    "ACS712_FAULT_RATIO":    0.70,
    "PT1000_R0_OHMS":     1000.0,
    "PT1000_CVD_A":     3.9083e-3,
    "PT1000_CVD_B":    -5.775e-7,
}

NUMERIC_RE = re.compile(
    r"^\s*#\s*define\s+([A-Z0-9_]+)\s+"
    r"([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)[fF]?\b"
)


def load_app_config(path: Path) -> dict:
    cfg = dict(DEFAULTS)
    if not path.exists():
        print(f"[warn] {path} not found — using built-in defaults", file=sys.stderr)
        return cfg
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        m = NUMERIC_RE.match(line)
        if not m:
            continue
        name, val = m.group(1), m.group(2)
        if name in cfg:
            try:
                cfg[name] = float(val) if "." in val or "e" in val.lower() else int(val)
            except ValueError:
                pass
    return cfg


# --------------------------------------------------------------------------
#   PID (matches components/pid_controller/pid_controller.c semantics)
# --------------------------------------------------------------------------

@dataclass
class PIDParams:
    kp: float = 8.0
    ki: float = 0.15
    kd: float = 40.0
    out_min: float = 0.0
    out_max: float = 1.0
    d_filter_alpha: float = 0.85
    derivative_on_measurement: bool = True
    kt: float = 0.15          # back-calculation gain


class PID:
    def __init__(self, p: PIDParams):
        self.p = p
        self.setpoint = 0.0
        self.integral = 0.0
        self.prev_input = 0.0
        self.prev_error = 0.0
        self.prev_d_filtered = 0.0
        self.first_run = True

    def reset(self):
        self.integral = 0.0
        self.prev_d_filtered = 0.0
        self.first_run = True

    def compute(self, measurement: float, dt: float) -> float:
        err = self.setpoint - measurement

        if self.first_run:
            self.prev_input = measurement
            self.prev_error = err
            self.first_run = False

        # Proportional
        p_term = self.p.kp * err

        # Integral (provisional)
        self.integral += self.p.ki * err * dt

        # Derivative on measurement (or on error) with low-pass
        if self.p.derivative_on_measurement:
            d_raw = -self.p.kd * (measurement - self.prev_input) / max(dt, 1e-6)
        else:
            d_raw = self.p.kd * (err - self.prev_error) / max(dt, 1e-6)
        d_filtered = (
            self.p.d_filter_alpha * self.prev_d_filtered
            + (1.0 - self.p.d_filter_alpha) * d_raw
        )

        unclamped = p_term + self.integral + d_filtered
        clamped = min(max(unclamped, self.p.out_min), self.p.out_max)

        # Back-calculation anti-windup
        if self.p.kt > 0.0:
            self.integral += self.p.kt * (clamped - unclamped) * dt

        self.prev_input = measurement
        self.prev_error = err
        self.prev_d_filtered = d_filtered
        return clamped


# --------------------------------------------------------------------------
#   Plant models
# --------------------------------------------------------------------------

@dataclass
class ThermalPlant:
    """First-order thermal model.

    dT/dt = (heater_gain * duty - (T - T_ambient) / tau) + noise
    """
    temp_c: float = 25.0
    ambient_c: float = 25.0
    tau_s: float = 120.0            # thermal time constant
    heater_gain_c_per_s: float = 1.0  # at duty = 1, raw heating rate
    noise_pp: float = 0.05

    def step(self, dt: float, duty: float) -> float:
        cooling = (self.temp_c - self.ambient_c) / self.tau_s
        heating = self.heater_gain_c_per_s * duty
        self.temp_c += (heating - cooling) * dt
        self.temp_c += random.uniform(-self.noise_pp, self.noise_pp) * 0.1
        return self.temp_c


@dataclass
class HumidityPlant:
    """Drying curve: RH = 25 + 45*exp(-t/tau). Active only while heating."""
    rh: float = 70.0
    rh_floor: float = 25.0
    rh_ceil: float = 70.0
    tau_min: float = 12.0
    t_running_s: float = 0.0

    def step(self, dt: float, running: bool):
        if running:
            self.t_running_s += dt
        # Asymptotic approach toward floor while drying.
        decay = math.exp(-(self.t_running_s / 60.0) / self.tau_min)
        self.rh = self.rh_floor + (self.rh_ceil - self.rh_floor) * decay
        self.rh += random.uniform(-0.3, 0.3)
        return max(0.0, min(100.0, self.rh))


@dataclass
class FanCurrent:
    """ACS712 surrogate: A_rms = nominal × (1 + noise) when fan is ON."""
    nominal_a: float = 0.45
    noise_frac: float = 0.03

    def read(self, fan_on: bool) -> float:
        if not fan_on:
            return 0.0
        return self.nominal_a * (1.0 + random.uniform(-self.noise_frac, self.noise_frac))


# --------------------------------------------------------------------------
#   3-channel SSR (time-proportional, 1 s period)
# --------------------------------------------------------------------------

@dataclass
class SSR3:
    period_s: float = 1.0
    duty: list[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    state_on: list[bool] = field(default_factory=lambda: [False, False, False])
    cycles: list[int] = field(default_factory=lambda: [0, 0, 0])
    _t_in_period: float = 0.0

    def tick(self, dt: float):
        self._t_in_period += dt
        if self._t_in_period >= self.period_s:
            self._t_in_period -= self.period_s
        for i, d in enumerate(self.duty):
            on_time = d * self.period_s
            new_on = (d >= 0.999) or (d > 0.0 and self._t_in_period < on_time)
            if new_on and not self.state_on[i]:
                self.cycles[i] += 1
            self.state_on[i] = new_on


# --------------------------------------------------------------------------
#   Safety (matching main/safety.c subset)
# --------------------------------------------------------------------------

OVERTEMP        = 1 << 0
SENSOR_FAULT    = 1 << 1
RUNAWAY         = 1 << 2
WDT_TIMEOUT     = 1 << 3
FAN_FAULT       = 1 << 4
WARN_NEAR_LIMIT = 1 << 8
TRIP_MASK = OVERTEMP | SENSOR_FAULT | RUNAWAY | WDT_TIMEOUT | FAN_FAULT


class Safety:
    def __init__(self, cfg):
        self.cfg = cfg
        self.faults = 0
        self.latched = 0
        self.runaway_t0 = None
        self.runaway_start_temp = 0.0

    def evaluate(self, temperature: float, duty: float, dt: float,
                 sensor_fault: bool, fan_fault: bool) -> int:
        self.faults = 0
        if sensor_fault:
            self.faults |= SENSOR_FAULT
        if fan_fault:
            self.faults |= FAN_FAULT
            self.latched |= FAN_FAULT
        if not sensor_fault and temperature > self.cfg["SAFETY_TEMP_MAX_C"]:
            self.faults |= OVERTEMP
            self.latched |= OVERTEMP
        elif not sensor_fault and temperature >= (
            self.cfg["SAFETY_TEMP_MAX_C"] - 5.0
        ):
            self.faults |= WARN_NEAR_LIMIT

        # Runaway: temp rising with SSR essentially off
        if not sensor_fault and duty <= self.cfg["SAFETY_RUNAWAY_DUTY_THR"]:
            if self.runaway_t0 is None:
                self.runaway_t0 = 0.0
                self.runaway_start_temp = temperature
            else:
                self.runaway_t0 += dt
                if temperature - self.runaway_start_temp > self.cfg["SAFETY_RUNAWAY_DT_C"]:
                    self.faults |= RUNAWAY
                    self.latched |= RUNAWAY
                if self.runaway_t0 >= self.cfg["SAFETY_RUNAWAY_WINDOW_S"]:
                    self.runaway_t0 = 0.0
                    self.runaway_start_temp = temperature
        else:
            self.runaway_t0 = None

        return self.faults | self.latched


# --------------------------------------------------------------------------
#   Dashboard rendering
# --------------------------------------------------------------------------

def fmt_hms(secs: int) -> str:
    h, rem = divmod(int(secs), 3600)
    m, s = divmod(rem, 60)
    return f"{h:02d}:{m:02d}:{s:02d}"


def state_label(running: bool, faults: int, completed: bool) -> str:
    if faults & TRIP_MASK:
        return "TRIPPED"
    if completed:
        return "COMPLETED"
    if running:
        return "RUNNING"
    return "IDLE"


def fault_names(mask: int) -> str:
    pairs = [
        (OVERTEMP, "OVERTEMP"),
        (SENSOR_FAULT, "SENSOR"),
        (RUNAWAY, "RUNAWAY"),
        (WDT_TIMEOUT, "WDT"),
        (FAN_FAULT, "FAN"),
        (WARN_NEAR_LIMIT, "warn:near-limit"),
    ]
    return " ".join(n for b, n in pairs if mask & b) or "none"


def render(sim: "Sim", uptime_s: float, dt_print: float):
    s = sim
    t      = s.thermal.temp_c
    sp     = s.setpoint
    drv    = s.ssr.duty[0] * 100
    fan    = s.ssr.duty[1] * 100
    aux    = s.ssr.duty[2] * 100
    pid    = s.last_pid_out * 100
    rh     = s.last_rh
    i_fan  = s.last_fan_a
    state  = state_label(s.running, s.faults, s.completed)
    fan_ok = "OK" if not s.fan_fault else "FAULT"
    nom    = s.fan.nominal_a

    bar = "-" * 64
    print()
    print(bar)
    print(f"  FA-10T v3.0 | UP {fmt_hms(uptime_s)} | {state}")
    print(f"  TEMP: {t:5.1f} C | SP: {sp:5.1f} C | dT: {t-sp:+5.1f} C")
    print(f"  PID OUT: {pid:5.1f}% | SSR DRV: {drv:5.1f}% | SSR FAN: {fan:5.1f}% | SSR AUX: {aux:5.1f}%")
    print(f"  HUMIDITY: {rh:4.1f}% | FAN: {i_fan:.3f}A {fan_ok} (nom={nom:.3f}A)")
    if s.faults & TRIP_MASK:
        print(f"  *** FAULTS: {fault_names(s.faults)} ***")
    elif s.faults & WARN_NEAR_LIMIT:
        print(f"  warnings: {fault_names(s.faults & WARN_NEAR_LIMIT)}")
    cyc = s.ssr.cycles
    print(f"  cycles: drv={cyc[0]} fan={cyc[1]} aux={cyc[2]} | sim {sim.speed:.1f}x")
    print(bar)
    sys.stdout.flush()


# --------------------------------------------------------------------------
#   Simulation driver
# --------------------------------------------------------------------------

@dataclass
class Sim:
    cfg: dict
    setpoint: float
    duration_s: float
    speed: float = 1.0

    def __post_init__(self):
        self.pid = PID(PIDParams())
        self.pid.setpoint = self.setpoint
        self.thermal = ThermalPlant()
        self.humidity = HumidityPlant()
        self.fan = FanCurrent()
        self.ssr = SSR3()
        self.safety = Safety(self.cfg)
        self.elapsed_s = 0.0
        self.last_pid_out = 0.0
        self.last_rh = self.humidity.rh
        self.last_fan_a = 0.0
        self.fan_fault = False
        self.faults = 0
        self.running = True
        self.completed = False

    def step(self, dt: float):
        if self.completed or (self.faults & TRIP_MASK):
            self.ssr.duty = [0.0, 0.0, 0.0]
            self.ssr.tick(dt)
            self.last_pid_out = 0.0
            self.last_fan_a = 0.0
            self.running = False
            return

        # PID computes heater duty
        duty = self.pid.compute(self.thermal.temp_c, dt)
        self.last_pid_out = duty

        # Drive SSRs
        self.ssr.duty[0] = duty
        self.ssr.duty[1] = 1.0   # fan ON while running
        self.ssr.duty[2] = 0.0
        self.ssr.tick(dt)

        # Plant updates
        self.thermal.step(dt, duty)
        self.last_rh = self.humidity.step(dt, running=True)
        self.last_fan_a = self.fan.read(fan_on=True)

        # Fan fault detection
        if self.fan.nominal_a > 0.05:
            self.fan_fault = self.last_fan_a < self.cfg["ACS712_FAULT_RATIO"] * self.fan.nominal_a

        # Safety
        self.faults = self.safety.evaluate(
            self.thermal.temp_c, duty, dt,
            sensor_fault=False,
            fan_fault=self.fan_fault,
        )

        # Completion
        self.elapsed_s += dt
        if self.elapsed_s >= self.duration_s:
            self.completed = True
            self.running = False


# --------------------------------------------------------------------------
#   Main
# --------------------------------------------------------------------------

def parse_args():
    p = argparse.ArgumentParser(
        description="FA-10T v3.0 standalone PC simulator",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--sp", "--setpoint", type=float, default=60.0,
                   dest="setpoint", help="setpoint °C")
    p.add_argument("--duration", type=float, default=600.0,
                   help="session duration (seconds)")
    p.add_argument("--dt", type=float, default=0.1,
                   help="simulation step (seconds)")
    p.add_argument("--print-interval", type=float, default=2.0,
                   help="dashboard refresh interval (seconds)")
    p.add_argument("--fast", type=float, default=1.0,
                   help="real-time multiplier (1.0 = real time, 10 = 10x)")
    p.add_argument("--config", type=Path,
                   default=Path(__file__).resolve().parent.parent / "main" / "app_config.h",
                   help="path to app_config.h")
    return p.parse_args()


def main():
    args = parse_args()
    cfg = load_app_config(args.config)

    if not (cfg["OPERATING_TEMP_MIN_C"] <= args.setpoint <= cfg["OPERATING_TEMP_MAX_C"]):
        print(f"[warn] setpoint {args.setpoint} outside operating envelope "
              f"[{cfg['OPERATING_TEMP_MIN_C']}, {cfg['OPERATING_TEMP_MAX_C']}] °C",
              file=sys.stderr)

    sim = Sim(cfg=cfg, setpoint=args.setpoint,
              duration_s=args.duration, speed=args.fast)

    print(f"FA-10T simulator | SP={args.setpoint:.1f} C | duration={args.duration:.0f}s | "
          f"speed={args.fast:.1f}x | dt={args.dt:.2f}s")
    print(f"config loaded from: {args.config}")

    stop = {"flag": False}
    def on_signal(signum, frame):
        stop["flag"] = True
    signal.signal(signal.SIGINT, on_signal)
    try:
        signal.signal(signal.SIGTERM, on_signal)
    except (AttributeError, ValueError):
        pass

    t_start_wall = time.monotonic()
    next_print_sim = 0.0
    while not stop["flag"]:
        sim.step(args.dt)
        if sim.elapsed_s >= next_print_sim:
            render(sim, sim.elapsed_s, args.print_interval)
            next_print_sim += args.print_interval

        if sim.completed or (sim.faults & TRIP_MASK):
            # Run a few extra prints, then exit gracefully.
            if sim.elapsed_s >= next_print_sim + args.print_interval * 1.5:
                break

        if args.fast > 0:
            target_wall = t_start_wall + sim.elapsed_s / args.fast
            now = time.monotonic()
            sleep_s = target_wall - now
            if sleep_s > 0:
                time.sleep(min(sleep_s, args.dt))

    print("\nsimulator stopped.")
    if sim.completed:
        print(f"  session completed in {fmt_hms(sim.elapsed_s)}")
    if sim.faults & TRIP_MASK:
        print(f"  ended on FAULT: {fault_names(sim.faults)}")


if __name__ == "__main__":
    main()
