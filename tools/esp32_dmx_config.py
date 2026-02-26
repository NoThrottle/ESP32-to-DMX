#!/usr/bin/env python3
"""
ESP32 DMX Node Configurator
Windows UI tool for the ESP32-C6 triple-universe DMX node.

Communicates over the native USB Serial/JTAG port (HWCDC).
Connect the USB-C port labelled "USB TO PC" (not the CH340 bridge port).

Requirements:  pip install pyserial
"""

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import serial
import serial.tools.list_ports
import threading
import queue
import json


# ---------------------------------------------------------------------------
#  Main application
# ---------------------------------------------------------------------------

class ESP32DMXConfig:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("ESP32 DMX Node Configurator")
        self.root.minsize(560, 560)

        self._serial: serial.Serial | None = None
        self._rx_thread: threading.Thread | None = None
        self._running = False
        self._rx_queue: queue.Queue[str] = queue.Queue()

        # When True, incoming lines are scanned for a JSON STATUS response
        self._waiting_status = False
        self._status_buf = ""

        # When True, incoming lines are scanned for a WIFISCAN response
        self._waiting_scan = False
        self._scan_buf = ""
        self._scan_networks: list = []

        self._build_ui()
        self._refresh_ports()
        self.root.after(100, self._poll_rx)

    # -------------------------------------------------------------------------
    #  UI construction
    # -------------------------------------------------------------------------

    def _build_ui(self):
        self.root.configure(padx=8, pady=8)

        # ── Connection bar ─────────────────────────────────────────────────
        conn = ttk.LabelFrame(self.root, text="Connection", padding=6)
        conn.pack(fill="x", pady=(0, 6))

        ttk.Label(conn, text="Port:").pack(side="left")
        self._port_var = tk.StringVar()
        self._port_combo = ttk.Combobox(conn, textvariable=self._port_var,
                                         width=10, state="readonly")
        self._port_combo.pack(side="left", padx=(4, 2))

        ttk.Button(conn, text="Refresh", command=self._refresh_ports).pack(side="left", padx=2)
        self._conn_btn = ttk.Button(conn, text="Connect", command=self._toggle_connect)
        self._conn_btn.pack(side="left", padx=(8, 4))

        self._conn_label = ttk.Label(conn, text="● Disconnected", foreground="#cc0000")
        self._conn_label.pack(side="left", padx=4)

        # ── Notebook ───────────────────────────────────────────────────────
        nb = ttk.Notebook(self.root)
        nb.pack(fill="both", expand=True, pady=(4, 0))

        cfg_tab = ttk.Frame(nb, padding=10)
        mon_tab = ttk.Frame(nb, padding=10)
        nb.add(cfg_tab, text="  Configuration  ")
        nb.add(mon_tab, text="  Monitor / Terminal  ")

        self._build_config_tab(cfg_tab)
        self._build_monitor_tab(mon_tab)

    # ── Configuration tab ──────────────────────────────────────────────────

    def _build_config_tab(self, parent: ttk.Frame):
        # WiFi ---------------------------------------------------------------
        wf = ttk.LabelFrame(parent, text="WiFi", padding=8)
        wf.pack(fill="x", pady=(0, 8))

        self._btns: list[ttk.Button] = []
        self._wifi = {}
        rows = [
            ("SSID:",       "ssid",   False),
            ("Password:",   "pass",   True),
            ("Static IP:",  "ip",     False),
            ("Gateway:",    "gw",     False),
            ("Subnet:",     "subnet", False),
        ]
        for i, (lbl, key, hidden) in enumerate(rows):
            ttk.Label(wf, text=lbl, width=13, anchor="e").grid(
                row=i, column=0, sticky="e", padx=4, pady=2)
            var = tk.StringVar()
            if key == "ssid":
                self._ssid_combo = ttk.Combobox(wf, textvariable=var, width=27, state="normal")
                self._ssid_combo.grid(row=i, column=1, sticky="w", pady=2)
                self._ssid_combo.bind("<<ComboboxSelected>>", self._on_ssid_selected)
                self._scan_btn = ttk.Button(wf, text="Scan",
                                            command=self._scan_wifi, state="disabled")
                self._scan_btn.grid(row=i, column=2, sticky="w", padx=(4, 0), pady=2)
                self._btns.append(self._scan_btn)
            else:
                e = ttk.Entry(wf, textvariable=var, width=30,
                              show="*" if hidden else "")
                e.grid(row=i, column=1, sticky="w", pady=2)
            self._wifi[key] = var

        ttk.Label(wf, text="Leave IP / GW / Subnet blank to use DHCP",
                  foreground="gray").grid(
            row=len(rows), column=0, columnspan=2, sticky="w", padx=4, pady=(2, 0))

        # DMX / Art-Net -------------------------------------------------------
        df = ttk.LabelFrame(parent, text="DMX / Art-Net", padding=8)
        df.pack(fill="x", pady=(0, 8))

        self._u1_mode  = tk.StringVar(value="TX")
        self._u2_mode  = tk.StringVar(value="TX")
        self._u1_artnet = tk.StringVar(value="0")
        self._u2_artnet = tk.StringVar(value="1")
        self._artnet_port = tk.StringVar(value="6454")

        modes = ["TX", "RX", "PASS"]

        def dmx_row(row, label, mode_var, art_var):
            ttk.Label(df, text=label, anchor="e").grid(
                row=row, column=0, sticky="e", padx=(4, 6), pady=3)
            ttk.Combobox(df, textvariable=mode_var, values=modes,
                         width=6, state="readonly").grid(
                row=row, column=1, sticky="w", pady=3)
            ttk.Label(df, text="Art-Net Universe:").grid(
                row=row, column=2, sticky="e", padx=(14, 4), pady=3)
            ttk.Entry(df, textvariable=art_var, width=6).grid(
                row=row, column=3, sticky="w", pady=3)

        dmx_row(0, "Universe 1 Mode:", self._u1_mode, self._u1_artnet)
        dmx_row(1, "Universe 2 Mode:", self._u2_mode, self._u2_artnet)

        ttk.Label(df, text="Art-Net UDP Port:").grid(
            row=2, column=0, sticky="e", padx=(4, 6), pady=3)
        ttk.Entry(df, textvariable=self._artnet_port, width=8).grid(
            row=2, column=1, sticky="w", pady=3)

        hint = ttk.Label(
            df,
            text="Loopback test: set U1=TX, U2=RX and wire GPIO6 → GPIO11.",
            foreground="#0066aa",
        )
        hint.grid(row=3, column=0, columnspan=4, sticky="w", padx=4, pady=(4, 0))

        # Action buttons -------------------------------------------------------
        bf = ttk.Frame(parent)
        bf.pack(fill="x", pady=(4, 0))

        def mkbtn(text, cmd):
            b = ttk.Button(bf, text=text, command=cmd, state="disabled")
            b.pack(side="left", padx=4)
            self._btns.append(b)
            return b

        mkbtn("Read Status",  self._read_status)
        mkbtn("Apply",         self._apply_config)
        mkbtn("Reboot Device", self._reboot)
        mkbtn("Factory Reset", self._factory_reset)

    # ── Monitor tab ───────────────────────────────────────────────────────

    def _build_monitor_tab(self, parent: ttk.Frame):
        ctrl = ttk.Frame(parent)
        ctrl.pack(fill="x", pady=(0, 6))

        def monbtn(text, cmd):
            b = ttk.Button(ctrl, text=text, command=cmd, state="disabled")
            b.pack(side="left", padx=4)
            self._btns.append(b)
            return b

        monbtn("DMX Monitor ON",  lambda: self._send("DMXMON ON\n"))
        monbtn("DMX Monitor OFF", lambda: self._send("DMXMON OFF\n"))
        ttk.Button(ctrl, text="Clear", command=self._clear_monitor).pack(side="left", padx=4)

        # Raw command entry
        cmd_row = ttk.Frame(parent)
        cmd_row.pack(fill="x", pady=(0, 6))
        ttk.Label(cmd_row, text="Command:").pack(side="left")
        self._cmd_var = tk.StringVar()
        entry = ttk.Entry(cmd_row, textvariable=self._cmd_var)
        entry.pack(side="left", padx=4, fill="x", expand=True)
        entry.bind("<Return>", lambda _e: self._send_raw_cmd())
        ttk.Button(cmd_row, text="Send", command=self._send_raw_cmd).pack(side="left")

        # Output
        self._monitor = scrolledtext.ScrolledText(
            parent, height=18, wrap="word",
            font=("Consolas", 9),
            bg="#1e1e1e", fg="#d4d4d4",
            insertbackground="white",
        )
        self._monitor.pack(fill="both", expand=True)
        self._monitor.configure(state="disabled")

    # -------------------------------------------------------------------------
    #  Serial helpers
    # -------------------------------------------------------------------------

    def _refresh_ports(self):
        ports = sorted(p.device for p in serial.tools.list_ports.comports())
        self._port_combo["values"] = ports
        if ports and not self._port_var.get():
            self._port_combo.current(0)

    def _toggle_connect(self):
        if self._serial and self._serial.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self._port_var.get()
        if not port:
            messagebox.showerror("Error", "Select a COM port first.")
            return
        try:
            self._serial = serial.Serial(port, baudrate=115200, timeout=0.1)
            self._running = True
            self._rx_thread = threading.Thread(target=self._read_loop, daemon=True)
            self._rx_thread.start()

            self._conn_label.config(text=f"● Connected  ({port})", foreground="#007700")
            self._conn_btn.config(text="Disconnect")
            self._set_enabled(True)
            self._log(f"[Connected to {port}]\n")
            # Auto-query status
            self.root.after(500, self._read_status)
        except Exception as exc:
            messagebox.showerror("Connection failed", str(exc))

    def _disconnect(self):
        self._running = False
        if self._serial:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None
        self._conn_label.config(text="● Disconnected", foreground="#cc0000")
        self._conn_btn.config(text="Connect")
        self._set_enabled(False)

    def _set_enabled(self, enabled: bool):
        state = "normal" if enabled else "disabled"
        for btn in self._btns:
            btn.configure(state=state)

    def _send(self, text: str):
        if self._serial and self._serial.is_open:
            try:
                self._serial.write(text.encode())
            except Exception as exc:
                self._log(f"[TX ERROR] {exc}\n")

    def _send_raw_cmd(self):
        cmd = self._cmd_var.get().strip()
        if cmd:
            self._log(f"> {cmd}\n")
            self._send(cmd + "\n")
            self._cmd_var.set("")

    def _read_loop(self):
        """Background thread: readline and push to queue."""
        while self._running:
            try:
                raw = self._serial.readline()
                if raw:
                    self._rx_queue.put(raw.decode("utf-8", errors="replace"))
            except Exception:
                if self._running:
                    self._rx_queue.put("[SERIAL ERROR — device disconnected]\n")
                    self._running = False
                break

    def _poll_rx(self):
        """Drain the RX queue from the main thread (called every 100 ms)."""
        while not self._rx_queue.empty():
            try:
                line = self._rx_queue.get_nowait()
            except queue.Empty:
                break
            self._log(line)
            # After "applied" confirmation, refresh status after a short delay
            # to pick up the new IP if WiFi reconnected
            if '"status":"applied"' in line:
                self.root.after(3000, self._read_status)
            if self._waiting_status:
                self._status_buf += line
                # The STATUS response is one JSON line starting with '{'
                start = self._status_buf.find("{")
                end   = self._status_buf.rfind("}") + 1
                if start >= 0 and end > start:
                    self._populate_config(self._status_buf[start:end])
                    self._waiting_status = False
                    self._status_buf = ""

            if self._waiting_scan:
                self._scan_buf += line
                start = self._scan_buf.find("{")
                end   = self._scan_buf.rfind("}") + 1
                if start >= 0 and end > start:
                    chunk = self._scan_buf[start:end]
                    try:
                        d = json.loads(chunk)
                        if "networks" in d:
                            self._populate_wifi_scan(d)
                            self._waiting_scan = False
                            self._scan_buf = ""
                    except json.JSONDecodeError:
                        pass

        self.root.after(100, self._poll_rx)

    def _log(self, text: str):
        self._monitor.configure(state="normal")
        self._monitor.insert("end", text)
        self._monitor.see("end")
        self._monitor.configure(state="disabled")

    def _clear_monitor(self):
        self._monitor.configure(state="normal")
        self._monitor.delete("1.0", "end")
        self._monitor.configure(state="disabled")

    # -------------------------------------------------------------------------
    #  Config actions
    # -------------------------------------------------------------------------

    def _scan_wifi(self):
        self._waiting_scan = True
        self._scan_buf = ""
        self._scan_networks = []
        self._log("[Scanning for WiFi networks...]\n")
        self._send("WIFISCAN\n")

    def _on_ssid_selected(self, _event):
        idx = self._ssid_combo.current()
        if 0 <= idx < len(self._scan_networks):
            self._wifi["ssid"].set(self._scan_networks[idx]["ssid"])

    def _populate_wifi_scan(self, d: dict):
        networks = d.get("networks", [])
        networks.sort(key=lambda n: n.get("rssi", -100), reverse=True)
        self._scan_networks = networks
        display = []
        for n in networks:
            lock = "[*]" if n.get("secure") else "[ ]"
            display.append(f"{lock} {n['ssid']}  ({n.get('rssi', '?')} dBm)")
        self._ssid_combo["values"] = display
        self._log(f"[Scan complete: {len(networks)} network(s) found]\n")

    def _read_status(self):
        self._waiting_status = True
        self._status_buf = ""
        self._send("STATUS\n")

    def _populate_config(self, json_str: str):
        try:
            d = json.loads(json_str)
        except json.JSONDecodeError:
            return

        self._wifi["ssid"].set(d.get("ssid", ""))
        self._wifi["pass"].set("")          # password not echoed by firmware
        self._wifi["ip"].set(d.get("ip", ""))
        self._wifi["gw"].set(d.get("gw", ""))
        self._wifi["subnet"].set(d.get("subnet", "255.255.255.0"))

        self._artnet_port.set(str(d.get("artnet_port", 6454)))
        self._u1_artnet.set(str(d.get("u1_artnet", 0)))
        self._u2_artnet.set(str(d.get("u2_artnet", 1)))
        self._u1_mode.set(d.get("u1_mode", "TX"))
        self._u2_mode.set(d.get("u2_mode", "TX"))

        # Show RX frame counts in the monitor tab as a diagnostic line
        u1_rx = d.get("u1_rx_frames", 0)
        u2_rx = d.get("u2_rx_frames", 0)
        if u1_rx or u2_rx:
            self._log(f"[RX frames: U1={u1_rx}  U2={u2_rx}]\n")

    def _apply_config(self):
        try:
            art_port = int(self._artnet_port.get())
            u1_art   = int(self._u1_artnet.get())
            u2_art   = int(self._u2_artnet.get())
        except ValueError:
            messagebox.showerror("Validation Error",
                                 "Art-Net port and universe numbers must be integers.")
            return

        if not (1 <= art_port <= 65535):
            messagebox.showerror("Validation Error", "Art-Net port must be 1–65535.")
            return

        cfg: dict = {
            "ssid":        self._wifi["ssid"].get(),
            "ip":          self._wifi["ip"].get(),
            "gw":          self._wifi["gw"].get(),
            "subnet":      self._wifi["subnet"].get(),
            "artnet_port": art_port,
            "u1_artnet":   u1_art,
            "u2_artnet":   u2_art,
            "u1_mode":     self._u1_mode.get(),
            "u2_mode":     self._u2_mode.get(),
        }
        pw = self._wifi["pass"].get()
        if pw:
            cfg["pass"] = pw          # only send if the user typed a new one

        cmd = f"CONFIG {json.dumps(cfg)}\n"
        self._log(f"> {cmd}")
        self._send(cmd)
        # Device applies settings live — no reboot, stay connected
        self._log("[Config sent. Device will reconnect WiFi if credentials changed.]\n")

    def _reboot(self):
        if messagebox.askyesno("Reboot", "Reboot the ESP32 now?"):
            self._send("REBOOT\n")
            self.root.after(1500, self._disconnect)

    def _factory_reset(self):
        if messagebox.askyesno(
            "Factory Reset",
            "This will erase ALL saved settings and reboot the device.\nContinue?",
        ):
            self._send("RESET\n")
            self.root.after(1500, self._disconnect)


# ---------------------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------------------

def main():
    root = tk.Tk()
    app = ESP32DMXConfig(root)
    root.protocol("WM_DELETE_WINDOW", lambda: (app._disconnect(), root.destroy()))
    root.mainloop()


if __name__ == "__main__":
    main()
