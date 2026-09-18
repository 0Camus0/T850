import { createServer } from 'node:http';
import { createReadStream, existsSync, readdirSync, statSync } from 'node:fs';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { createHash } from 'node:crypto';
import { execFile } from 'node:child_process';

const sourceRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const bundleRoot = existsSync(join(sourceRoot, 'web/site/DayScene.html')) ? join(sourceRoot, 'web') : join(sourceRoot, 'build/web');
const { values } = parseArgs({ options: {
  port: { type: 'string', default: '8765' },
  site: { type: 'string', default: join(bundleRoot, 'site') },
  assets: { type: 'string', default: existsSync(join(sourceRoot, 'web/assets')) ? join(sourceRoot, 'web/assets') : join(sourceRoot, 'Assets') },
  shaders: { type: 'string', default: join(bundleRoot, 'WebShaders') },
  open: { type: 'boolean', default: false },
  browser: { type: 'string' },
  query: { type: 'string', default: '' },
} });
if (values.browser && (!existsSync(values.browser) || !statSync(values.browser).isFile())) {
  throw new Error(`Browser executable missing: ${values.browser}`);
}
let port = Number(values.port);
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Invalid port');
const lastPort = Math.min(65535, port + 30);
for (const file of ['DayScene.html', 'DayScene.js', 'DayScene.wasm', 'scenes.json']) {
  if (!existsSync(join(values.site, file))) throw new Error(`Browser build missing: ${join(values.site, file)}. Run scripts/BuildWeb.ps1 or install the browser bundle.`);
}
function openBrowser(url) {
  const executable = values.browser ?? (process.platform === 'win32' ? 'rundll32.exe' : process.platform === 'darwin' ? 'open' : 'xdg-open');
  const argumentsList = !values.browser && process.platform === 'win32' ? ['url.dll,FileProtocolHandler', url] : [url];
  execFile(executable, argumentsList, error => {
    if (error) console.error(`Cannot open ${values.browser ?? 'the default browser'}: ${error.message}. Open ${url}`);
  });
}
function launchUrl() {
  const url = new URL(`http://127.0.0.1:${port}/`);
  url.search = new URLSearchParams(values.query).toString();
  return url.href;
}
const assets = new Map();
function catalog(directory, prefix = '') {
  if (!existsSync(directory)) throw new Error(`Missing asset directory: ${directory}`);
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    if (entry.name.startsWith('.')) continue;
    const resource = prefix + entry.name;
    const absolute = join(directory, entry.name);
    if (entry.isDirectory()) catalog(absolute, resource + '/');
    else if (entry.isFile()) assets.set(resource, absolute);
  }
}
catalog(values.assets);
catalog(values.shaders, 'WebShaders/');
const assetIndex = JSON.stringify([...assets.keys()].sort());
const identity = createHash('sha256').update(JSON.stringify([values.site, values.assets, values.shaders].map(path => resolve(path))) + assetIndex).digest('hex');
const mime = new Map([
  ['.html', 'text/html; charset=utf-8'], ['.js', 'text/javascript'],
  ['.wasm', 'application/wasm'], ['.json', 'application/json'],
  ['.png', 'image/png'], ['.jpg', 'image/jpeg'], ['.css', 'text/css'],
  ['.svg', 'image/svg+xml'],
]);
const server = createServer((request, response) => {
  response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  response.setHeader('Cross-Origin-Resource-Policy', 'same-origin');
  response.setHeader('Cache-Control', 'no-cache');
  if (request.method !== 'GET' && request.method !== 'HEAD') {
    response.writeHead(405).end();
    return;
  }
  try {
    const path = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
    if (path === '/__t850') {
      response.setHeader('Content-Type', 'application/json');
      response.end(request.method === 'HEAD' ? undefined : JSON.stringify({ identity }));
      return;
    }
    if (path === '/assets/index.json') {
      response.setHeader('Content-Type', 'application/json');
      response.end(request.method === 'HEAD' ? undefined : assetIndex);
      return;
    }
    const file = path.startsWith('/assets/') ? assets.get(path.slice('/assets/'.length)) :
      join(values.site, path === '/' ? 'DayScene.html' : path.slice(1));
    const siteRelative = path === '/' || (!path.includes('..') && !path.includes('\\'));
    if (!file || (!path.startsWith('/assets/') && !siteRelative) || !existsSync(file) || !statSync(file).isFile()) {
      response.writeHead(404).end('Not found');
      return;
    }
    response.setHeader('Content-Type', mime.get(extname(file)) ?? 'application/octet-stream');
    response.setHeader('Content-Length', statSync(file).size);
    if (request.method === 'HEAD') response.end();
    else createReadStream(file).pipe(response);
  } catch {
    response.writeHead(400).end('Invalid request');
  }
});
for (;;) {
  try {
    await new Promise((resolveListen, rejectListen) => {
      server.once('error', rejectListen);
      server.listen(port, '127.0.0.1', () => {
        server.removeListener('error', rejectListen);
        resolveListen();
      });
    });
    console.log(`T850 browser runtime: ${launchUrl()} (${assets.size} cataloged assets)`);
    if (values.open) openBrowser(launchUrl());
    break;
  } catch (error) {
    if (!values.open || error.code !== 'EADDRINUSE') throw error;
    try {
      const response = await fetch(`http://127.0.0.1:${port}/__t850`, { signal: AbortSignal.timeout(1000) });
      const existing = await response.json();
      if (existing.identity === identity) {
        console.log(`Reusing T850 browser runtime: ${launchUrl()}`);
        openBrowser(launchUrl());
        break;
      }
    } catch {}
    if (++port > lastPort) throw new Error(`No available T850 browser port (${values.port}-${lastPort}).`);
  }
}