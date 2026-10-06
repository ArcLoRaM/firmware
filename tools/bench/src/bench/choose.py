"""bench choose: ask the user which board runs as which class, in a page that opens by itself.

Same loop as the Lavish editor: the agent writes a page, a local server opens it in the user's
browser, the user answers in the page, and the answer comes back to the agent through the command's
output. Local only: the server listens on 127.0.0.1 (a Windows browser reaches it through WSL's
localhost forwarding), under a secret path, answers one valid POST, then stops.
"""

from __future__ import annotations

import json
import platform as _platform
import re
import secrets
import subprocess
import sys
import threading
from html import escape
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Callable

from bench.viz import _CSS as BASE_CSS

CLASSES = ("C3", "C2", "C1", "watch")
_CLASS_TEXT = {"unused": "not in the run", "C3": "C3 (anchor)", "C2": "C2 (relay)", "C1": "C1 (edge)",
               "watch": "watch only (not flashed)"}
_OVERRIDE_NAME = re.compile(r"^[A-Z][A-Z0-9_]{0,63}$")
_OVERRIDE_VALUE = re.compile(r"^[\w.+\-]{1,64}$")
MAX_BODY = 65536


# ---------------------------------------------------------------------------
# The answer
# ---------------------------------------------------------------------------


class AnswerError(ValueError):
    def __init__(self, errors: list[str]) -> None:
        self.errors = errors
        super().__init__("; ".join(errors))


def selectable(entry: dict) -> bool:
    """A board the user can put in a run: it has a Node ID and no other session holds it."""
    return entry["status"] == "free" and entry["node_id"] is not None


def validate_answer(snap: dict, payload: object) -> dict:
    """The user's choice checked against the boards that were shown: every problem is reported at once."""
    if not isinstance(payload, dict):
        raise AnswerError(["the answer must be an object"])
    errors: list[str] = []
    boards = {b["node_id"]: b for b in snap["boards"] if b["node_id"] is not None}

    nodes: dict[int, str] = {}
    chosen = payload.get("nodes")
    if not isinstance(chosen, dict):
        errors.append("nodes must be an object of Node ID to class")
        chosen = {}
    for key, cls in chosen.items():
        if cls in (None, "", "unused"):
            continue
        if not str(key).isdigit():
            errors.append(f"{key!r} is not a Node ID")
            continue
        nid = int(key)
        if cls not in CLASSES:
            errors.append(f"Node ID {nid}: {cls!r} is not a class ({', '.join(CLASSES)})")
        elif nid not in boards:
            errors.append(f"Node ID {nid} is not connected")
        elif boards[nid]["status"] == "held":
            errors.append(f"Node ID {nid} is held by {boards[nid]['held_by']}")
        else:
            nodes[nid] = cls
    if not nodes and not errors:
        errors.append("choose at least one board")

    overrides: dict[str, str] = {}
    raw = payload.get("overrides", {})
    if not isinstance(raw, dict):
        errors.append("overrides must be an object of NAME to value")
        raw = {}
    for name, value in raw.items():
        if not _OVERRIDE_NAME.match(str(name)) or not _OVERRIDE_VALUE.match(str(value)):
            errors.append(f"override {name}={value!r}: a name in capitals, digits and _, and a value of letters, "
                          "digits and . + - _ without spaces")
        else:
            overrides[str(name)] = str(value)

    note = payload.get("note", "")
    note = re.sub(r"[\x00-\x08\x0b-\x1f\x7f]", "", note if isinstance(note, str) else "").strip()
    if len(note) > 500:
        errors.append("the note is longer than 500 characters")
    if errors:
        raise AnswerError(errors)
    return {"nodes": nodes, "overrides": overrides, "note": note}


def flags(answer: dict) -> list[str]:
    """The answer as the options of `bench run` and `bench flash`."""
    nodes = answer["nodes"]
    out: list[str] = []
    for nid in sorted(n for n, c in nodes.items() if c != "watch"):
        out += ["--node", f"{nid}={nodes[nid]}"]
    for nid in sorted(n for n, c in nodes.items() if c == "watch"):
        out += ["--watch", str(nid)]
    for name in sorted(answer["overrides"]):
        out += ["-D", f"{name}={answer['overrides'][name]}"]
    return out


def command_line(answer: dict) -> str:
    return " ".join(flags(answer))


