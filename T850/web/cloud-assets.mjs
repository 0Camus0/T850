import { mkdir, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { loadCloudResources } from './cloudflare-config.mjs';

export const publicAssetManifests = Object.freeze([
  {
    binding: 'MODELS',
    manifestUrl: 'https://pub-2fa5c50bbfbc4b829da0d6c6300815b0.r2.dev/runtime_assets.json',
  },
  {
    binding: 'TEXTURES',
    manifestUrl: 'https://pub-ef5de729f9044220aa32f0601d99faa8.r2.dev/manifest.json',
  },
]);
const approvedOrigins = new Set(publicAssetManifests.map(entry => new URL(entry.manifestUrl).origin));

export function parseCloudAssetCatalog(input) {
  if (!input || input.version !== 1 || !input.routes || typeof input.routes !== 'object' || Array.isArray(input.routes))
    throw new Error('Invalid cloud asset catalog');
  const routes = new Map();
  const folded = new Set();
  for (const [resource, route] of Object.entries(input.routes)) {
    if (!resource || resource.includes('\\') || resource.startsWith('/') ||
        resource.split('/').some(part => !part || part === '.' || part === '..'))
      throw new Error('Unsafe cloud asset path');
    const normalized = resource.toLowerCase();
    if (folded.has(normalized)) throw new Error('Duplicate cloud asset path');
    folded.add(normalized);
    let url;
    try { url = new URL(route?.url); } catch { throw new Error('Invalid cloud asset URL'); }
    const loopback = url.protocol === 'http:' && ['127.0.0.1', 'localhost', '[::1]'].includes(url.hostname);
    if ((!loopback && url.protocol !== 'https:') || url.username || url.password || url.search || url.hash)
      throw new Error('Cloud asset URL must be public HTTPS');
    if (!loopback && !approvedOrigins.has(url.origin)) throw new Error('Cloud asset URL uses an unapproved origin');
    const size = route.size === undefined ? undefined : Number(route.size);
    if (size !== undefined && (!Number.isSafeInteger(size) || size < 0)) throw new Error('Invalid cloud asset size');
    const contentType = route.contentType ?? 'application/octet-stream';
    if (typeof contentType !== 'string' || !contentType || /[\r\n]/.test(contentType)) throw new Error('Invalid cloud asset content type');
    routes.set(resource, { url: url.href, contentType, size });
  }
  if (!routes.size) throw new Error('Cloud asset catalog is empty');
  return routes;
}

export async function createCloudAssetCatalog(fetchManifest = fetch) {
  const cloud = await loadCloudResources(publicAssetManifests, fetchManifest);
  const routes = Object.fromEntries([...cloud.entries()].sort(([left], [right]) => left.localeCompare(right))
    .map(([resource, route]) => [resource, { url: route.url, contentType: route.contentType, size: route.size }]));
  const catalog = { version: 1, manifests: publicAssetManifests.map(entry => entry.manifestUrl), routes };
  parseCloudAssetCatalog(catalog);
  return catalog;
}

const scriptPath = fileURLToPath(import.meta.url);
if (process.argv[1] && resolve(process.argv[1]) === resolve(scriptPath)) {
  const { values } = parseArgs({ options: {
    output: { type: 'string', default: resolve(dirname(scriptPath), '../build/web/CloudAssets/routes.json') },
  } });
  const catalog = await createCloudAssetCatalog();
  await mkdir(dirname(values.output), { recursive: true });
  await writeFile(values.output, JSON.stringify(catalog, null, 2));
  console.log(`Cloud asset catalog: ${Object.keys(catalog.routes).length} routes -> ${values.output}`);
}
