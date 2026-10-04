import assert from 'node:assert/strict';
import { test } from 'node:test';
import { readFile, readdir, access, mkdtemp, writeFile, rm } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { runInNewContext } from 'node:vm';
import { createAssetHandler } from './pages-worker.mjs';
import { minecraftAssetSelection, minecraftWebScene } from './minecraft-assets.mjs';
import { mountAssetBundle, loadAssetBundle } from './asset-bundle.mjs';
import { zipSync } from 'fflate';
import { cloudflareWranglerConfig, loadCloudResources, validateCloudflareConfig } from './cloudflare-config.mjs';
import { createCloudAssetCatalog, parseCloudAssetCatalog, publicAssetManifests } from './cloud-assets.mjs';
import { deployPages } from './deploy-pages.mjs';

test('Cloudflare deploy refuses absent credentials before any child process and dry-run does no deployment', async () => {
  const directory = await mkdtemp(join(tmpdir(), 't850-cloudflare-test-'));
  const path = join(directory, 'cloudflare.local.json');
  const input = { projectName: 'example-demo', accountId: '0'.repeat(32),
    r2Buckets: [
      { binding: 'MODELS', bucketName: 'example-models', manifestUrl: 'https://models.example.com/manifest.json' },
      { binding: 'TEXTURES', bucketName: 'example-textures', manifestUrl: 'https://textures.example.com/manifest.json' },
    ] };
  const run = async () => { assert.fail('No child process should run'); };
  try {
    await writeFile(path, JSON.stringify(input));
    await assert.rejects(deployPages(path, { env: {}, run }), /requires.*TOKEN/);
    const preview = await deployPages(path, { dryRun: true, env: { CLOUDFLARE_API_TOKEN: 'unit-test-secret' }, run });
    assert.equal(preview.projectName, 'example-demo');
    assert.equal(JSON.stringify(preview).includes('unit-test-secret'), false);
    await writeFile(path, '{"apiToken":"unit-test-secret", INVALID');
    await assert.rejects(deployPages(path, { env: {}, run }), error => error.message.includes('Invalid JSON') && !error.message.includes('unit-test-secret'));
  } finally { await rm(directory, { recursive: true, force: true }); }
});

test('Cloudflare deployment config requires explicit credentials and never serializes them for Wrangler', () => {
  const input = { projectName: 'example-demo', accountId: '0'.repeat(32),
    r2Buckets: [
      { binding: 'MODELS', bucketName: 'example-models', manifestUrl: 'https://models.example.com/manifest.json' },
      { binding: 'TEXTURES', bucketName: 'example-textures', manifestUrl: 'https://textures.example.com/manifest.json' },
    ] };
  assert.throws(() => validateCloudflareConfig(input, { requireToken: true, env: {} }), /requires.*TOKEN/);
  const config = validateCloudflareConfig({ ...input, apiToken: 'local-test-secret' },
    { requireToken: true, env: { CLOUDFLARE_API_TOKEN: 'env-test-secret' } });
  assert.equal(config.apiToken, 'env-test-secret');
  const wrangler = cloudflareWranglerConfig(config, '/build/pages-minecraft');
  assert.equal(JSON.stringify(wrangler).includes('test-secret'), false);
  assert.equal(Object.hasOwn(wrangler, 'apiToken'), false);
  assert.equal(Object.hasOwn(wrangler, 'account_id'), false);
  assert.equal(Object.hasOwn(wrangler, 'vars'), false);
  assert.equal(Object.hasOwn(wrangler, 'r2_buckets'), false);
  assert.equal(cloudflareWranglerConfig({ ...config, minecraftOnly: false }, '/build/pages').r2_buckets[1].bucket_name, 'example-textures');
  assert.deepEqual(validateCloudflareConfig({ ...input, r2Buckets: undefined }, { env: {} }).r2Buckets, []);
  assert.throws(() => validateCloudflareConfig({ ...input, minecraftOnly: false, r2Buckets: undefined }, { env: {} }), /both MODELS and TEXTURES/);
  assert.equal(config.minecraftOnly, true);
  for (const value of ['http://example.com/manifest.json', 'https://user:secret@example.com/manifest.json', 'https://example.com/manifest.json?token=secret']) {
    assert.throws(() => validateCloudflareConfig({ ...input, r2Buckets: [{ ...input.r2Buckets[0], manifestUrl: value }, input.r2Buckets[1]] }, { env: {} }), /public HTTPS/);
  }
  assert.throws(() => validateCloudflareConfig({ ...input, accountId: 'invalid' }, { env: {} }), /accountId/);
  assert.throws(() => validateCloudflareConfig({ ...input, r2Buckets: [input.r2Buckets[0], input.r2Buckets[0]] }, { env: {} }), /duplicate/);
});

test('cloud manifests reject duplicate resource paths before routes can be overwritten', async () => {
  const buckets = [
    { binding: 'MODELS', manifestUrl: 'https://models.example.com/manifest.json' },
    { binding: 'TEXTURES', manifestUrl: 'https://textures.example.com/manifest.json' },
  ];
  const model = { localRelativePath: 'Models/test.glb', url: 'https://models.example.com/test.glb', size: 4 };
  const texture = { kind: 'texture', key: 'test.png', url: 'https://textures.example.com/test.png' };
  const load = (assets, includeResource) => loadCloudResources(buckets, async (url, options) => {
    assert.equal(options.redirect, 'error');
    return { ok: true, json: async () => ({ assets: assets[buckets.findIndex(bucket => bucket.manifestUrl === url)] }) };
  }, includeResource);
  const routes = await load([[model], [texture]]);
  assert.equal(routes.size, 2);
  assert.equal(routes.get('Models/test.glb').binding, 'MODELS');
  assert.equal(routes.get('Textures/test.png').binding, 'TEXTURES');
  const mirrored = await load([[model, texture], [model, texture]]);
  assert.deepEqual([...mirrored.entries()], [...routes.entries()]);
  await assert.rejects(load([[model, model], []]), /Duplicate cloud resource/);
  const texturesOnly = resource => resource.startsWith('Textures/');
  const selected = await load([[model, model], [texture]], texturesOnly);
  assert.deepEqual([...selected.keys()], ['Textures/test.png']);
  await assert.rejects(load([[model, model], [texture, texture]], texturesOnly), /Duplicate cloud resource/);
  for (const path of ['Models/test.glb', 'models/TEST.glb']) {
    await assert.rejects(load([[model], [{ ...texture, localRelativePath: path }]]), /Duplicate cloud resource/);
  }
  await assert.rejects(load([[{ ...model, url: 'https://unapproved.example.com/test.glb' }], []]), /approved public R2/);
  await assert.rejects(load([[{ ...model, localRelativePath: '../private.glb' }], []]), /Unsafe cloud resource/);
});