def default_proposal(snap: dict) -> dict[int, str]:
    """The recommendation when the agent has none: the C3 is the board connected to this PC, the other
    boards (Pi Nodes) are C2. A board another session's capture records may be in use: not proposed."""
    free = [b for b in snap["boards"] if selectable(b) and not b.get("capture_elsewhere")]
    local = sorted((b for b in free if b["kind"] == "st-link"), key=lambda b: b["node_id"])
    remote = sorted((b for b in free if b["kind"] == "pi-node"), key=lambda b: b["node_id"])
    out = {local[0]["node_id"]: "C3"} if local else {}
    out.update({b["node_id"]: "C2" for b in remote})
    return out


# ---------------------------------------------------------------------------
# The page
# ---------------------------------------------------------------------------

_EXTRA_CSS = """
.pick { display: grid; gap: 4px; font-size: 0.8rem; color: var(--muted); }
select, textarea, input[type="text"] { font: 0.9rem var(--mono); color: var(--ink); background: var(--bg);
  border: 1.5px solid var(--line); border-radius: 8px; padding: 7px 9px; min-width: 0; width: 100%; }
select:focus-visible, textarea:focus-visible, input:focus-visible, button:focus-visible { outline: 2px solid var(--new); outline-offset: 2px; }
.panel { background: var(--panel); border: 1.5px solid var(--line); border-radius: 10px; padding: 14px 16px; display: grid; gap: 12px; }
.panel label { display: grid; gap: 4px; font-size: 0.85rem; color: var(--muted); }
textarea { min-height: 4.2em; resize: vertical; }
.actions { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
button { font: 600 0.9rem var(--sans); border-radius: 8px; padding: 9px 16px; border: 1.5px solid var(--line);
  background: var(--panel); color: var(--ink); cursor: pointer; }
button.primary { background: var(--ink); color: var(--bg); border-color: var(--ink); }
button:disabled, select:disabled, textarea:disabled, input:disabled { opacity: 0.55; cursor: not-allowed; }
#status { font-size: 0.9rem; min-height: 1.3em; overflow-wrap: anywhere; }
#status.err { color: var(--down); }
#status.ok { color: var(--free); }
"""

_SCRIPT = """
const q = (s) => document.querySelector(s);
const say = (text, cls) => { const s = q("#status"); s.textContent = text; s.className = cls || ""; };
function collect() {
  const nodes = {};
  document.querySelectorAll("select[data-node]").forEach((s) => { nodes[s.dataset.node] = s.value; });
  const overrides = {}, bad = [];
  q("#overrides").value.split("\\n").map((l) => l.trim()).filter(Boolean).forEach((l) => {
    const i = l.indexOf("=");
    if (i < 1) { bad.push(l); return; }
    overrides[l.slice(0, i).trim()] = l.slice(i + 1).trim();
  });
  return { nodes, overrides, note: q("#note").value, bad };
}
q("#reset").addEventListener("click", () => {
  document.querySelectorAll("select[data-node]").forEach((s) => { s.value = s.dataset.proposed; });
  say("", "");
});
q("#send").addEventListener("click", async () => {
  const c = collect();
  if (c.bad.length) { say("Not NAME=VALUE: " + c.bad.join(", "), "err"); return; }
  delete c.bad;
  try {
    const r = await fetch("answer", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(c) });
    const j = await r.json();
    if (r.ok) {
      document.querySelectorAll("select, textarea, input, button").forEach((e) => { e.disabled = true; });
      say("Sent to the agent. You can close this tab.", "ok");
    } else {
      say((j.errors || [j.error]).join(" | "), "err");
    }
  } catch (e) {
    say("Could not reach bench: " + e, "err");
  }
});
"""


def _group_title(kind: str) -> str:
    return "USB · ST-LINK probes" if kind == "st-link" else "Tailnet · Pi Nodes"


