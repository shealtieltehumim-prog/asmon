// Asmon update server. No dependencies.
//   GET  /api/latest          -> manifest JSON of the newest build
//   GET  /files/<name>        -> the published file
//   POST /api/publish         -> upload a new build (Authorization: Bearer $PUBLISH_TOKEN)
//        headers: X-Build (integer), X-Version, X-Notes (url-encoded), X-File-Name
//        body: raw bytes of the file (asmon.exe)
const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const PORT = process.env.PORT || 8080;
const DATA = process.env.DATA_DIR || '/data';
const TOKEN = process.env.PUBLISH_TOKEN || '';
const MAX = 200 * 1024 * 1024;
fs.mkdirSync(path.join(DATA, 'files'), { recursive: true });
const MANIFEST = path.join(DATA, 'latest.json');

function send(res, code, body, type) {
  res.writeHead(code, { 'Content-Type': type || 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body));
}
function safeEqual(a, b) {
  const x = Buffer.from(a), y = Buffer.from(b);
  return x.length === y.length && crypto.timingSafeEqual(x, y);
}

http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  if (req.method === 'GET' && url.pathname === '/api/latest') {
    if (!fs.existsSync(MANIFEST)) return send(res, 404, { error: 'no build published yet' });
    return send(res, 200, fs.readFileSync(MANIFEST));
  }
  if (req.method === 'GET' && url.pathname.startsWith('/files/')) {
    const name = path.basename(decodeURIComponent(url.pathname));
    const file = path.join(DATA, 'files', name);
    if (!fs.existsSync(file)) return send(res, 404, { error: 'not found' });
    res.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Content-Length': fs.statSync(file).size,
      'Cache-Control': 'no-store',
    });
    return fs.createReadStream(file).pipe(res);
  }
  if (req.method === 'POST' && url.pathname === '/api/publish') {
    const auth = req.headers['authorization'] || '';
    if (!TOKEN || !safeEqual(auth, 'Bearer ' + TOKEN)) return send(res, 401, { error: 'unauthorized' });
    const build = parseInt(req.headers['x-build'], 10);
    if (!Number.isInteger(build) || build < 1) return send(res, 400, { error: 'bad X-Build' });
    const version = String(req.headers['x-version'] || '');
    const notes = decodeURIComponent(String(req.headers['x-notes'] || ''));
    const fileName = path.basename(String(req.headers['x-file-name'] || 'asmon.exe'));
    const chunks = [];
    let size = 0;
    req.on('data', (c) => {
      size += c.length;
      if (size > MAX) { req.destroy(); return; }
      chunks.push(c);
    });
    req.on('end', () => {
      const buf = Buffer.concat(chunks);
      if (!buf.length) return send(res, 400, { error: 'empty body' });
      if (fs.existsSync(MANIFEST)) {
        const cur = JSON.parse(fs.readFileSync(MANIFEST, 'utf8'));
        if (build <= cur.build) return send(res, 409, { error: 'build must be newer', current: cur.build });
      }
      const stored = `build-${build}-${fileName}`;
      fs.writeFileSync(path.join(DATA, 'files', stored), buf);
      const manifest = {
        build, version, notes, file: fileName, size: buf.length,
        sha256: crypto.createHash('sha256').update(buf).digest('hex'),
        url: `/files/${stored}`,
        published: new Date().toISOString(),
      };
      fs.writeFileSync(MANIFEST + '.tmp', JSON.stringify(manifest));
      fs.renameSync(MANIFEST + '.tmp', MANIFEST);
      // keep the 3 newest files
      const all = fs.readdirSync(path.join(DATA, 'files'))
        .map((f) => ({ f, n: parseInt((f.match(/^build-(\d+)-/) || [])[1], 10) || 0 }))
        .sort((a, b) => b.n - a.n);
      for (const old of all.slice(3)) fs.unlinkSync(path.join(DATA, 'files', old.f));
      console.log('published build', build, manifest.sha256);
      send(res, 200, manifest);
    });
    return;
  }
  if (req.method === 'GET' && (url.pathname === '/' || url.pathname === '/health')) {
    return send(res, 200, 'Asmon update server', 'text/plain; charset=utf-8');
  }
  send(res, 404, { error: 'not found' });
}).listen(PORT, () => console.log('listening on', PORT));