test('Minecraft package rejects unrelated scene assets and retains declared dependencies', async () => {
  const scene = JSON.parse(await readFile(new URL('../Assets/Scenes/Minecraft.t8scene', import.meta.url), 'utf8'));
  const selection = minecraftAssetSelection(scene);
  for (const path of selection.required) assert.equal(selection.includes(path), true, path);
  assert.equal(selection.required.has(`Textures/${scene.voxel_world.mob.skin_texture}`), true);
  assert.throws(() => minecraftAssetSelection({ ...scene, voxel_world: {
    ...scene.voxel_world, mob: { skin_texture: '../private.png' },
  } }), /Unsafe Minecraft dependency/);
  for (const path of ['Models/DamagedHelmet.glb', 'Models/doomslayer.glb', 'Scenes/DayScene.t8scene',
    'Scenes/Q3/q3dm6_mod_3.t8scene', 'Textures/Terrain/HeightmapExample.bmp', 'model-cloud-manifest.json'])
    assert.equal(selection.includes(path), false, path);
  assert.equal(selection.includes('Textures/lens1.png'), true);
  assert.equal(selection.includes('WebShaders/mesh.json'), true);
  const webScene = minecraftWebScene(scene);
  assert.notEqual(webScene.voxel_world.environment_map, scene.voxel_world.environment_map);
  assert.deepEqual(webScene.voxel_world.environment_options, ['sky/CubeMap_SkyWater_512.dds']);
  const embedded = minecraftAssetSelection(webScene, { embedded: true });
  for (const path of ['Textures/sky/CubeMap_SkyWater.dds', 'Textures/sky/Ennis.dds',
    'Textures/LUT/lut_ggx.dds', 'Textures/GeneratedIBLCache/ggx_specular_cube_v1_old.t8ibl'])
    assert.equal(embedded.includes(path), false, path);
  assert.equal(embedded.required.has('Textures/sky/CubeMap_SkyWater_512.dds'), true);
});

test('bundled assets mount completely and reject missing, duplicate or unsafe files before writing', () => {
  const files = new Map();
  const filesystem = { mkdirTree() {}, writeFile(path, bytes) { files.set(path, bytes); } };
  const archive = (resources, contents) => zipSync({ 'index.json': Buffer.from(JSON.stringify(resources)), ...contents });
  const data = archive(['Textures/test.bin'], { 'Textures/test.bin': new Uint8Array([0, 255, 1, 128]) });
  assert.equal(mountAssetBundle(filesystem, data).resources, 1);
  assert.deepEqual([...files.get('/assets/Textures/test.bin')], [0, 255, 1, 128]);
  const noWrites = { mkdirTree() { assert.fail('Invalid archive must not write'); } };
  for (const path of ['../secret', '/absolute', 'Textures/../../secret', 'C:/secret', 'Textures\\test', 'Textures//test'])
    assert.throws(() => mountAssetBundle(noWrites, archive([path], { [path]: new Uint8Array() })), /Unsafe/);
  assert.throws(() => mountAssetBundle(noWrites, archive(['missing'], {})), /Missing bundled/);
  assert.throws(() => mountAssetBundle(noWrites, archive([], { extra: new Uint8Array() })), /Uncatalogued/);
  assert.throws(() => mountAssetBundle(noWrites, archive(['test', 'TEST'], { test: new Uint8Array(), TEST: new Uint8Array() })), /duplicate/);
});

test('offline web cubemap has six 512-square RGBA faces and a complete mip chain', async () => {
  const bytes = await readFile(new URL('./minecraft-resources/CubeMap_SkyWater_512.dds', import.meta.url));
  assert.equal(bytes.toString('ascii', 0, 4), 'DDS ');
  assert.equal(bytes.readUInt32LE(12), 512);
  assert.equal(bytes.readUInt32LE(16), 512);
  assert.equal(bytes.readUInt32LE(28), 10);
  assert.equal(bytes.readUInt32LE(80) & 4, 0);
  assert.equal(bytes.readUInt32LE(88), 32);
  assert.equal(bytes.readUInt32LE(112) & 0xfe00, 0xfe00);
  assert.equal(bytes.length, 128 + 6 * 4 * (4 ** 10 - 1) / 3);
  const directory = new URL('./minecraft-resources/GeneratedIBLCache/', import.meta.url);
  const caches = await readdir(directory);
  assert.equal(caches.length, 3);
  const kinds = new Set();
  for (const file of caches) {
    const cache = await readFile(new URL(file, directory));
    assert.equal(cache.toString('ascii', 0, 8), 'T8IBLF32');
    assert.equal(cache.readUInt32LE(8), 1);
    kinds.add(cache.readUInt32LE(12));
    assert.equal(cache.readUInt32LE(24), 6);
    assert.equal(Number(cache.readBigUInt64LE(40)) + 48, cache.length);
  }
  assert.equal(kinds.size, 3);
});

test('bundle download verifies content identity and never mounts failed or corrupt data', async context => {
  const archive = zipSync({ 'index.json': Buffer.from('[]') });
  const name = `minecraft-assets.${createHash('sha256').update(archive).digest('hex')}.zip`;
  let mounted = false;
  const filesystem = { mkdirTree() {}, writeFile() { mounted = true; } };
  const request = context.mock.method(globalThis, 'fetch', async url => {
    assert.equal(url, name);
    return new Response(archive);
  });
  assert.equal((await loadAssetBundle(filesystem, name)).resources, 0);
  assert.equal(mounted, true);
  mounted = false;
  request.mock.mockImplementation(async () => new Response(new Uint8Array([1, 2, 3])));
  await assert.rejects(loadAssetBundle(filesystem, name), /integrity/);
  request.mock.mockImplementation(async () => new Response('', { status: 404 }));
  await assert.rejects(loadAssetBundle(filesystem, name), /404/);
  await assert.rejects(loadAssetBundle(filesystem, '../other.zip'), /Invalid asset bundle/);
  assert.equal(mounted, false);
});

