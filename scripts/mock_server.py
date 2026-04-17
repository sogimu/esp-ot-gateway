#!/usr/bin/env python3
"""
mock_server.py — эмулятор STM32 OpenTherm контроллера для Baxi duo-tec compact.

Запуск:
    python3 scripts/mock_server.py
    # → открыть http://localhost:8080

Эмулирует поведение котла:
  - Температура подачи растёт/падает в зависимости от уставки
  - Горелка включается если temp < setpoint - 3°C
  - Давление слегка флуктуирует
  - Модуляция зависит от разницы температур
"""

import json
import math
import random
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

# ─── Извлечь HTML из web_page.h ──────────────────────────────────────────────

def load_html():
    try:
        with open("main/web_page.h", "r", encoding="utf-8") as f:
            src = f.read()
        # Убрать C-строковые кавычки и конкатенацию
        lines = []
        for line in src.splitlines():
            m = re.match(r'\s*"(.*)"[;]?\s*$', line)
            if m:
                # Раскрыть C escape-последовательности
                text = m.group(1)
                text = text.replace("\\n", "\n").replace("\\'", "'")
                lines.append(text)
        if lines:
            print("[✓] HTML загружен из src/web_page.h")
            return "".join(lines)
    except FileNotFoundError:
        pass

    print("[!] src/web_page.h не найден, используется встроенная страница")
    return FALLBACK_HTML


FALLBACK_HTML = """<!DOCTYPE html>
<html lang="ru"><head><meta charset="UTF-8">
<title>Baxi mock</title></head><body>
<h2>Mock сервер работает</h2>
<p>Запустите из корня проекта: <code>python3 scripts/mock_server.py</code></p>
<p>API: <a href="/api/status">/api/status</a></p>
</body></html>"""

# ─── Состояние котла ──────────────────────────────────────────────────────────

state = {
    # Управление
    "ch_enable":      True,
    "dhw_enable":     True,
    "ch_setpoint":    30.0,
    "dhw_setpoint":   60.0,   # уставка бойлера КН
    "dhw_sp_min":     40.0,   # границы уставки (читаются из котла, ID=48)
    "dhw_sp_max":     80.0,
    # Динамические (эмулируются)
    "ch_temp":        45.0,
    "return_temp":    38.0,
    "dhw_temp":       50.0,   # температура в баке БКН
    "outside_temp":    5.0,
    "pressure":        1.5,
    "modulation":      0.0,
    "flame":          False,
    "ch_active":      False,
    "dhw_active":     False,  # True = 3-ход. клапан → бойлер КН
    "fault":          False,
    "connected":      True,
}

# Гистерезис переключения клапана (котёл не переключает клапан слишком часто)
DHW_HYST_ON  = 5.0   # включить нагрев БКН если temp ниже setpoint на 5°C
DHW_HYST_OFF = 1.0   # выключить нагрев БКН если temp выше setpoint на 1°C

START_TIME = time.time()

def simulate():
    """
    Эмуляция Baxi Duo-tec Compact 1.24 с бойлером косвенного нагрева.

    Логика 3-ходового клапана:
      - Котёл имеет приоритет нагрева БКН над CH
      - При dhw_enable=True и t_бойлера < setpoint - HYST_ON → клапан на бойлер
      - При t_бойлера > setpoint + HYST_OFF → клапан обратно на отопление
      - Пока клапан на бойлере: ch_active=True (насос работает), но
        теплоноситель идёт в змеевик бойлера, а не в радиаторы
    """
    t = time.time() - START_TIME

    # ── 1. Логика 3-ходового клапана ────────────────────────────────────────
    if state["dhw_enable"]:
        dhw_diff = state["dhw_setpoint"] - state["dhw_temp"]
        if not state["dhw_active"] and dhw_diff >= DHW_HYST_ON:
            # Температура упала ниже уставки — переключить клапан на БКН
            state["dhw_active"] = True
            print(f"    [клапан] → Бойлер КН  (t={state['dhw_temp']:.1f}°C, уставка={state['dhw_setpoint']:.0f}°C)")
        elif state["dhw_active"] and state["dhw_temp"] >= state["dhw_setpoint"] + DHW_HYST_OFF:
            # Бойлер прогрет — вернуть клапан на отопление
            state["dhw_active"] = False
            print(f"    [клапан] → Отопление  (t={state['dhw_temp']:.1f}°C, уставка={state['dhw_setpoint']:.0f}°C)")
    else:
        state["dhw_active"] = False

    # ── 2. Горелка и нагрев ─────────────────────────────────────────────────
    if state["dhw_active"]:
        # Клапан на БКН: горелка греет бойлер
        state["flame"]     = True
        state["ch_active"] = True   # насос работает (теплоноситель → змеевик)
        bkn_diff = state["dhw_setpoint"] - state["dhw_temp"]
        state["modulation"] = min(100.0, max(20.0, bkn_diff * 10.0 + random.uniform(-2, 2)))
        # Температура подачи — высокая (котёл греет на максимум)
        state["ch_temp"] = min(80.0, state["ch_temp"] + 0.6 + random.uniform(-0.1, 0.1))
        # Бойлер нагревается медленнее (тепловая инерция бака)
        heat = state["modulation"] / 100.0 * 0.15
        state["dhw_temp"] = min(state["dhw_setpoint"] + DHW_HYST_OFF + 0.5,
                                state["dhw_temp"] + heat + random.uniform(-0.05, 0.05))
    else:
        # Клапан на отопление: обычная логика CH
        if state["ch_enable"]:
            ch_diff = state["ch_setpoint"] - state["ch_temp"]
            if ch_diff > 3.0:
                state["flame"]      = True
                state["ch_active"]  = True
                state["modulation"] = min(100.0, ch_diff * 8.0 + random.uniform(-2, 2))
            elif ch_diff < 0.5:
                state["flame"]      = False
                state["modulation"] = 0.0
                state["ch_active"]  = True
        else:
            state["flame"]      = False
            state["ch_active"]  = False
            state["modulation"] = 0.0

        # Бойлер медленно остывает пока клапан на отоплении
        state["dhw_temp"] = max(20.0, state["dhw_temp"] - 0.03 + random.uniform(-0.02, 0.02))

        # Температура подачи CH
        if state["flame"]:
            heat_rate = state["modulation"] / 100.0 * 0.5
            state["ch_temp"] = min(state["ch_setpoint"] + 2,
                                   state["ch_temp"] + heat_rate + random.uniform(-0.05, 0.1))
        else:
            cool = 0.1 if state["ch_active"] else 0.05
            state["ch_temp"] = max(20.0, state["ch_temp"] - cool + random.uniform(-0.05, 0.05))

    # Температура на улице — медленная синусоида ±3°C
    state["outside_temp"] = 5.0 + 3.0 * math.sin(t / 600.0)

    # Давление — небольшие флуктуации
    state["pressure"] = round(1.5 + random.uniform(-0.03, 0.03), 2)

    # Округляем до 1 знака
    for k in ("ch_temp", "return_temp", "dhw_temp", "outside_temp", "modulation"):
        state[k] = round(state[k], 1)