def _choice_card(entry: dict, proposed: str) -> str:
    state = entry["status"]
    nid = entry["node_id"]
    rows = [("on", entry["where"])]
    if entry.get("build"):
        rows.append(("build", entry["build"]))
    dl = "".join(f"<dt>{escape(k)}</dt><dd>{escape(v)}</dd>" for k, v in rows)
    pill_text = {"free": "free", "held": "held", "new": "new board"}[state]
    head = (f'<div class="top"><span class="nid"><small>NODE</small>{escape(str(nid)) if nid is not None else "?"}</span>'
            f'<span class="pill">{pill_text}</span></div>')
    if selectable(entry):
        options = "".join(
            f'<option value="{c}"{" selected" if c == proposed else ""}>{escape(_CLASS_TEXT[c])}</option>'
            for c in ("unused", "C3", "C2", "C1", "watch"))
        body = (f'<label class="pick">class<select data-node="{nid}" data-proposed="{proposed}" '
                f'aria-label="Class for Node {nid}">{options}</select></label>')
        if entry.get("capture_elsewhere"):
            body += f'<div class="who">recorded by {escape(entry["capture_elsewhere"])}: the board may be in use</div>'
    elif state == "held":
        body = f'<div class="who">held by {escape(entry["held_by"])}</div>'
    else:
        body = '<div class="who" style="color: var(--new)">needs a Node ID: the agent registers it before a run</div>'
    return f'<article class="card" data-state="{state}">{head}<dl>{dl}</dl>{body}</article>'


def render_chooser(snap: dict, proposal: dict[int, str], overrides: dict[str, str]) -> str:
    """The page that asks for the deployment. `proposal` is the agent's recommendation (Node ID to class)."""
    groups: dict[str, list[str]] = {"st-link": [], "pi-node": []}
    for b in snap["boards"]:
        proposed = proposal.get(b["node_id"], "unused") if b["node_id"] is not None else "unused"
        groups[b["kind"]].append(_choice_card(b, proposed))
    for d in snap["not_answering"]:
        groups["pi-node"].append(
            f'<article class="card" data-state="not answering"><div class="top"><span class="nid"><small>NODE</small>?</span>'
            f'<span class="pill">not answering</span></div><dl><dt>on</dt><dd>{escape(d["where"])}</dd>'
            f'<dt>id</dt><dd>{escape(d["id"])}</dd></dl></article>')
    branches = "".join(
        f'<section class="branch"><h2>{escape(_group_title(kind))}</h2>'
        + (f'<div class="cards">{"".join(cards)}</div>' if cards else '<div class="none">none</div>')
        + "</section>" for kind, cards in groups.items())
    when = f'{snap["generated"][11:16]} UTC, {snap["generated"][:10]}'
    lines = "\n".join(f"{k}={v}" for k, v in overrides.items())
    return (
        '<!doctype html><html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width, initial-scale=1">'
        f"<title>Bench Choose</title><style>{BASE_CSS}{_EXTRA_CSS}</style></head><body>"
        '<div class="wrap"><header><h1>Choose the deployment</h1>'
        f'<p class="meta">Snapshot {escape(when)} · worktree {escape(snap["worktree"])} · '
        "pick a class for each board that takes part; the agent's recommendation is preselected.</p></header>"
        f'<div class="pc"><div class="name">this PC</div><div class="sub">worktree {escape(snap["worktree"])}</div></div>'
        f'<div class="branches">{branches}</div>'
        '<section class="panel"><label>Build Overrides, one NAME=VALUE per line, for this run only'
        f'<textarea id="overrides" spellcheck="false">{escape(lines)}</textarea></label>'
        '<label>A note for the agent (optional)<input type="text" id="note" maxlength="500" autocomplete="off"></label>'
        '<div class="actions"><button class="primary" id="send" type="button">Send to the agent</button>'
        '<button id="reset" type="button">Use the recommendation</button></div>'
        '<p id="status" role="status"></p></section>'
        '<footer>Nothing leaves this PC: the page talks to the bench command that opened it.</footer></div>'
        f"<script>{_SCRIPT}</script></body></html>")


# ---------------------------------------------------------------------------
# The server
# ---------------------------------------------------------------------------