test('WSSI welcome validates enemies, starts only on Launch, and handles unavailable WebGPU', async () => {
  const html = await readFile(new URL('./minecraft-wssi.html', import.meta.url), 'utf8');
  assert.match(html, /<span id="demo-version">v0\.1\.11<\/span>/);
  assert.match(html, /<input id="console-logs" type="checkbox" autocomplete="off">/);
  const source = await readFile(new URL('./minecraft-wssi.mjs', import.meta.url), 'utf8');
  const createWelcome = async (gpu, search = '', mobile = false, isolated = true, storage = new Map()) => {
    const options = ['960x540', '1280x720', '1920x1080'].map(value => ({ value, selected: value === '1280x720' }));
    const portrait = { matches: mobile, addEventListener(name, callback) { this[name] = callback; } };
    const controls = {
      'launch-form': { addEventListener(name, callback) { this[name] = callback; }, reportValidity: () => true },
      enemies: { value: '1', get valueAsNumber() { return Number(this.value); } },
      resolution: { options, get value() { return options.find(option => option.selected)?.value ?? ''; },
        set value(value) { for (const option of options) option.selected = option.value === value; } },
      'console-logs': { checked: false }, launch: { disabled: true }, status: { dataset: {} },
      'saved-report': { hidden: true }, 'saved-report-text': {}, 'saved-report-feedback': {},
      'saved-report-download': { addEventListener(name, callback) { this[name] = callback; } },
    };
    const navigations = [];
    const downloads = [];
    const blobs = [];
    const events = {};
    const timers = [];
    const revoked = [];
    class BrowserURL extends URL {
      static createObjectURL(blob) { blobs.push(blob); return 'blob:test'; }
      static revokeObjectURL(url) { revoked.push(url); }
    }
    const sandbox = { URL: BrowserURL, URLSearchParams, Blob, setTimeout: callback => timers.push(callback),
      sessionStorage: { getItem: key => storage.get(key) }, crossOriginIsolated: isolated, navigator: { gpu },
      document: { getElementById: id => controls[id], body: { append() {} },
        createElement: () => ({ click() { downloads.push({ name: this.download, href: this.href }); }, remove() {} }) },
      window: { addEventListener(name, callback) { events[name] = callback; }, matchMedia: () => portrait },
      location: { href: 'https://test.pages.dev/minecraft-wssi.html', search, assign: url => navigations.push(url) } };
    await runInNewContext(`(async () => { ${source} })()`, sandbox);
    return { controls, navigations, portrait, downloads, blobs, events, timers, revoked,
      submit: () => controls['launch-form'].submit({ preventDefault() {} }) };
  };
  const gpu = { requestAdapter: async () => ({ limits: { maxColorAttachments: 8, maxColorAttachmentBytesPerSample: 128 } }) };
  for (const count of ['0', '8']) {
    const page = await createWelcome(gpu, `?minecraftEnemyCount=${count}&resolution=960x540`);
    assert.equal(page.controls.launch.disabled, false);
    assert.equal(page.controls['console-logs'].checked, false);
    assert.equal(page.controls['saved-report'].hidden, true);
    assert.equal(page.navigations.length, 0);
    page.submit();
    const target = new URL(page.navigations[0]);
    assert.equal(target.searchParams.get('scene'), '6');
    assert.equal(target.searchParams.get('demo'), 'wssi');
    assert.equal(target.searchParams.get('postProcessMode'), 'compute');
    assert.equal(target.searchParams.get('logLevel'), 'error');
    assert.equal(target.searchParams.get('minecraftEnemyCount'), count);
    assert.equal(target.searchParams.get('width'), '960');
    page.submit();
    assert.equal(page.navigations.length, 1);
  }
  const verbose = await createWelcome(gpu);
  verbose.controls['console-logs'].checked = true;
  verbose.submit();
  assert.equal(new URL(verbose.navigations[0]).searchParams.get('logLevel'), 'trace');
  const returned = await createWelcome(gpu, '?logLevel=trace');
  assert.equal(returned.controls['console-logs'].checked, true);
  returned.controls['console-logs'].checked = false;
  returned.submit();
  assert.equal(new URL(returned.navigations[0]).searchParams.get('logLevel'), 'error');
  const phone = await createWelcome(gpu, '?minecraftEnemyCount=7&resolution=720x1280', true);
  assert.equal(phone.controls.resolution.value, '720x1280');
  assert.deepEqual(phone.controls.resolution.options.map(option => option.value), ['540x960', '720x1280', '1080x1920']);
  phone.submit();
  assert.equal(new URL(phone.navigations[0]).searchParams.get('width'), '720');
  assert.equal(new URL(phone.navigations[0]).searchParams.get('height'), '1280');
  phone.portrait.matches = false;
  phone.portrait.change();
  assert.equal(phone.controls.resolution.value, '1280x720');
  assert.deepEqual(phone.controls.resolution.options.map(option => option.value), ['960x540', '1280x720', '1920x1080']);
  for (const count of ['-1', '9', '1.5', 'NaN', '']) {
    const page = await createWelcome(gpu);
    page.controls.enemies.value = count;
    page.submit();
    assert.equal(page.navigations.length, 0);
  }
  const unavailable = await createWelcome(undefined);
  unavailable.submit();
  assert.equal(unavailable.controls.launch.disabled, true);
  assert.equal(unavailable.navigations.length, 0);
  assert.match(unavailable.controls.status.textContent, /WebGPU is unavailable/);
  const embedded = await createWelcome({ requestAdapter() { assert.fail('Do not initialize GPU without isolation'); } }, '', true, false);
  embedded.submit();
  assert.equal(embedded.controls.launch.disabled, true);
  assert.equal(embedded.navigations.length, 0);
  assert.equal(embedded.controls.status.dataset.error, 'true');
  assert.match(embedded.controls.status.textContent, /standalone Safari, Chrome, Edge or Firefox/);
  const prior = { version: 1, capturedAt: '2026-09-18T00:00:00Z', state: 'running', frames: 300, wasmMemoryBytes: 268435456 };
  const storage = new Map([['t850:previous-session:v1', JSON.stringify(prior)],
    ['t850:last-runtime-error:v1', JSON.stringify({ message: '<script>old error</script>' })]]);
  const recovered = await createWelcome(undefined, '', true, false, storage);
  assert.equal(recovered.controls['saved-report'].hidden, false);
  assert.equal(recovered.controls.launch.disabled, true);
  assert.equal(recovered.navigations.length, 0);
  assert.match(recovered.controls['saved-report-text'].textContent, /<script>old error<\/script>/);
  recovered.controls['saved-report-download'].click();
  assert.deepEqual(recovered.downloads, [{ name: 't850-saved-diagnostics.json', href: 'blob:test' }]);
  assert.deepEqual(JSON.parse(await recovered.blobs[0].text()).previousSession, prior);
  for (const callback of recovered.timers) callback();
  assert.deepEqual(recovered.revoked, ['blob:test']);
  storage.set('t850:last-session:v1', JSON.stringify({ ...prior, frames: 600 }));
  recovered.events.pageshow();
  assert.equal(JSON.parse(recovered.controls['saved-report-text'].textContent).latestCheckpoint.frames, 600);
  const malformed = await createWelcome(gpu, '', false, true, new Map([
    ['t850:last-session:v1', '{invalid'], ['t850:previous-session:v1', 'x'.repeat(16000)], ['t850:last-runtime-error:v1', '{}'],
  ]));
  assert.equal(malformed.controls['saved-report'].hidden, true);
  assert.equal(malformed.controls.launch.disabled, false);
  const denied = await createWelcome(gpu, '', false, true, { get() { throw new Error('Storage denied'); } });
  assert.equal(denied.controls['saved-report'].hidden, true);
  assert.equal(denied.controls.launch.disabled, false);
});

