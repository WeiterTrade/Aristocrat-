# Тестовый «сайт по подписке» для автопроверки: вход, окно 18+, плеер с HLS.
import http.server, sys, os, urllib.parse
ROOT = sys.argv[1]
LOGIN = b"""<!doctype html><html><head><meta charset=utf-8><title>Login</title></head><body>
<h1>Sign in</h1><form method=post action=/Account/Login>
<input type=email name=Email placeholder=Email><input type=password name=Password>
<label><input type=checkbox name=RememberMe> Remember</label><button type=submit>Log In</button></form></body></html>"""
VIDEO = b"""<!doctype html><html><head><meta charset=utf-8><title>Test Movie (2026) | Test Empire</title>
<meta property="og:image" content="/thumb.jpg"></head><body>
<div id=gate style="padding:40px"><h2>Confirm You are Over 18.</h2><a href="#" id=yes>Yes, I am 18+</a></div>
<div id=player style="display:none"><button class="vjs-big-play-button">Play</button></div>
<script>
document.getElementById('yes').onclick=function(e){e.preventDefault();document.getElementById('gate').style.display='none';
document.getElementById('player').style.display='block';};
document.querySelector('.vjs-big-play-button').onclick=function(){fetch('/hls/index.m3u8').then(r=>r.text());};
</script></body></html>"""
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=ROOT, **k)
    def authed(self): return 'auth=ok' in (self.headers.get('Cookie') or '')
    def send_html(self, body, code=200, extra=None):
        self.send_response(code); self.send_header('Content-Type','text/html; charset=utf-8')
        for k,v in (extra or []): self.send_header(k,v)
        self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        p = urllib.parse.urlparse(self.path).path
        if p == '/Account/Login': return self.send_html(LOGIN)
        if p == '/movie.html': return self.send_html(VIDEO if self.authed() else LOGIN)
        if p.startswith('/hls/') and not self.authed():
            return self.send_html(b'forbidden', 403)
        return super().do_GET()
    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0); data = urllib.parse.parse_qs(self.rfile.read(n).decode())
        if data.get('Email',[''])[0] == 'user@test.ru' and data.get('Password',[''])[0] == 'secret':
            return self.send_html(b'<html><body>Welcome! <a href=/Account/Logout>Logout</a></body></html>', 200,
                                  [('Set-Cookie','auth=ok; Path=/')])
        return self.send_html(LOGIN)
http.server.ThreadingHTTPServer(('127.0.0.1', 8766), H).serve_forever()
