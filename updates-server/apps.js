// Extra apps hosted on the same update server, each under its own path prefix.
// Asmon keeps its original routes in server.js untouched; this module only answers
// requests that start with /<app>/.
//
// Per app (currently: apkstart):
//   GET  /<app>/api/latest        -> latest.json of the newest published build
//   GET  /<app>/api/history       -> last published builds (newest first)
//   GET  /<app>/files/<name>      -> a published file (HTTP Range supported, for parallel downloads)
//   PUT  /<app>/api/files/<name>  -> upload a file, raw body           (Authorization: Bearer <token>)
//   POST /<app>/api/release       -> publish a build (JSON)            (Authorization: Bearer <token>)
//        { build, version, notes, electronVersion, app, manifest, full }
//        app/manifest/full = names of files already uploaded with PUT; full is optional.
//
// Token: <APP>_PUBLISH_TOKEN if set (e.g. APKSTART_PUBLISH_TOKEN), else PUBLISH_TOKEN.
// Files: $DATA_DIR/apps/<app>/files. Files of the 3 newest builds are kept.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const APPS = ['apkstart'];
const DATA = process.env.DATA_DIR || '/data';
const MAX_UPLOAD = 600 * 1024 * 1024;
const KEEP = 3;

function send(res, code, body, headers) {
  res.writeHead(code, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
    ...(headers || {}),
  });
  res.end(typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body));
}

function tokenFor(app) {
  return process.env[`${app.toUpperCase()}_PUBLISH_TOKEN`] || process.env.PUBLISH_TOKEN || '';
}

function authorized(req, app) {
  const token = tokenFor(app);
  if (!token) return false;
  const got = Buffer.from(String(req.headers.authorization || ''));
  const want = Buffer.from(`Bearer ${token}`);
  return got.length === want.length && crypto.timingSafeEqual(got, want);
}

// plain file names only: no slashes, no leading dot
const safeName = (n) => /^[A-Za-z0-9._+-]{1,160}$/.test(n) && !n.startsWith('.');

function dirs(app) {
  const root = path.join(DATA, 'apps', app);
  const files = path.join(root, 'files');
  fs.mkdirSync(files, { recursive: true });
  return { root, files, latest: path.join(root, 'latest.json'), history: path.join(root, 'history.json') };
}

function readJson(p, def) {
  try { return JSON.parse(fs.readFileSync(p, 'utf8')); } catch (_) { return def; }
}

function sha256Of(p) {
  return new Promise((resolve, reject) => {
    const h = crypto.createHash('sha256');
    fs.createReadStream(p).on('data', (c) => h.update(c)).on('end', () => resolve(h.digest('hex'))).on('error', reject);
  });
}

function serveFile(req, res, d, name) {
  if (!safeName(name)) return send(res, 400, { error: 'bad name' });
  const p = path.join(d.files, name);
  if (!fs.existsSync(p)) return send(res, 404, { error: 'not found' });
  const size = fs.statSync(p).size;
  const base = { 'Content-Type': 'application/octet-stream', 'Accept-Ranges': 'bytes', 'Cache-Control': 'public, max-age=31536000, immutable' };
  const m = /^bytes=(\d*)-(\d*)$/.exec(String(req.headers.range || '').trim());
  if (m && (m[1] || m[2])) {
    let start = m[1] ? Number(m[1]) : Math.max(0, size - Number(m[2]));
    let end = m[1] && m[2] ? Number(m[2]) : size - 1;
    if (end >= size) end = size - 1;
    if (start > end || start >= size) {
      res.writeHead(416, { ...base, 'Content-Range': `bytes */${size}` });
      return res.end();
    }
    res.writeHead(206, { ...base, 'Content-Range': `bytes ${start}-${end}/${size}`, 'Content-Length': end - start + 1 });
    if (req.method === 'HEAD') return res.end();
    return fs.createReadStream(p, { start, end }).pipe(res);
  }
  res.writeHead(200, { ...base, 'Content-Length': size });
  if (req.method === 'HEAD') return res.end();
  return fs.createReadStream(p).pipe(res);
}

function upload(req, res, d, app, name) {
  if (!authorized(req, app)) return send(res, 401, { error: 'unauthorized' });
  if (!safeName(name)) return send(res, 400, { error: 'bad name' });
  const tmp = path.join(d.files, `.${name}.${Date.now()}.part`);
  const out = fs.createWriteStream(tmp);
  let size = 0;
  let failed = false;
  const fail = (code, msg) => {
    if (failed) return;
    failed = true;
    out.destroy();
    try { fs.rmSync(tmp, { force: true }); } catch (_) { /* ignore */ }
    send(res, code, { error: msg });
  };
  req.on('data', (c) => {
    size += c.length;
    if (size > MAX_UPLOAD) { fail(413, 'too large'); req.destroy(); }
  });
  req.on('error', () => fail(400, 'upload aborted'));
  req.pipe(out);
  out.on('error', () => fail(500, 'write failed'));
  out.on('finish', async () => {
    if (failed) return;
    if (!size) return fail(400, 'empty body');
    const dest = path.join(d.files, name);
    fs.renameSync(tmp, dest);
    const sha256 = await sha256Of(dest);
    console.log(`[${app}] uploaded`, name, size, sha256);
    send(res, 200, { name, size, sha256 });
  });
}