test('WSSI runtime forwards validated enemy counts and stays on Minecraft', async () => {
  const html = await readFile(new URL('./shell.html', import.meta.url), 'utf8');
  const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const execute = async search => {
    let completed;
    const ready = new Promise(resolve => { completed = resolve; });
    const home = { setAttribute(name, value) { this[name] = value; }, querySelector: () => ({}) };
    const elements = { status: {}, 'runtime-error': { hidden: true }, 'runtime-error-title': {}, 'runtime-error-text': {},
      canvas: { style: { setProperty() {} }, clientWidth: 960, clientHeight: 540, addEventListener() {} },
      scene: { add() {}, addEventListener() {} } };
    const window = { addEventListener() {} };
    const sandbox = { URL, URLSearchParams, console: { log() {} }, window, navigator: { gpu: {} }, crossOriginIsolated: true,
      location: { search, href: `https://test.pages.dev/DayScene.html${search}` },
      document: { getElementById: id => elements[id], querySelector: () => home, body: { classList: { add() {} } } },
      Option: function () {}, fetch: async () => ({ ok: true, json: async () => ({ arguments: ['--logLevel', 'info'], defaultScene: 0, scenes: [{ id: 0, name: 'Sandbox' }, { id: 1, name: 'DayScene' }, { id: 6, name: 'Minecraft' }] }) }) };
    runInNewContext(source, sandbox);
    sandbox.Module.addRunDependency = () => {};
    sandbox.Module.removeRunDependency = completed;
    sandbox.Module.FS = { mkdirTree() {}, mount() {}, syncfs: (populate, done) => done(null) };
    sandbox.Module.preRun[0]();
    await ready;
    return { args: Array.from(sandbox.Module.arguments), diagnostics: window.t850, home, elements, title: sandbox.document.title };
  };
  for (const count of ['0', '8']) {
    const runtime = await execute(`?demo=wssi&scene=2&model=wrong.glb&sceneFile=wrong.t8scene&minecraftEnemyCount=${count}`);
    assert.equal(runtime.args[runtime.args.indexOf('--scene') + 1], '6');
    assert.equal(runtime.args[runtime.args.indexOf('--minecraftEnemyCount') + 1], count);
    assert.equal(runtime.args.includes('--model'), false);
    assert.equal(runtime.args.includes('--sceneFile'), false);
    assert.equal(runtime.args[runtime.args.indexOf('--postProcessMode') + 1], 'compute');
    assert.equal(runtime.elements.scene.hidden, true);
    assert.match(runtime.home.href, /^minecraft-wssi.html\?/);
    assert.equal(runtime.title, 'Hackathon 2026: WSSI Web GPU Minecraft Demo');
  }
  for (const count of ['-1', '9', '1.5', 'NaN', '']) {
    const runtime = await execute(`?demo=wssi&minecraftEnemyCount=${count}`);
    assert.equal(runtime.diagnostics.state, 'failed');
    assert.equal(runtime.args.includes('--minecraftEnemyCount'), false);
  }
  const launcher = await execute('?entry=launcher&scene=0');
  assert.equal(launcher.home.href, 'launcher.html');
  assert.equal(launcher.elements.scene.hidden, false);
  for (const level of ['error', 'trace']) {
    const runtime = await execute(`?demo=wssi&logLevel=${level}`);
    assert.equal(runtime.args[runtime.args.lastIndexOf('--logLevel') + 1], level);
    assert.equal(new URL(runtime.home.href, 'https://test.pages.dev/').searchParams.get('logLevel'), level);
  }
  for (const mode of ['raster', 'compute']) {
    const runtime = await execute(`?postProcessMode=${mode}&computeSelfTest`);
    assert.equal(runtime.args[runtime.args.indexOf('--postProcessMode') + 1], mode);
    assert.equal(runtime.args.includes('--compute-selftest'), true);
  }
  for (const mode of ['', 'invalid']) {
    assert.equal((await execute(`?postProcessMode=${mode}`)).diagnostics.state, 'failed');
  }
  const profiling = await execute('?scene=1&profile&profileFrames=1200&benchmark');
  assert.equal(profiling.args.includes('--profileCpuOnly'), true);
  assert.equal(profiling.args.includes('--profile'), false);
  assert.equal(profiling.args[profiling.args.indexOf('--benchmarkFrames') + 1], '1201');
  assert.equal(profiling.args.includes('--regressionFixedDt'), false);
  assert.equal((await execute('?scene=6&profile&benchmark')).diagnostics.state, 'failed');
  const offline = await execute('?scene=1&benchmarkNoPresent&benchmarkFrames=600&benchmarkHoldFrame=3000');
  assert.equal(offline.args.includes('--benchmarkNoPresent'), true);
  assert.equal(offline.args[offline.args.indexOf('--benchmarkFrames') + 1], '600');
  assert.equal(offline.args[offline.args.indexOf('--benchmarkHoldFrame') + 1], '3000');
  assert.equal((await execute('?scene=6&benchmarkNoPresent')).diagnostics.state, 'failed');
  for (const query of ['?scene=1&benchmarkNoPresent&benchmarkFrames=29', '?scene=1&benchmarkNoPresent&benchmarkHoldFrame=0'])
    assert.equal((await execute(query)).diagnostics.state, 'failed');
});

test('runtime errors show and retain the first failure across reloads without hiding current status', async () => {
  const html = await readFile(new URL('./shell.html', import.meta.url), 'utf8');
  const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const storage = new Map();
  const sessionStorage = { getItem: key => storage.get(key), setItem: (key, value) => storage.set(key, value) };
  const load = (sessionStorageOverride = sessionStorage) => {
    const elements = { status: {}, canvas: { addEventListener() {} }, 'runtime-error': { hidden: true },
      'runtime-error-title': {}, 'runtime-error-text': {} };
    const events = {};
    const window = { addEventListener: (name, callback) => { events[name] = callback; } };
    const sandbox = { window, sessionStorage: sessionStorageOverride,
      document: { getElementById: id => elements[id] }, console: { log() {} } };
    runInNewContext(source, sandbox);
    return { elements, events, sandbox, diagnostics: window.t850 };
  };
  const first = load();
  first.events['t850-runtime-ready']();
  first.diagnostics.frames = 123;
  first.diagnostics.renderSize = [1280, 720];
  first.sandbox.Module.print('Frame rendered');
  first.events.error({ message: 'memory access out of bounds', error: { stack: 'RuntimeError: memory access out of bounds\n  at engine.wasm:42' } });
  assert.equal(first.elements.status.textContent, 'Error');
  assert.equal(first.elements['runtime-error'].hidden, false);
  assert.match(first.elements['runtime-error-text'].textContent, /engine.wasm:42/);
  assert.equal(first.diagnostics.lastError.frames, 123);
  first.diagnostics.renderSize[0] = 500;
  assert.equal(first.diagnostics.lastError.renderSize[0], 1280);
  const stored = storage.get('t850:last-runtime-error:v1');
  for (let index = 0; index < 100; ++index) first.sandbox.Module.printErr('Follow-up error');
  first.events['t850-runtime-ready']();
  assert.equal(first.elements.status.textContent, 'Error');
  assert.equal(first.diagnostics.errors.length, 64);
  assert.equal(storage.get('t850:last-runtime-error:v1'), stored);
  const reloaded = load();
  assert.equal(reloaded.elements['runtime-error'].hidden, true);
  assert.equal(reloaded.diagnostics.previousError.frames, 123);
  reloaded.events['t850-runtime-ready']();
  assert.equal(reloaded.elements.status.textContent, 'Running');
  assert.equal(reloaded.elements['runtime-error'].hidden, true);
  reloaded.events.unhandledrejection({ reason: { stack: 'New failure stack' } });
  assert.equal(reloaded.diagnostics.lastError.message, 'New failure stack');
  assert.equal(reloaded.elements['runtime-error'].open, true);
  const unavailable = load({ getItem() { throw new Error('Storage denied'); }, setItem() { throw new Error('Storage denied'); } });
  unavailable.sandbox.Module.print('\x1b[1;31m[ERROR] GPU validation\x1b[0m');
  assert.equal(unavailable.diagnostics.state, 'failed');
  assert.equal(unavailable.diagnostics.lastError.message, '[ERROR] GPU validation');
  assert.equal(unavailable.elements['runtime-error'].hidden, false);
});