# ─── HTTP Handler ─────────────────────────────────────────────────────────────

HTML = None  # загружается при первом запросе

class Handler(BaseHTTPRequestHandler):

    def log_message(self, fmt, *args):
        ts = time.strftime("%H:%M:%S")
        print(f"[{ts}] {self.address_string()} {fmt % args}")

    def send_json(self, code, data):
        body = json.dumps(data, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", len(body))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(body)

    def send_html(self, html: str):
        body = html.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", len(body))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        global HTML
        simulate()

        if self.path in ("/", "/index.html"):
            if HTML is None:
                HTML = load_html()
            self.send_html(HTML)

        elif self.path == "/api/status":
            self.send_json(200, {
                "connected":    state["connected"],
                "fault":        state["fault"],
                "ch_active":    state["ch_active"],
                "dhw_active":   state["dhw_active"],
                "flame":        state["flame"],
                "ch_temp":      state["ch_temp"],
                "return_temp":  state["return_temp"],
                "dhw_temp":     state["dhw_temp"],
                "outside_temp": round(state["outside_temp"], 1),
                "modulation":   state["modulation"],
                "pressure":     state["pressure"],
                "ch_setpoint":  state["ch_setpoint"],
                "dhw_setpoint": state["dhw_setpoint"],
                "dhw_sp_min":   state["dhw_sp_min"],
                "dhw_sp_max":   state["dhw_sp_max"],
                "ch_enable":    state["ch_enable"],
                "dhw_enable":   state["dhw_enable"],
            })

        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        if self.path == "/api/control":
            length = int(self.headers.get("Content-Length", 0))
            raw    = self.rfile.read(length)
            try:
                data = json.loads(raw)
                if "ch_enable"    in data: state["ch_enable"]    = bool(data["ch_enable"])
                if "dhw_enable"   in data: state["dhw_enable"]   = bool(data["dhw_enable"])
                if "ch_setpoint"  in data:
                    v = float(data["ch_setpoint"])
                    state["ch_setpoint"] = max(20.0, min(80.0, v))
                if "dhw_setpoint" in data:
                    v = float(data["dhw_setpoint"])
                    state["dhw_setpoint"] = max(state["dhw_sp_min"], min(state["dhw_sp_max"], v))

                print(f"    → CH={state['ch_enable']} sp={state['ch_setpoint']}°C  "
                      f"DHW={state['dhw_enable']} sp={state['dhw_setpoint']}°C")
                self.send_json(200, {"ok": True})
            except Exception as e:
                self.send_json(400, {"ok": False, "error": str(e)})
        else:
            self.send_response(404)
            self.end_headers()

    def do_OPTIONS(self):
        # Поддержка CORS preflight
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()


# ─── Main ─────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    HTML = load_html()

    server = HTTPServer(("0.0.0.0", port), Handler)
    print(f"\n{'='*50}")
    print(f"  Baxi duo-tec compact — Mock сервер")
    print(f"{'='*50}")
    print(f"  URL: http://localhost:{port}")
    print(f"  API: http://localhost:{port}/api/status")
    print(f"{'='*50}")
    print("  Ctrl+C для остановки\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nОстановлено.")
