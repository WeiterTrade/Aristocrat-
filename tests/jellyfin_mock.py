# Простой заменитель сервера Jellyfin для автопроверки вкладки «Сервер»
import json, sys, base64
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

PERSONS = {
    "p1": {"Id": "p1", "Name": "Andi Avalon", "Type": "Person", "Overview": "", "ImageTags": {}, "Tags": ["Старый тег"]},
    "p2": {"Id": "p2", "Name": "Cory Chase", "Type": "Person", "Overview": "Анкета уже заполнена.", "PremiereDate": "1980-04-10T00:00:00.0000000Z", "ProductionLocations": ["США"], "ImageTags": {"Primary": "x"}},
}
LOG = open(sys.argv[2] if len(sys.argv) > 2 else "jellyfin.log", "w", encoding="utf-8")

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, obj=None):
        body = json.dumps(obj, ensure_ascii=False).encode() if obj is not None else b""
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def authed(self):
        return 'Token="tok"' in self.headers.get("Authorization", "")
    def do_GET(self):
        u = urlparse(self.path); q = parse_qs(u.query)
        if not self.authed(): return self.send(401)
        if u.path == "/Persons":
            term = q.get("searchTerm", [""])[0].lower()
            items = [p for p in PERSONS.values() if term in p["Name"].lower()]
            return self.send(200, {"Items": items, "TotalRecordCount": len(items)})
        if u.path.endswith("/Images/Primary"):   # фото исполнителя с сервера
            import os
            img = os.path.join(os.path.dirname(os.path.abspath(__file__)), "release", "gallery-2-large.jpg")
            if os.path.exists(img):
                body = open(img, "rb").read()
                self.send_response(200); self.send_header("Content-Type", "image/jpeg"); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
                LOG.write("PHOTO SENT %s\n" % u.path); LOG.flush()
                return
            return self.send(404)
        if u.path.startswith("/Users/u1/Items/"):
            pid = u.path.rsplit("/", 1)[1]
            return self.send(200, PERSONS[pid]) if pid in PERSONS else self.send(404)
        self.send(404)
    def do_POST(self):
        u = urlparse(self.path)
        data = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if u.path == "/v1/chat/completions":   # заменитель нейросети (формат OpenAI)
            req = json.loads(data)
            prompt = req["messages"][0]["content"]
            LOG.write("AI REQUEST model=%s key=%s prompt_has_iafd=%s\n" % (req.get("model"), self.headers.get("Authorization"), "IAFD" in prompt)); LOG.flush()
            answer = {"ru_name": "Энди Авалон", "bio": "Энди Авалон (Andi Avalon) — американская актриса фильмов для взрослых.\n\nВторой абзац.\n\nТретий абзац.",
                      "birth_date": "1988-08-20", "birth_place": "Лос-Анджелес, США", "aliases": ["Two Cherrys", "Andi"]}
            return self.send(200, {"choices": [{"message": {"role": "assistant", "content": "```json\n" + json.dumps(answer, ensure_ascii=False) + "\n```"}}]})
        if u.path == "/Users/AuthenticateByName":
            return self.send(200, {"AccessToken": "tok", "User": {"Id": "u1", "Name": "test"}})
        if not self.authed(): return self.send(401)
        if u.path.endswith("/Images/Primary"):
            img = base64.b64decode(data)
            LOG.write("IMAGE %s %d bytes jpeg=%s\n" % (u.path, len(img), img[:2] == b"\xff\xd8")); LOG.flush()
            return self.send(204)
        if u.path.startswith("/Items/"):
            o = json.loads(data)
            LOG.write("UPDATE %s\n%s\n" % (u.path, json.dumps({k: o.get(k) for k in ("Name", "Overview", "PremiereDate", "ProductionLocations", "Tags")}, ensure_ascii=False, indent=1)))
            LOG.flush()
            return self.send(204)
        self.send(404)

HTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