test('session checkpoints retain bounded reload evidence without displaying a historical error', async () => {
  const html = await readFile(new URL('./shell.html', import.meta.url), 'utf8');
  const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const key = 't850:last-session:v1';
  const storage = new Map();
  function load() {
    const elements = { status: { textContent: 'Loading' }, canvas: { addEventListener() {} }, 'runtime-error': { hidden: true },
      'runtime-error-title': {}, 'runtime-error-text': {} };
    const events = {};
    const timers = new Map();
    let timerId = 0;
    const window = { addEventListener(name, callback) { events[name] = callback; },
      setInterval(callback, delay) { assert.equal(delay, 2000); timers.set(++timerId, callback); return timerId; },
      clearInterval(id) { timers.delete(id); } };
    const sandbox = { window, document: { visibilityState: 'visible', getElementById: id => elements[id] },
      sessionStorage: { getItem: key => storage.get(key), setItem: (key, value) => storage.set(key, value) }, console: { log() {} } };
    runInNewContext(source, sandbox);
    return { sandbox, events, timers, elements, diagnostics: window.t850 };
  }
  const first = load();
  assert.equal(first.timers.size, 1);
  first.events.pageshow();
  assert.equal(first.timers.size, 1);
  first.events['t850-runtime-ready']();
  Object.assign(first.diagnostics, { frames: 300, memoryBytes: 268435456, renderSize: [390, 600] });
  for (let index = 0; index < 12; index++) first.sandbox.Module.print('x'.repeat(2000));
  for (const callback of first.timers.values()) callback();
  const checkpoint = JSON.parse(storage.get(key));
  assert.equal(checkpoint.frames, 300);
  assert.equal(checkpoint.wasmMemoryBytes, 268435456);
  assert.equal(checkpoint.lifecycle, 'active');
  assert.equal(checkpoint.recentLogs.length, 8);
  assert.ok(checkpoint.recentLogs.every(line => line.length === 512));
  assert.ok(storage.get(key).length < 16000);
  const restored = load();
  assert.equal(restored.diagnostics.previousSession.frames, 300);
  assert.equal(JSON.parse(storage.get('t850:previous-session:v1')).frames, 300);
  const restartedDuringLoading = load();
  assert.equal(restartedDuringLoading.diagnostics.previousSession.frames, 300);
  assert.equal(restartedDuringLoading.elements['runtime-error'].hidden, true);
  assert.equal(restored.diagnostics.errors.length, 0);
  assert.equal(restored.elements['runtime-error'].hidden, true);
  restored.events.pagehide();
  assert.equal(restored.timers.size, 0);
  assert.equal(JSON.parse(storage.get(key)).lifecycle, 'pagehide');
  restored.events.pageshow();
  assert.equal(restored.timers.size, 1);
  restored.sandbox.Module.onAbort('GPU process failed');
  assert.equal(JSON.parse(storage.get(key)).lifecycle, 'error');
  assert.equal(restored.elements['runtime-error'].hidden, false);
  storage.set(key, '{invalid JSON');
  assert.equal(load().diagnostics.previousSession.frames, 300);
  storage.clear();
  storage.set(key, '{invalid JSON');
  assert.equal(load().diagnostics.previousSession, undefined);
});

test('Wasm streaming fallback stays recoverable without suppressing actual startup or runtime failures', async () => {
  const html = await readFile(new URL('./shell.html', import.meta.url), 'utf8');
  const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  function load() {
    const elements = { status: {}, canvas: { addEventListener() {} }, 'runtime-error': { hidden: true },
      'runtime-error-title': {}, 'runtime-error-text': {} };
    const events = {};
    const storage = new Map();
    const sandbox = { window: { addEventListener(name, callback) { events[name] = callback; } },
      sessionStorage: { getItem: key => storage.get(key), setItem: (key, value) => storage.set(key, value) },
      document: { getElementById: id => elements[id] }, console: { log() {} } };
    runInNewContext(source, sandbox);
    return { module: sandbox.Module, diagnostics: sandbox.window.t850, elements, events, storage };
  }
  const warning = 'wasm streaming compile failed: TypeError: Load failed';
  const recovered = load();
  recovered.module.printErr(warning);
  recovered.module.printErr('falling back to ArrayBuffer instantiation');
  assert.equal(recovered.diagnostics.state, 'loading');
  assert.equal(recovered.diagnostics.wasmStreamingFallback, true);
  assert.equal(recovered.diagnostics.errors.length, 0);
  assert.equal(recovered.storage.size, 0);
  assert.equal(recovered.elements['runtime-error'].hidden, true);
  assert.equal(recovered.diagnostics.logs[0], warning);
  recovered.module.onRuntimeInitialized();
  recovered.events['t850-runtime-ready']();
  assert.equal(recovered.diagnostics.state, 'running');
  for (const failure of ['failed to asynchronously prepare wasm: TypeError: Load failed', 'Unexpected loader failure']) {
    const failed = load();
    failed.module.printErr(warning);
    failed.module.printErr(failure);
    assert.equal(failed.diagnostics.lastError.message, failure);
    assert.equal(failed.elements['runtime-error'].open, true);
    assert.equal(failed.diagnostics.lastError.recentLogs[0], warning);
  }
  const aborted = load();
  aborted.module.printErr(warning);
  aborted.module.onAbort('ArrayBuffer load failed');
  assert.equal(aborted.diagnostics.state, 'failed');
  for (const initialized of [false, true]) {
    const failed = load();
    if (initialized) failed.module.onRuntimeInitialized();
    else failed.events['t850-runtime-ready']();
    failed.module.printErr(warning);
    assert.equal(failed.diagnostics.lastError.message, warning);
  }
  const uncaught = load();
  uncaught.events.error({ message: warning });
  assert.equal(uncaught.diagnostics.lastError.message, warning);
});

test('on-page console captures startup output, bounds history and exports without developer tools', async () => {
  const html = await readFile(new URL('./shell.html', import.meta.url), 'utf8');
  const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  function load(search, previousError) {
    const elements = Object.fromEntries(['status', 'canvas', 'runtime-error', 'runtime-error-title', 'runtime-error-text',
      'runtime-console', 'console-follow', 'console-output', 'console-count', 'console-feedback', 'console-clear', 'console-copy', 'console-download', 'console-previous-error', 'console-previous-error-text']
      .map(id => [id, { hidden: true, checked: id === 'console-follow', handlers: {}, textContent: '', scrollTop: 0, scrollHeight: 1000, clientHeight: 100,
        addEventListener(name, callback) { this.handlers[name] = callback; } }]));
    const timers = new Map();
    let timerId = 0;
    const copied = [];
    const downloads = [];
    const blobs = [];
    const revoked = [];
    const browserLogs = [];
    const navigator = { userAgent: 'Unit-test mobile browser', clipboard: { async writeText(text) { copied.push(text); } } };
    const document = { getElementById: id => elements[id],
      body: { classList: { add() {} }, append() {} },
      createElement: () => ({ click() { downloads.push({ name: this.download, href: this.href }); }, remove() {} }) };
    const sandbox = { window: { addEventListener() {} }, document, navigator, URLSearchParams, location: { search }, Blob,
      sessionStorage: { getItem: () => previousError ? JSON.stringify(previousError) : null, setItem() {} },
      URL: { createObjectURL(blob) { blobs.push(blob); return 'blob:test'; }, revokeObjectURL(url) { revoked.push(url); } },
      console: { log(text) { browserLogs.push(text); } },
      setTimeout(callback) { timers.set(++timerId, callback); return timerId; }, clearTimeout(id) { timers.delete(id); } };
    runInNewContext(source, sandbox);
    const flush = () => { const pending = [...timers.values()]; timers.clear(); for (const callback of pending) callback(); };
    return { elements, sandbox, timers, copied, downloads, blobs, revoked, browserLogs, navigator, flush };
  }
  const previousError = { message: 'Old failure', capturedAt: '2026-09-18T00:00:00Z' };
  const disabled = load('?logLevel=error', previousError);
  disabled.sandbox.Module.print('Engine message');
  assert.equal(disabled.elements['runtime-console'].hidden, true);
  assert.equal(disabled.elements['runtime-error'].hidden, true);
  assert.equal(disabled.elements['console-previous-error'].hidden, true);
  assert.equal(disabled.timers.size, 0);
  const page = load('?logLevel=trace', previousError);
  assert.equal(page.elements['runtime-console'].hidden, false);
  assert.equal(page.elements['runtime-error'].hidden, true);
  assert.equal(page.elements['console-previous-error'].hidden, false);
  assert.match(page.elements['console-previous-error-text'].textContent, /Old failure/);
  page.sandbox.Module.print('Startup <script>not markup</script>');
  page.sandbox.Module.print('Loading shader');
  assert.equal(page.timers.size, 1);
  page.flush();
  assert.match(page.elements['console-output'].textContent, /Startup <script>not markup<\/script>\nLoading shader/);
  assert.equal(page.elements['console-output'].scrollTop, 1000);
  page.elements['console-follow'].checked = false;
  page.elements['console-output'].scrollTop = 50;
  for (let index = 0; index < 4005; index++) page.sandbox.Module.print(`Message ${index}`);
  page.flush();
  assert.equal(page.sandbox.window.t850.logs.length, 4000);
  assert.equal(page.elements['console-output'].scrollTop, 50);
  assert.equal(page.browserLogs.length, 4007);
  page.sandbox.Module.printErr('Last engine failure');
  assert.match(page.elements['console-output'].textContent, /Last engine failure$/);
  assert.equal(page.timers.size, 0);
  await page.elements['console-copy'].handlers.click();
  assert.match(page.copied[0], /State: failed/);
  assert.match(page.copied[0], /Saved report from previous run:[\s\S]*Old failure/);
  assert.match(page.copied[0], /Last engine failure$/);
  page.navigator.clipboard.writeText = async () => { throw new Error('Denied'); };
  await page.elements['console-copy'].handlers.click();
  assert.equal(page.elements['console-feedback'].textContent, 'Clipboard unavailable');
  assert.equal(page.sandbox.window.t850.errors.length, 1);
  page.elements['console-download'].handlers.click();
  assert.deepEqual(page.downloads, [{ name: 't850-engine-logs.txt', href: 'blob:test' }]);
  assert.match(await page.blobs[0].text(), /Last engine failure$/);
  page.flush();
  assert.deepEqual(page.revoked, ['blob:test']);
  page.elements['console-clear'].handlers.click();
  assert.equal(page.elements['console-output'].textContent, '');
  assert.equal(page.sandbox.window.t850.logs.length, 0);
  assert.equal(page.sandbox.window.t850.errors.length, 1);
  page.sandbox.Module.print('After clear');
  page.flush();
  assert.equal(page.elements['console-output'].textContent, 'After clear');
});

