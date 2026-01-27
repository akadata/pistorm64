#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import json
import os
import socket
import sys
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

import os

DEFAULT_ADF_DIR = os.path.join(os.path.expanduser("~"), "pistorm64", "data", "adfs")
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 23890

ALLOWED_EXTS = {".adf", ".hdf"}


def send_disk_cmd(host, port, command):
    with socket.create_connection((host, port), timeout=5.0) as sock:
        sock.sendall((command + "\n").encode("utf-8"))
        data = sock.recv(4096)
    return data.decode("utf-8", errors="replace").strip()


def list_disks(adf_dir):
    try:
        entries = []
        for name in sorted(os.listdir(adf_dir)):
            _, ext = os.path.splitext(name)
            if ext.lower() in ALLOWED_EXTS:
                entries.append(name)
        return entries
    except FileNotFoundError:
        return []


def build_insert_cmd(unit, filename, writable):
    if writable:
        return f"insert {unit} -rw {filename}"
    return f"insert {unit} {filename}"


def serve_web(args):
    adf_dir = args.adf_dir
    host = args.host
    port = args.port
    disk_host = args.disk_host
    disk_port = args.disk_port

    class Handler(BaseHTTPRequestHandler):
        def _send_json(self, payload, status=HTTPStatus.OK):
            data = json.dumps(payload).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _send_text(self, text, status=HTTPStatus.OK):
            data = text.encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _send_html(self, html):
            data = html.encode("utf-8")
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path == "/":
                return self._send_html(render_index())
            if parsed.path == "/api/disks":
                return self._send_json({"disks": list_disks(adf_dir)})
            if parsed.path == "/api/status":
                return self._send_json(
                    {
                        "adf_dir": adf_dir,
                        "disk_host": disk_host,
                        "disk_port": disk_port,
                    }
                )
            return self._send_text("Not found", status=HTTPStatus.NOT_FOUND)

        def do_POST(self):
            parsed = urlparse(self.path)
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length) if length else b""
            data = parse_qs(body.decode("utf-8"))

            def get_field(name, default=None):
                return data.get(name, [default])[0]

            if parsed.path == "/api/eject":
                unit = get_field("unit", "0")
                try:
                    res = send_disk_cmd(disk_host, disk_port, f"eject {int(unit)}")
                except Exception as exc:
                    return self._send_json({"error": str(exc)}, status=HTTPStatus.BAD_GATEWAY)
                return self._send_json({"result": res})

            if parsed.path == "/api/insert":
                unit = get_field("unit", "0")
                name = get_field("name")
                writable = get_field("writable", "0") == "1"
                if not name:
                    return self._send_json({"error": "missing name"}, status=HTTPStatus.BAD_REQUEST)
                filename = os.path.join(adf_dir, name)
                cmd = build_insert_cmd(int(unit), filename, writable)
                try:
                    res = send_disk_cmd(disk_host, disk_port, cmd)
                except Exception as exc:
                    return self._send_json({"error": str(exc)}, status=HTTPStatus.BAD_GATEWAY)
                return self._send_json({"result": res})

            return self._send_text("Not found", status=HTTPStatus.NOT_FOUND)

        def log_message(self, format, *args):
            return

    server = ThreadingHTTPServer((host, port), Handler)
    print(f"ADF web manager listening on http://{host}:{port}")
    print(f"ADF dir: {adf_dir}")
    print(f"Disk service: {disk_host}:{disk_port}")
    server.serve_forever()


def render_index():
    return """<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <title>ADF Manager</title>
    <style>
      body { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; margin: 24px; }
      h1 { margin-bottom: 12px; }
      .row { display: flex; gap: 12px; align-items: center; margin-bottom: 12px; }
      select, button, input { font-family: inherit; font-size: 14px; }
      #log { white-space: pre-wrap; background: #f2f2f2; padding: 10px; }
    </style>
  </head>
  <body>
    <h1>ADF Manager</h1>
    <div class="row">
      <label>Unit:</label>
      <select id="unit">
        <option value="0">PD0</option>
        <option value="1">PD1</option>
        <option value="2">PD2</option>
        <option value="3">PD3</option>
      </select>
      <label>Disk:</label>
      <select id="disk"></select>
      <label><input type="checkbox" id="rw"> RW</label>
      <button onclick="insertDisk()">Insert</button>
      <button onclick="ejectDisk()">Eject</button>
      <button onclick="refresh()">Refresh</button>
    </div>
    <div id="log"></div>
    <script>
      async function api(path, body) {
        const opts = body ? {method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body} : {};
        const res = await fetch(path, opts);
        return res.json();
      }
      async function refresh() {
        const data = await api('/api/disks');
        const sel = document.getElementById('disk');
        sel.innerHTML = '';
        for (const name of data.disks) {
          const opt = document.createElement('option');
          opt.value = name;
          opt.textContent = name;
          sel.appendChild(opt);
        }
        log('disks: ' + data.disks.join(', '));
      }
      async function insertDisk() {
        const unit = document.getElementById('unit').value;
        const name = document.getElementById('disk').value;
        const rw = document.getElementById('rw').checked ? '1' : '0';
        const res = await api('/api/insert', `unit=${unit}&name=${encodeURIComponent(name)}&writable=${rw}`);
        log(res.result || res.error);
      }
      async function ejectDisk() {
        const unit = document.getElementById('unit').value;
        const res = await api('/api/eject', `unit=${unit}`);
        log(res.result || res.error);
      }
      function log(msg) {
        document.getElementById('log').textContent = msg;
      }
      refresh();
    </script>
  </body>
</html>
"""


def main():
    parser = argparse.ArgumentParser(description="ADF manager for a314disk.device")
    parser.add_argument("--adf-dir", default=DEFAULT_ADF_DIR, help="ADF/HDF directory")
    parser.add_argument("--disk-host", default=DEFAULT_HOST, help="Disk service host")
    parser.add_argument("--disk-port", type=int, default=DEFAULT_PORT, help="Disk service port")
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("list", help="List available disk images")

    insert = sub.add_parser("insert", help="Insert disk image")
    insert.add_argument("unit", type=int)
    insert.add_argument("path")
    insert.add_argument("--rw", action="store_true")

    eject = sub.add_parser("eject", help="Eject disk image")
    eject.add_argument("unit", type=int)

    serve = sub.add_parser("serve", help="Run web UI")
    serve.add_argument("--host", default="0.0.0.0")
    serve.add_argument("--port", type=int, default=8088)

    args = parser.parse_args()

    if args.cmd == "list":
        for name in list_disks(args.adf_dir):
            print(name)
        return 0

    if args.cmd == "insert":
        path = args.path
        if not os.path.isabs(path):
            path = os.path.join(args.adf_dir, path)
        cmd = build_insert_cmd(args.unit, path, args.rw)
        print(send_disk_cmd(args.disk_host, args.disk_port, cmd))
        return 0

    if args.cmd == "eject":
        print(send_disk_cmd(args.disk_host, args.disk_port, f"eject {args.unit}"))
        return 0

    if args.cmd == "serve":
        serve_web(args)
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
