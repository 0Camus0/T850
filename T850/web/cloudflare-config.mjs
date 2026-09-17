import { readFile } from 'node:fs/promises';

export function validateCloudflareConfig(input, { requireToken = false, env = process.env } = {}) {
  const fail = message => { throw new Error(message); };
  if (!input || typeof input !== 'object' || Array.isArray(input)) fail('Cloudflare config must be an object');
  if (!/^[a-z0-9][a-z0-9-]{0,57}$/.test(input.projectName ?? '')) fail('Invalid Cloudflare projectName');
  if (!/^[a-f0-9]{32}$/i.test(input.accountId ?? '')) fail('accountId must be a 32-character Cloudflare account ID');
  const branch = input.branch ?? 'main';
  if (typeof branch !== 'string' || !/^[a-zA-Z0-9][a-zA-Z0-9._/-]{0,127}$/.test(branch)) fail('Invalid deployment branch');
  if (input.minecraftOnly !== undefined && typeof input.minecraftOnly !== 'boolean') fail('minecraftOnly must be a boolean');
  if (!Array.isArray(input.r2Buckets) || input.r2Buckets.length !== 2) fail('Configure both MODELS and TEXTURES R2 buckets');
  const bindings = new Set();
  const origins = new Set();
  const r2Buckets = input.r2Buckets.map(bucket => {
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

export function cloudflareWranglerConfig(config, output) {
  return { name: config.projectName, account_id: config.accountId,
    pages_build_output_dir: output, compatibility_date: '2026-09-16',
    r2_buckets: config.r2Buckets.map(bucket => ({ binding: bucket.binding, bucket_name: bucket.bucketName })) };
}