test('pointer-lock rejection is recoverable at the request boundary without suppressing unrelated failures', async () => {
  const html = await readFile(new URL('./shell.html', import.meta.url), 'utf8');
  const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const elements = { status: {}, 'runtime-error': { hidden: true }, 'runtime-error-title': {}, 'runtime-error-text': {} };
  const events = {};
  const documentEvents = {};
  let request;
  let receivedOptions;
  elements.canvas = { addEventListener() {}, requestPointerLock(...options) {
    assert.equal(this, elements.canvas);
    receivedOptions = options;
    return request();
  } };
  const window = { addEventListener(name, callback) { events[name] = callback; } };
  const document = { getElementById: id => elements[id], addEventListener(name, callback) { documentEvents[name] = callback; } };
  runInNewContext(source, { window, document, console: { log() {} } });
  events['t850-runtime-ready']();
  for (const name of ['WrongDocumentError', 'NotAllowedError', 'SecurityError', 'NotSupportedError', 'InvalidStateError', 'AbortError']) {
    const error = Object.assign(new Error('Pointer lock rejected'), { name });
    request = () => Promise.reject(error);
    await elements.canvas.requestPointerLock({ unadjustedMovement: true });
    assert.equal(receivedOptions[0].unadjustedMovement, true);
    assert.equal(window.t850.pointerLock.reason, name);
    assert.equal(window.t850.state, 'running');
    assert.equal(window.t850.errors.length, 0);
    assert.equal(elements['runtime-error'].hidden, true);
    request = () => { throw error; };
    elements.canvas.requestPointerLock();
    assert.equal(window.t850.state, 'running');
  }
  request = () => Promise.resolve();
  await elements.canvas.requestPointerLock();
  document.pointerLockElement = elements.canvas;
  documentEvents.pointerlockchange();
  assert.equal(window.t850.pointerLock.state, 'locked');
  document.pointerLockElement = null;
  documentEvents.pointerlockchange();
  assert.equal(window.t850.pointerLock.state, 'unlocked');
  request = () => undefined;
  assert.equal(elements.canvas.requestPointerLock(), undefined);
  request = () => Promise.reject(new TypeError('Unexpected implementation error'));
  await assert.rejects(elements.canvas.requestPointerLock(), /Unexpected implementation error/);
  request = () => { throw new TypeError('Unexpected synchronous error'); };
  assert.throws(() => elements.canvas.requestPointerLock(), /Unexpected synchronous error/);
  events.unhandledrejection({ reason: new Error('Unrelated rendering failure') });
  assert.equal(window.t850.state, 'failed');
  assert.match(window.t850.lastError.message, /Unrelated rendering failure/);
});

test('browser defaults select the authored launcher scenes and Doomslayer', async () => {
  const catalog = JSON.parse(await readFile(new URL('./scenes.json', import.meta.url), 'utf8'));
  const quake = catalog.scenes.find(scene => scene.id === 2);
  const ragdoll = catalog.scenes.find(scene => scene.id === 3);
  const template = catalog.scenes.find(scene => scene.id === 4);
  assert.equal(quake.sceneFile, 'Scenes/Q3/q3dm6_mod_3.t8scene');
  assert.equal(template.sceneFile, 'Scenes/Q3/q3dm6_mod_3_jolt.t8scene');
  assert.deepEqual(ragdoll.arguments, ['--model', 'Models/doomslayer_cine_all_animations_no-damage_double-sided-opaque.glb', '--sceneProfile', 'pc/x64']);
  for (const scene of [quake, ragdoll, template]) {
    assert.equal(scene.arguments[scene.arguments.indexOf('--sceneProfile') + 1], 'pc/x64');
  }
  assert.equal(quake.arguments?.includes('--model') ?? false, false);
  for (const scene of [quake, template]) {
    await access(new URL('../Assets/' + scene.sceneFile, import.meta.url));
    for (const launcher of ['Launcher.ps1', 'Launcher_Release.ps1']) {
      const script = await readFile(new URL('../scripts/' + launcher, import.meta.url), 'utf8');
      const suffix = scene.sceneFile.slice('Scenes/'.length).replaceAll('/', '\\');
      assert.ok(script.includes(`"${scene.id}" { return "${suffix}" }`), `${launcher} has a different scene ${scene.id} default`);
    }
  }
});

test('local public mirror requests the approved URL without following redirects', async context => {
  const handler = createAssetHandler({ 'Models/test.glb': { url: 'https://approved.example/test.glb' } });
  context.mock.method(globalThis, 'fetch', async (url, options) => {
    assert.equal(url, 'https://approved.example/test.glb');
    assert.equal(options.redirect, 'manual');
    return new Response('model');
  });
  const response = await handler(new Request('http://localhost/assets/Models/test.glb'), { ASSET_SOURCE: 'public-mirror' });
  assert.equal(await response.text(), 'model');
  assert.equal(response.headers.get('Cross-Origin-Resource-Policy'), 'same-origin');
});

