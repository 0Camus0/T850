import { cp, mkdir, readFile, readdir, rm, stat, writeFile } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { minecraftAssetSelection } from './minecraft-assets.mjs';
import { loadCloudflareConfig } from './cloudflare-config.mjs';

const { values } = parseArgs({ options: { config: { type: 'string' }, 'minecraft-only': { type: 'boolean' } } });
const webRoot = dirname(fileURLToPath(import.meta.url));
const config = await loadCloudflareConfig(resolve(values.config ?? join(webRoot, 'cloudflare.local.json')));
const minecraftOnly = values['minecraft-only'] ?? config.minecraftOnly;
const sourceRoot = resolve(webRoot, '..');
const output = join(sourceRoot, minecraftOnly ? 'build/pages-minecraft' : 'build/pages');
const payloads = join(sourceRoot, 'build/pages-r2');
const limit = 25 * 1024 * 1024;
const selection = minecraftOnly ? minecraftAssetSelection(JSON.parse(await readFile(join(sourceRoot, 'Assets/Scenes/Minecraft.t8scene'), 'utf8'))) : null;
const origins = new Map(config.r2Buckets.map(bucket => [new URL(bucket.manifestUrl).origin, bucket]));
const cloud = new Map();
for (const bucket of config.r2Buckets) {
  const response = await fetch(bucket.manifestUrl, { redirect: 'error' });
  if (!response.ok) throw new Error(`Manifest unavailable: ${response.status}`);
  const manifest = await response.json();
  for (const entry of manifest.assets ?? []) {
    const url = new URL(entry.url);
    const location = origins.get(url.origin);
    if (!location || url.username || url.password || url.search || url.hash) throw new Error('Manifest points outside the approved public R2 buckets');
    const resource = entry.localRelativePath ?? `${entry.kind === 'texture' ? 'Textures' : 'Models'}/${entry.key}`;
    if (!resource || resource.includes('\\') || resource.startsWith('/') || resource.split('/').some(part => part === '..' || part === '.'))
      throw new Error('Unsafe cloud resource path');
    cloud.set(resource, {
      binding: location.binding, key: decodeURIComponent(url.pathname.slice(1)), url: url.href,
      contentType: entry.contentType ?? 'application/octet-stream', size: entry.size,
    });
  }
}
await rm(output, { recursive: true, force: true });
await mkdir(output, { recursive: true });
await mkdir(payloads, { recursive: true });
const routes = Object.create(null);
const uploads = [];
const folded = new Map();
function register(resource, route) {
  const previous = folded.get(resource.toLowerCase());
  if (previous && previous !== resource) throw new Error(`Ambiguous resource: ${resource}`);
  folded.set(resource.toLowerCase(), resource);
  routes[resource] = route;
}
async function copyStatic(file, target) {
  if ((await stat(file)).size > limit) throw new Error(`Pages file exceeds 25 MiB: ${file}`);
  await mkdir(dirname(target), { recursive: true });
  await cp(file, target);
}
async function stageDirectory(directory, prefix = '') {
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    if (entry.name.startsWith('.')) continue;
    const file = join(directory, entry.name);
    const resource = prefix + entry.name;
    if (entry.isDirectory()) { await stageDirectory(file, resource + '/'); continue; }
    if (!entry.isFile()) continue;
    if (selection && !selection.includes(resource)) continue;
    const size = (await stat(file)).size;
    const remote = cloud.get(resource);
    if (remote) {
      if (remote.size && remote.size !== size) throw new Error(`Cloud/local size mismatch: ${resource}`);
      register(resource, remote);
    } else if (size > limit) {
      const hash = createHash('sha256');
      for await (const chunk of createReadStream(file)) hash.update(chunk);
      const digest = hash.digest('hex');
      const name = digest + extname(file);
      await cp(file, join(payloads, name));
      const key = `web-preview/${name}`;
      const binding = resource.startsWith('Textures/') ? 'TEXTURES' : 'MODELS';
      uploads.push({ resource, bucket: config.r2Buckets.find(bucket => bucket.binding === binding).bucketName, key, file: join(payloads, name), size });
      register(resource, { binding, key, size });
    } else {
      await copyStatic(file, join(output, 'assets', resource));
      register(resource, { static: true });
    }
  }
}
for (const file of ['DayScene.html', 'DayScene.js', 'DayScene.wasm', 'scenes.json', 'touch-controls.js'])
  await copyStatic(join(sourceRoot, 'build/web/site', file), join(output, file));
await cp(join(webRoot, 'icons'), join(output, 'icons'), { recursive: true });
if (minecraftOnly) {
  const catalog = JSON.parse(await readFile(join(output, 'scenes.json'), 'utf8'));
  catalog.scenes = catalog.scenes.filter(scene => scene.id === 6);
  if (catalog.scenes.length !== 1) throw new Error('Minecraft scene is missing from the catalog');
  catalog.defaultScene = 6;
  await writeFile(join(output, 'scenes.json'), JSON.stringify(catalog));
}
await stageDirectory(join(sourceRoot, 'Assets'));
await stageDirectory(join(sourceRoot, 'build/web/WebShaders'), 'WebShaders/');
if (selection) {
  for (const resource of selection.required) {
    if (!Object.hasOwn(routes, resource)) throw new Error(`Missing Minecraft dependency: ${resource}`);
  }
}
await writeFile(join(output, 'assets/index.json'), JSON.stringify(Object.keys(routes).sort()));
register('index.json', { static: true });
for (const [source, target] of [
  ['minecraft-wssi.html', 'index.html'], ['minecraft-wssi.html', 'minecraft-wssi.html'],
  ['minecraft-wssi.mjs', 'minecraft-wssi.mjs'],
  ...(minecraftOnly ? [] : [['launcher.html', 'launcher.html'], ['launcher.mjs', 'launcher.mjs']]),
])
  await copyStatic(join(webRoot, source), join(output, target));
if (minecraftOnly) {
  await copyStatic(join(webRoot, 'previews/minecraft-wssi.png'), join(output, 'previews/minecraft-wssi.png'));
} else {
  try { await cp(join(webRoot, 'previews'), join(output, 'previews'), { recursive: true }); }
  catch (error) { if (error.code !== 'ENOENT') throw error; }
}
await writeFile(join(output, '404.html'), '<!doctype html><title>Not found</title><a href="/">T850 scenes</a>');
await writeFile(join(output, '_headers'), '/*\n  Cross-Origin-Opener-Policy: same-origin\n  Cross-Origin-Embedder-Policy: require-corp\n  Cross-Origin-Resource-Policy: same-origin\n  X-Content-Type-Options: nosniff\n  Cache-Control: no-cache\n');
await writeFile(join(output, '_routes.json'), JSON.stringify({ version: 1, include: ['/assets/*'], exclude: [] }));
const handler = await readFile(join(webRoot, 'pages-worker.mjs'), 'utf8');
await writeFile(join(output, '_worker.js'), handler + '\nexport default { fetch: createAssetHandler(' + JSON.stringify(routes) + ') };\n');
await writeFile(join(payloads, minecraftOnly ? 'minecraft-uploads.json' : 'uploads.json'), JSON.stringify(uploads, null, 2));
console.log(JSON.stringify({ output, minecraftOnly, resources: Object.keys(routes).length - 1,
  existingR2: Object.values(routes).filter(route => route.url).length,
  pendingR2Uploads: uploads, credentialsIncluded: false }, null, 2));