function readBody(req, limit) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', (c) => { size += c.length; if (size > limit) { reject(new Error('too large')); req.destroy(); } else chunks.push(c); });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

async function release(req, res, d, app) {
  if (!authorized(req, app)) return send(res, 401, { error: 'unauthorized' });
  let b;
  try { b = JSON.parse(await readBody(req, 64 * 1024)); } catch (_) { return send(res, 400, { error: 'bad json' }); }
  const build = Number(b.build);
  if (!Number.isInteger(build) || build < 1) return send(res, 400, { error: 'bad build' });
  const cur = readJson(d.latest, null);
  if (cur && build <= cur.build && !b.force) return send(res, 409, { error: 'build must be newer', current: cur.build });
  const describe = async (name, required) => {
    if (!name) { if (required) throw new Error('missing file name'); return null; }
    if (!safeName(String(name))) throw new Error(`bad file name ${name}`);
    const p = path.join(d.files, String(name));
    if (!fs.existsSync(p)) throw new Error(`file not uploaded: ${name}`);
    return { name: String(name), size: fs.statSync(p).size, sha256: await sha256Of(p), path: `/${app}/files/${encodeURIComponent(String(name))}` };
  };
  let appFile; let manifest; let full;
  try {
    appFile = await describe(b.app, true);
    manifest = await describe(b.manifest, true);
    full = await describe(b.full, false);
  } catch (e) {
    return send(res, 400, { error: e.message });
  }
  const latest = {
    schema: 1,
    app: app,
    build,
    version: String(b.version || ''),
    notes: String(b.notes || ''),
    electronVersion: String(b.electronVersion || ''),
    published: new Date().toISOString(),
    files: { app: appFile, manifest, full },
  };
  fs.writeFileSync(`${d.latest}.tmp`, JSON.stringify(latest, null, 2));
  fs.renameSync(`${d.latest}.tmp`, d.latest);
  const hist = [latest, ...readJson(d.history, []).filter((h) => h.build !== build)].slice(0, 20);
  fs.writeFileSync(d.history, JSON.stringify(hist, null, 2));
  const keep = new Set();
  for (const h of hist.slice(0, KEEP)) for (const f of Object.values(h.files || {})) if (f) keep.add(f.name);
  for (const f of fs.readdirSync(d.files)) {
    if (f.startsWith('.')) continue;
    if (!keep.has(f)) { try { fs.rmSync(path.join(d.files, f), { force: true }); console.log(`[${app}] pruned`, f); } catch (_) { /* ignore */ } }
  }
  console.log(`[${app}] released build`, build, latest.version);
  return send(res, 200, latest);
}

/** Returns true when the request belonged to one of the extra apps (and was answered). */
function handle(req, res, url) {
  const m = /^\/([a-z0-9-]+)(\/.*)?$/.exec(url.pathname);
  if (!m || !APPS.includes(m[1])) return false;
  const app = m[1];
  const rest = m[2] || '/';
  const d = dirs(app);
  (async () => {
    if (req.method === 'GET' && (rest === '/' || rest === '/health')) {
      return send(res, 200, `${app} update server: ok`, { 'Content-Type': 'text/plain; charset=utf-8' });
    }
    if (req.method === 'GET' && rest === '/api/latest') {
      if (!fs.existsSync(d.latest)) return send(res, 404, { error: 'no build published yet' });
      return send(res, 200, fs.readFileSync(d.latest));
    }
    if (req.method === 'GET' && rest === '/api/history') return send(res, 200, readJson(d.history, []));
    if ((req.method === 'GET' || req.method === 'HEAD') && rest.startsWith('/files/')) {
      return serveFile(req, res, d, path.basename(decodeURIComponent(rest.slice(7))));
    }
    if (req.method === 'PUT' && rest.startsWith('/api/files/')) {
      return upload(req, res, d, app, path.basename(decodeURIComponent(rest.slice(11))));
    }
    if (req.method === 'POST' && rest === '/api/release') return release(req, res, d, app);
    return send(res, 404, { error: 'not found' });
  })().catch((e) => {
    console.error(e);
    if (!res.headersSent) send(res, 500, { error: 'server error' });
  });
  return true;
}

module.exports = { handle, APPS };