test('Pages assets use only manifest keys, stream R2, and preserve isolation', async () => {
  const calls = [];
  const handler = createAssetHandler({
    'Models/Test & model.glb': { binding: 'MODELS', key: 'Test & model.glb', contentType: 'model/gltf-binary' },
    'Scenes/Test.t8scene': { static: true },
  });
  const object = () => ({ body: new ReadableStream({ start(controller) { controller.enqueue(new TextEncoder().encode('model')); controller.close(); } }), size: 5, httpEtag: '"version1"' });
  const env = {
    MODELS: { get: async key => { calls.push(key); return object(); }, head: async key => { calls.push(key); return object(); } },
    ASSETS: { fetch: async () => new Response('static') },
  };
  const request = (path, options) => new Request('https://test.pages.dev' + path, options);
  const response = await handler(request('/assets/Models/Test%20%26%20model.glb'), env);
  assert.equal(await response.text(), 'model');
  assert.equal(response.headers.get('Content-Type'), 'model/gltf-binary');
  assert.equal(response.headers.get('Cross-Origin-Embedder-Policy'), 'require-corp');
  assert.deepEqual(calls, ['Test & model.glb']);
  const head = await handler(request('/assets/Models/Test%20%26%20model.glb', { method: 'HEAD' }), env);
  assert.equal(await head.text(), '');
  assert.equal(head.headers.get('Content-Length'), '5');
  const cached = await handler(request('/assets/Models/Test%20%26%20model.glb', { headers: { 'If-None-Match': '"version1"' } }), env);
  assert.equal(cached.status, 304);
  assert.equal(await (await handler(request('/assets/Scenes/Test.t8scene'), env)).text(), 'static');
  assert.equal((await handler(request('/assets/unlisted.glb'), env)).status, 404);
  assert.equal((await handler(request('/assets/__proto__'), env)).status, 404);
  assert.equal((await handler(request('/assets/%2e%2e%2fsecret'), env)).status, 400);
  assert.equal((await handler(request('/assets/Models/Test%20%26%20model.glb', { method: 'POST' }), env)).status, 405);
  assert.equal((await handler(request('/assets/Models/Test%20%26%20model.glb'), { ASSETS: env.ASSETS })).status, 502);
});

