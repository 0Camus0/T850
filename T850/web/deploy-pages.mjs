import { spawn } from 'node:child_process';
import { access, mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';
import { cloudflareWranglerConfig, loadCloudflareConfig } from './cloudflare-config.mjs';

const webRoot = dirname(fileURLToPath(import.meta.url));
const sourceRoot = resolve(webRoot, '..');

function runNode(args, options) {
  return new Promise((resolveRun, reject) => {
    const child = spawn(process.execPath, args, { ...options, stdio: 'inherit', shell: false });
    child.once('error', () => reject(new Error('Could not start deployment tool')));
    child.once('exit', code => code === 0 ? resolveRun() : reject(new Error(`Deployment tool exited with code ${code}`)));
  });
}

export async function deployPages(configPath, { dryRun = false, dev = false, port = 8788, env = process.env, run = runNode } = {}) {
  configPath = resolve(configPath);
  for (const directory of ['Assets', 'web/previews', 'web/icons', 'web/minecraft-resources', 'build/pages', 'build/pages-minecraft', 'build/web/site']) {
    const inside = relative(join(sourceRoot, directory), configPath);
    if (!inside.startsWith('..') && !inside.includes(':')) throw new Error('Keep Cloudflare configuration outside published asset directories');
  }
  const config = await loadCloudflareConfig(configPath, { requireToken: !dev, env });
  const output = join(sourceRoot, config.minecraftOnly ? 'build/pages-minecraft' : 'build/pages');
  const settings = cloudflareWranglerConfig(config, output);
  if (dryRun) return { mode: dev ? 'dev' : 'deploy', projectName: config.projectName, branch: config.branch,
    output, bindings: (settings.r2_buckets ?? []).map(bucket => bucket.binding), credentialsIncluded: false };
  const wrangler = join(webRoot, 'node_modules/wrangler/bin/wrangler.js');
  try { await access(wrangler); await access(join(sourceRoot, 'build/web/site/DayScene.wasm')); }
  catch { throw new Error('Build the browser Release bundle and run npm ci in web before deployment'); }
  if (!Number.isInteger(port) || port < 1024 || port > 65535) throw new Error('Invalid local dev port');
  const childEnv = { ...env, CLOUDFLARE_ACCOUNT_ID: config.accountId, CLOUDFLARE_API_TOKEN: '',
    CLOUDFLARE_API_KEY: '', CLOUDFLARE_EMAIL: '', WRANGLER_LOG: 'info', CI: 'true' };
  await run([join(webRoot, 'prepare-pages.mjs'), '--config', configPath], { cwd: webRoot, env: childEnv });
  const uploads = JSON.parse(await readFile(join(sourceRoot, 'build/pages-r2', config.minecraftOnly ? 'minecraft-uploads.json' : 'uploads.json'), 'utf8'));
  if (!dev && uploads.length) throw new Error('Pending R2 uploads: see build/pages-r2/*uploads.json; populate the configured buckets before deploying');
  const scratch = join(sourceRoot, 'build/cloudflare');
  await mkdir(scratch, { recursive: true });
  const work = await mkdtemp(join(scratch, 'deploy-'));
  try {
    await writeFile(join(work, 'wrangler.jsonc'), JSON.stringify(settings, null, 2));
    const args = dev
      ? [wrangler, 'pages', 'dev', '--port', String(port), ...(config.minecraftOnly ? [] : ['--binding', 'ASSET_SOURCE=public-mirror'])]
      : [wrangler, 'pages', 'deploy', output, '--project-name', config.projectName, '--branch', config.branch, '--commit-dirty=true'];
    await run(args, { cwd: work, env: { ...childEnv, CLOUDFLARE_API_TOKEN: dev ? '' : config.apiToken } });
    return { mode: dev ? 'dev' : 'deploy', projectName: config.projectName, output, credentialsIncluded: false };
  } finally { await rm(work, { recursive: true, force: true }); }
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    const { values } = parseArgs({ options: { config: { type: 'string', default: join(webRoot, 'cloudflare.local.json') },
      'dry-run': { type: 'boolean', default: false }, dev: { type: 'boolean', default: false }, port: { type: 'string', default: '8788' } } });
    console.log(JSON.stringify(await deployPages(values.config, { dryRun: values['dry-run'], dev: values.dev, port: Number(values.port) }), null, 2));
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}