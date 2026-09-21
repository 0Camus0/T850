import { readFile } from 'node:fs/promises';

export function validateCloudflareConfig(input, { requireToken = false, env = process.env } = {}) {
  const fail = message => { throw new Error(message); };
  if (!input || typeof input !== 'object' || Array.isArray(input)) fail('Cloudflare config must be an object');
  if (!/^[a-z0-9][a-z0-9-]{0,57}$/.test(input.projectName ?? '')) fail('Invalid Cloudflare projectName');
  if (!/^[a-f0-9]{32}$/i.test(input.accountId ?? '')) fail('accountId must be a 32-character Cloudflare account ID');
  const branch = input.branch ?? 'main';
  if (typeof branch !== 'string' || !/^[a-zA-Z0-9][a-zA-Z0-9._/-]{0,127}$/.test(branch)) fail('Invalid deployment branch');
  if (input.minecraftOnly !== undefined && typeof input.minecraftOnly !== 'boolean') fail('minecraftOnly must be a boolean');
  const configuredBuckets = input.r2Buckets ?? [];
  if (!Array.isArray(configuredBuckets) || (configuredBuckets.length !== 2 && !(input.minecraftOnly !== false && configuredBuckets.length === 0)))
    fail('Configure both MODELS and TEXTURES R2 buckets for the multi-scene site');
  const bindings = new Set();
  const origins = new Set();
  const r2Buckets = configuredBuckets.map(bucket => {
    if (!bucket || !['MODELS', 'TEXTURES'].includes(bucket.binding) || bindings.has(bucket.binding)) fail('Invalid or duplicate R2 binding');
    if (!/^[a-z0-9][a-z0-9-]{1,61}[a-z0-9]$/.test(bucket.bucketName ?? '')) fail('Invalid R2 bucketName');
    let url;
    try { url = new URL(bucket.manifestUrl); } catch { fail('Invalid R2 manifestUrl'); }
    if (url.protocol !== 'https:' || url.username || url.password || url.search || url.hash || origins.has(url.origin))
      fail('R2 manifests must use distinct public HTTPS origins without credentials or query strings');
    bindings.add(bucket.binding);
    origins.add(url.origin);
    return { binding: bucket.binding, bucketName: bucket.bucketName, manifestUrl: url.href };
  });
  const apiToken = env.CLOUDFLARE_API_TOKEN ?? input.apiToken ?? '';
  if (typeof apiToken !== 'string' || /\s/.test(apiToken)) fail('Invalid Cloudflare API token format');
  if (requireToken && !apiToken) fail('Deployment requires CLOUDFLARE_API_TOKEN or apiToken in the ignored local config');
  return { projectName: input.projectName, accountId: input.accountId, branch,
    minecraftOnly: input.minecraftOnly ?? true, r2Buckets, apiToken };
}

export async function loadCloudflareConfig(path, options) {
  let contents;
  try { contents = await readFile(path, 'utf8'); }
  catch { throw new Error('Cannot read Cloudflare config; create cloudflare.local.json from the example'); }
  let input;
  try { input = JSON.parse(contents); }
  catch { throw new Error('Invalid JSON in Cloudflare config (contents withheld)'); }
  return validateCloudflareConfig(input, options);
}

export async function loadCloudResources(buckets, fetchManifest = fetch, includeResource = () => true) {
  const origins = new Map(buckets.map(bucket => [new URL(bucket.manifestUrl).origin, bucket]));
  const cloud = new Map();
  const names = new Set();
  for (const bucket of buckets) {
    const response = await fetchManifest(bucket.manifestUrl, { redirect: 'error' });
    if (!response.ok) throw new Error(`Manifest unavailable: ${response.status}`);
    const manifest = await response.json();
    for (const entry of manifest.assets ?? []) {
      const url = new URL(entry.url);
      const location = origins.get(url.origin);
      if (!location || url.username || url.password || url.search || url.hash) throw new Error('Manifest points outside the approved public R2 buckets');
      const resource = entry.localRelativePath ?? `${entry.kind === 'texture' ? 'Textures' : 'Models'}/${entry.key}`;
      if (!resource || resource.includes('\\') || resource.startsWith('/') || resource.split('/').some(part => part === '..' || part === '.'))
        throw new Error('Unsafe cloud resource path');
      if (location.binding !== bucket.binding) continue;
      if (!includeResource(resource)) continue;
      const folded = resource.toLowerCase();
      if (names.has(folded)) throw new Error(`Duplicate cloud resource: ${resource}`);
      names.add(folded);
      cloud.set(resource, {
        binding: location.binding, key: decodeURIComponent(url.pathname.slice(1)), url: url.href,
        contentType: entry.contentType ?? 'application/octet-stream', size: entry.size,
      });
    }
  }
  return cloud;
}

export function cloudflareWranglerConfig(config, output) {
  return { name: config.projectName,
    pages_build_output_dir: output, compatibility_date: '2026-09-16',
    ...(config.minecraftOnly ? {} : { r2_buckets: config.r2Buckets.map(bucket => ({ binding: bucket.binding, bucket_name: bucket.bucketName })) }) };
}