class Chooser:
    """Serves the page on loopback and waits for the user's answer."""

    def __init__(self, snap: dict, proposal: dict[int, str], overrides: dict[str, str]) -> None:
        self.snap = snap
        self.page = render_chooser(snap, proposal, overrides)
        self.token = secrets.token_urlsafe(16)
        self.answer: dict | None = None
        self._done = threading.Event()
        self._lock = threading.Lock()
        chooser = self

        class Handler(BaseHTTPRequestHandler):
            server_version = "bench-choose"

            def log_message(self, *args) -> None:
                pass

            def _send(self, code: int, body: str, ctype: str = "application/json") -> None:
                data = body.encode("utf-8")
                self.send_response(code)
                self.send_header("Content-Type", f"{ctype}; charset=utf-8")
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Cache-Control", "no-store")
                self.send_header("Referrer-Policy", "no-referrer")
                self.end_headers()
                self.wfile.write(data)

            def _refuse(self, code: int, why: str) -> None:
                self._send(code, json.dumps({"error": why}))

            def _local(self) -> bool:
                return self.headers.get("Host", "") in chooser.hosts

            def do_GET(self) -> None:
                if not self._local():
                    return self._refuse(403, "unknown host")
                path = self.path.split("?")[0]
                if path == chooser.base:
                    return self._send(200, chooser.page, "text/html")
                if path == chooser.base + "state":
                    return self._send(200, json.dumps({"answered": chooser._done.is_set()}))
                self._refuse(404, "not found")

            def do_POST(self) -> None:
                if not self._local():
                    return self._refuse(403, "unknown host")
                if self.path != chooser.base + "answer":
                    return self._refuse(404, "not found")
                origin = self.headers.get("Origin")
                if origin and origin not in chooser.origins:
                    return self._refuse(403, "unknown origin")
                if self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
                    return self._refuse(415, "send JSON")
                length = int(self.headers.get("Content-Length") or 0)
                if length > MAX_BODY:
                    self.rfile.read(min(length, 1_000_000))
                    return self._refuse(413, "too large")
                try:
                    payload = json.loads(self.rfile.read(length) or b"null")
                    answer = validate_answer(chooser.snap, payload)
                except AnswerError as exc:
                    return self._send(400, json.dumps({"errors": exc.errors}))
                except ValueError:
                    return self._refuse(400, "not JSON")
                with chooser._lock:
                    if chooser._done.is_set():
                        return self._refuse(409, "already answered")
                    chooser.answer = answer
                    chooser._done.set()
                self._send(200, json.dumps({"ok": True}))

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.port = self.server.server_address[1]
        self.base = f"/{self.token}/"
        self.hosts = {f"127.0.0.1:{self.port}", f"localhost:{self.port}"}
        self.origins = {f"http://{h}" for h in self.hosts}

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self.port}{self.base}"

    def start(self) -> None:
        threading.Thread(target=self.server.serve_forever, name="bench-choose", daemon=True).start()

    def wait(self, timeout: float) -> dict | None:
        """The user's answer, or None when none came in `timeout` seconds."""
        return self.answer if self._done.wait(timeout) else None

    def close(self) -> None:
        self.server.shutdown()
        self.server.server_close()


# ---------------------------------------------------------------------------
# Opening the page
# ---------------------------------------------------------------------------


def open_command(target: str, release: str | None = None, platform: str | None = None) -> list[str]:
    """The command that opens `target` (a URL or a Windows path) in the user's browser: under WSL the
    Windows browser, elsewhere the desktop's opener."""
    if "'" in target or any(ord(c) < 32 for c in target):
        raise ValueError(f"not an address bench will pass to a shell: {target!r}")
    release = (_platform.uname().release if release is None else release).lower()
    platform = sys.platform if platform is None else platform
    if "microsoft" in release:
        return ["powershell.exe", "-NoProfile", "-Command", f"Start-Process '{target}'"]
    return ["open" if platform == "darwin" else "xdg-open", target]


def open_in_browser(target: str, run: Callable = subprocess.run) -> bool:
    """Ask the desktop to open `target`; False when it could not (the caller prints the address)."""
    try:
        return run(open_command(target), capture_output=True, timeout=30).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def ask(snap: dict, proposal: dict[int, str], overrides: dict[str, str], timeout_s: float,
        opener: Callable[[str], bool] = open_in_browser,
        say: Callable[[str], None] = lambda m: print(m, file=sys.stderr, flush=True)) -> dict | None:
    """Serve the page, open it, and wait for the user's answer. None when none came in time."""
    chooser = Chooser(snap, proposal, overrides)
    chooser.start()
    try:
        say(chooser.url)
        if not opener(chooser.url):
            say("could not open a browser: open the address above yourself")
        return chooser.wait(timeout_s)
    finally:
        chooser.close()