test('touch gamepad defaults to mobile devices, preserves manual choice and handles multitouch safely', async () => {
  const source = await readFile(new URL('./touch-controls.js', import.meta.url), 'utf8');
  function load(touchPoints, device = { userAgent: 'Mozilla/5.0 (Linux; Android 14)', platform: 'Linux armv8l' }, coarsePointer = false) {
    const makeElement = dataset => ({ dataset, handlers: {}, captured: new Set(), hidden: true, style: { setProperty() {} },
      classList: { values: new Set(), add(name) { this.values.add(name); }, remove(name) { this.values.delete(name); }, contains(name) { return this.values.has(name); } },
      addEventListener(name, handler) { this.handlers[name] = handler; },
      hasAttribute(name) { return name === 'data-axis' && dataset.axis !== undefined; },
      getBoundingClientRect: () => ({ left: 0, top: 0, width: 120, height: 120 }),
      setPointerCapture(id) { this.captured.add(id); }, hasPointerCapture(id) { return this.captured.has(id); },
      releasePointerCapture(id) { this.captured.delete(id); },
    });
    const move = makeElement({ axis: '0' });
    const look = makeElement({ axis: '2' });
    const jump = makeElement({ button: '1' });
    const controls = [move, look, jump];
    const panel = { hidden: true, querySelectorAll: selector => selector === '.touch-stick' ? [move, look] : controls };
    const toggle = makeElement({});
    const label = { hidden: true };
    const onTop = { ...makeElement({}), checked: false };
    const onTopLabel = { hidden: true };
    const cameraControls = { hidden: true };
    const commands = ['1', '2', '1', '2'].map(command => ({ ...makeElement({ command }), setAttribute(name, value) { this[name] = value; } }));
    const classes = new Set();
    const events = {};
    const window = { t850: { scene: 'Minecraft' }, resizeEvents: 0,
      addEventListener: (name, callback) => { events[name] = callback; },
      dispatchEvent(event) { if (event.type === 'resize') ++this.resizeEvents; events[event.type]?.(event); } };
    const document = { hidden: false, body: { classList: { toggle(name, active) { if (active) classes.add(name); else classes.delete(name); } } }, addEventListener: (name, callback) => { events[name] = callback; },
      querySelectorAll: () => commands,
      getElementById: id => ({ 'touch-controls': panel, 'touch-enabled': toggle, 'touch-toggle': label,
        'touch-ontop': onTop, 'touch-ontop-toggle': onTopLabel, 'camera-controls': cameraControls })[id] };
    const media = { matches: coarsePointer, addEventListener(name, callback) { this[name] = callback; } };
    runInNewContext(source, { window, document, navigator: { maxTouchPoints: touchPoints, ...device }, Atomics, Event,
      matchMedia: () => media });
    const memory = new Int32Array(new SharedArrayBuffer(32));
    window.t850Touch.attach(memory);
    events['t850-runtime-ready']();
    const send = (element, name, id, clientX = 60, clientY = 60) => element.handlers[name]({ pointerId: id, button: 0, clientX, clientY, preventDefault() {}, stopPropagation() {} });
    return { move, look, jump, panel, toggle, label, onTop, onTopLabel, cameraControls, commands, classes, events, memory, window, send, media };
  }
  for (const device of [
    { userAgent: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)', platform: 'Win32', userAgentData: { platform: 'Windows', mobile: false } },
    { userAgent: 'Mozilla/5.0 (Windows NT 10.0; ARM64)', platform: 'Win32' },
    { userAgent: 'Mozilla/5.0 (X11; Linux x86_64)', platform: 'Linux x86_64' },
    { userAgent: 'Mozilla/5.0 (X11; CrOS x86_64)', platform: 'Linux x86_64' },
    { userAgent: '', platform: '' },
  ]) {
    const laptop = load(10, device, true);
    assert.equal(laptop.panel.hidden, true);
    assert.equal(laptop.label.hidden, false);
    assert.equal(laptop.cameraControls.hidden, false);
    laptop.events.pointerdown({ pointerType: 'touch' });
    assert.equal(laptop.memory[0], 0);
    laptop.toggle.checked = true;
    laptop.toggle.handlers.change();
    laptop.media.change();
    assert.equal(laptop.panel.hidden, false);
    assert.equal(laptop.memory[0], 1);
  }
  for (const device of [
    { userAgent: 'Mozilla/5.0 (Linux; Android 14; Pixel) Mobile', platform: 'Linux armv8l', userAgentData: { platform: 'Android', mobile: true } },
    { userAgent: 'Mozilla/5.0 (Linux; Android 14; Tablet)', platform: 'Linux armv8l', userAgentData: { platform: 'Android', mobile: false } },
    { userAgent: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X)', platform: 'iPhone' },
    { userAgent: 'Mozilla/5.0 (iPad; CPU OS 18_0 like Mac OS X)', platform: 'iPad' },
    { userAgent: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15)', platform: 'MacIntel' },
    { userAgent: '', platform: '', userAgentData: { mobile: true } },
  ]) {
    const mobile = load(5, device);
    assert.equal(mobile.panel.hidden, false);
    mobile.toggle.checked = false;
    mobile.toggle.handlers.change();
    mobile.media.change();
    mobile.events.pointerdown({ pointerType: 'touch' });
    mobile.events['t850-runtime-ready']();
    assert.equal(mobile.panel.hidden, true);
    assert.equal(mobile.memory[0], 0);
  }
  const mac = load(0, { userAgent: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15)', platform: 'MacIntel' }, true);
  assert.equal(mac.panel.hidden, true);
  const touch = load(5);
  assert.equal(touch.panel.hidden, false);
  assert.equal(touch.memory[0], 1);
  assert.equal(touch.onTopLabel.hidden, false);
  assert.equal(touch.onTop.disabled, false);
  assert.equal(touch.classes.has('touch-ontop'), false);
  assert.equal(touch.window.resizeEvents, 1);
  touch.send(touch.move, 'pointerdown', 1, 60, 0);
  touch.send(touch.look, 'pointerdown', 2, 120, 60);
  touch.send(touch.jump, 'pointerdown', 3);
  assert.equal(touch.memory[2], -1000);
  assert.equal(touch.memory[3], 1000);
  assert.equal(touch.memory[5], 1);
  assert.equal(Atomics.exchange(touch.memory, 6, 0), 1);
  touch.send(touch.move, 'pointercancel', 1);
  assert.equal(touch.memory[2], 0);
  assert.equal(touch.memory[3], 1000);
  touch.events.blur();
  assert.deepEqual([...touch.memory.slice(1, 7)], [0, 0, 0, 0, 0, 0]);
  touch.send(touch.move, 'pointerdown', 4, 60, 0);
  touch.onTop.checked = true;
  touch.onTop.handlers.change();
  assert.equal(touch.classes.has('touch-ontop'), true);
  assert.equal(touch.window.resizeEvents, 2);
  assert.equal(touch.panel.hidden, false);
  assert.deepEqual([...touch.memory.slice(1, 7)], [0, 0, 0, 0, 0, 0]);
  touch.onTop.checked = false;
  touch.onTop.handlers.change();
  assert.equal(touch.classes.has('touch-ontop'), false);
  touch.onTop.checked = true;
  touch.onTop.handlers.change();
  touch.toggle.checked = false;
  touch.toggle.handlers.change();
  assert.equal(touch.panel.hidden, true);
  assert.equal(touch.memory[0], 0);
  assert.equal(touch.onTop.disabled, true);
  assert.equal(touch.classes.has('touch-ontop'), false);
  touch.toggle.checked = true;
  touch.toggle.handlers.change();
  assert.equal(touch.classes.has('touch-ontop'), true);
  const desktop = load(0, { userAgent: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)', platform: 'Win32' });
  assert.equal(desktop.cameraControls.hidden, false);
  assert.equal(touch.cameraControls.hidden, true);
  for (const target of [desktop, touch]) {
    for (const command of target.commands) {
      assert.equal(command.disabled, false);
      command.handlers.click({ preventDefault() {}, stopPropagation() {} });
      assert.equal(Atomics.exchange(target.memory, 7, 0), Number(command.dataset.command));
    }
    target.window.t850Touch.updateCamera({ mode: 1, invertY: true });
    for (const command of target.commands) assert.equal(command['aria-pressed'], 'true');
    target.window.t850Touch.updateCamera({ mode: 0, invertY: false });
    for (const command of target.commands) assert.equal(command['aria-pressed'], 'false');
  }
  assert.equal(desktop.panel.hidden, true);
  assert.equal(desktop.onTopLabel.hidden, true);
  desktop.events.pointerdown({ pointerType: 'touch' });
  assert.equal(desktop.panel.hidden, true);
  assert.equal(desktop.onTopLabel.hidden, false);
  desktop.toggle.checked = true;
  desktop.toggle.handlers.change();
  assert.equal(desktop.panel.hidden, false);
  desktop.onTop.checked = true;
  desktop.onTop.handlers.change();
  desktop.window.t850Touch.detach();
  assert.equal(desktop.memory[0], 0);
  assert.equal(desktop.classes.has('touch-ontop'), false);
  for (const name of ['InvalidStateError', 'NotFoundError']) {
    const rejected = load(5);
    rejected.send(rejected.look, 'pointerdown', 2, 120, 60);
    const capture = rejected.move.setPointerCapture;
    rejected.move.setPointerCapture = () => { throw new DOMException('Pointer capture unavailable', name); };
    rejected.jump.setPointerCapture = rejected.move.setPointerCapture;
    assert.doesNotThrow(() => rejected.send(rejected.move, 'pointerdown', 1, 60, 0));
    assert.doesNotThrow(() => rejected.send(rejected.jump, 'pointerdown', 3));
    assert.equal(rejected.move.classList.contains('held'), false);
    assert.equal(rejected.jump.classList.contains('held'), false);
    assert.deepEqual([...rejected.memory.slice(0, 7)], [1, 0, 0, 1000, 0, 0, 0]);
    rejected.send(rejected.move, 'pointermove', 1, 60, 0);
    assert.equal(rejected.memory[2], 0);
    rejected.move.setPointerCapture = capture;
    rejected.jump.setPointerCapture = capture;
    rejected.send(rejected.move, 'pointerdown', 1, 60, 0);
    rejected.send(rejected.jump, 'pointerdown', 3);
    assert.equal(rejected.memory[2], -1000);
    assert.equal(rejected.memory[5], 1);
    rejected.move.releasePointerCapture = () => { throw new DOMException('Pointer already canceled', name); };
    assert.doesNotThrow(() => rejected.events.resize());
    assert.deepEqual([...rejected.memory.slice(0, 7)], [1, 0, 0, 0, 0, 0, 0]);
    for (const control of [rejected.move, rejected.look, rejected.jump]) assert.equal(control.classList.contains('held'), false);
    assert.equal(rejected.look.captured.size, 0);
    assert.equal(rejected.jump.captured.size, 0);
    rejected.send(rejected.look, 'pointerdown', 4, 120, 60);
    rejected.send(rejected.look, 'lostpointercapture', 4);
    assert.equal(rejected.memory[3], 0);
  }
  const ignored = load(5);
  ignored.move.setPointerCapture = () => {};
  ignored.send(ignored.move, 'pointerdown', 1, 60, 0);
  assert.equal(ignored.memory[2], 0);
  assert.equal(ignored.move.classList.contains('held'), false);
  const unexpected = load(5);
  unexpected.move.setPointerCapture = () => { throw new TypeError('Unexpected capture failure'); };
  assert.throws(() => unexpected.send(unexpected.move, 'pointerdown', 1), /Unexpected capture failure/);
});

test('cloud package catalog contains validated routes without asset payloads', async () => {
  const manifests = new Map([
    [publicAssetManifests[0].manifestUrl, { assets: [
      { key: 'test model.glb', url: new URL('test%20model.glb', publicAssetManifests[0].manifestUrl).href },
    ] }],
    [publicAssetManifests[1].manifestUrl, { assets: [
      { kind: 'texture', key: 'test.dds', localRelativePath: 'Textures/test.dds', size: 5,
        contentType: 'image/vnd-ms.dds', url: new URL('test.dds', publicAssetManifests[1].manifestUrl).href },
    ] }],
  ]);
  const catalog = await createCloudAssetCatalog(async (url, options) => {
    assert.equal(options.redirect, 'error');
    return new Response(JSON.stringify(manifests.get(url)), { headers: { 'Content-Type': 'application/json' } });
  });
  const routes = parseCloudAssetCatalog(catalog);
  assert.deepEqual([...routes.keys()], ['Models/test model.glb', 'Textures/test.dds']);
  assert.equal(routes.get('Textures/test.dds').size, 5);
  assert.deepEqual(Object.keys(catalog).sort(), ['manifests', 'routes', 'version']);
  assert.throws(() => parseCloudAssetCatalog({ version: 1, routes: { '../private': { url: 'https://example.com/private' } } }), /Unsafe/);
  assert.throws(() => parseCloudAssetCatalog({ version: 1, routes: { test: { url: 'http://example.com/test' } } }), /public HTTPS/);
  assert.throws(() => parseCloudAssetCatalog({ version: 1, routes: { test: { url: 'https://unapproved.example.com/test' } } }), /unapproved origin/);
  assert.throws(() => parseCloudAssetCatalog({ version: 1, routes: {
    'Models/Test.glb': { url: new URL('first', publicAssetManifests[0].manifestUrl).href },
    'models/test.glb': { url: new URL('second', publicAssetManifests[0].manifestUrl).href },
  } }), /Duplicate/);
});