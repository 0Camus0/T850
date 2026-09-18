import assert from 'node:assert/strict';
import { test } from 'node:test';
import { readFile, access, mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { runInNewContext } from 'node:vm';
import { createAssetHandler } from './pages-worker.mjs';
import { minecraftAssetSelection } from './minecraft-assets.mjs';
import { cloudflareWranglerConfig, validateCloudflareConfig } from './cloudflare-config.mjs';
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
  assert.equal(wrangler.r2_buckets[1].bucket_name, 'example-textures');
  assert.equal(config.minecraftOnly, true);
  for (const value of ['http://example.com/manifest.json', 'https://user:secret@example.com/manifest.json', 'https://example.com/manifest.json?token=secret']) {
    assert.throws(() => validateCloudflareConfig({ ...input, r2Buckets: [{ ...input.r2Buckets[0], manifestUrl: value }, input.r2Buckets[1]] }, { env: {} }), /public HTTPS/);
  }
  assert.throws(() => validateCloudflareConfig({ ...input, accountId: 'invalid' }, { env: {} }), /accountId/);
  assert.throws(() => validateCloudflareConfig({ ...input, r2Buckets: [input.r2Buckets[0], input.r2Buckets[0]] }, { env: {} }), /duplicate/);
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
});

test('WSSI welcome validates enemies, starts only on Launch, and handles unavailable WebGPU', async () => {
  const html = await readFile(new URL('./minecraft-wssi.html', import.meta.url), 'utf8');
  assert.match(html, /<span id="demo-version">v0\.1\.7<\/span>/);
  const source = await readFile(new URL('./minecraft-wssi.mjs', import.meta.url), 'utf8');
  const createWelcome = async (gpu, search = '', mobile = false, isolated = true) => {
    const options = ['960x540', '1280x720', '1920x1080'].map(value => ({ value, selected: value === '1280x720' }));
    const portrait = { matches: mobile, addEventListener(name, callback) { this[name] = callback; } };
    const controls = {
      'launch-form': { addEventListener(name, callback) { this[name] = callback; }, reportValidity: () => true },
      enemies: { value: '1', get valueAsNumber() { return Number(this.value); } },
      resolution: { options, get value() { return options.find(option => option.selected)?.value ?? ''; },
        set value(value) { for (const option of options) option.selected = option.value === value; } },
      launch: { disabled: true }, status: { dataset: {} },
    };
    const navigations = [];
    const sandbox = { URL, URLSearchParams, crossOriginIsolated: isolated, navigator: { gpu },
      document: { getElementById: id => controls[id] }, window: { addEventListener() {}, matchMedia: () => portrait },
      location: { href: 'https://test.pages.dev/minecraft-wssi.html', search, assign: url => navigations.push(url) } };
    await runInNewContext(`(async () => { ${source} })()`, sandbox);
    return { controls, navigations, portrait, submit: () => controls['launch-form'].submit({ preventDefault() {} }) };
  };
  const gpu = { requestAdapter: async () => ({ limits: { maxColorAttachments: 8, maxColorAttachmentBytesPerSample: 128 } }) };
  for (const count of ['0', '8']) {
    const page = await createWelcome(gpu, `?minecraftEnemyCount=${count}&resolution=960x540`);
    assert.equal(page.controls.launch.disabled, false);
    assert.equal(page.navigations.length, 0);
    page.submit();
    const target = new URL(page.navigations[0]);
    assert.equal(target.searchParams.get('scene'), '6');
    assert.equal(target.searchParams.get('demo'), 'wssi');
    assert.equal(target.searchParams.get('minecraftEnemyCount'), count);
    assert.equal(target.searchParams.get('width'), '960');
    page.submit();
    assert.equal(page.navigations.length, 1);
  }
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
      Option: function () {}, fetch: async () => ({ ok: true, json: async () => ({ arguments: [], defaultScene: 0, scenes: [{ id: 0, name: 'Sandbox' }, { id: 6, name: 'Minecraft' }] }) }) };
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
  for (const mode of ['raster', 'compute']) {
    const runtime = await execute(`?postProcessMode=${mode}&computeSelfTest`);
    assert.equal(runtime.args[runtime.args.indexOf('--postProcessMode') + 1], mode);
    assert.equal(runtime.args.includes('--compute-selftest'), true);
  }
  for (const mode of ['', 'invalid']) {
    assert.equal((await execute(`?postProcessMode=${mode}`)).diagnostics.state, 'failed');
  }
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
  assert.match(reloaded.elements['runtime-error-title'].textContent, /Previous run/);
  assert.equal(reloaded.diagnostics.previousError.frames, 123);
  reloaded.events['t850-runtime-ready']();
  assert.equal(reloaded.elements.status.textContent, 'Running');
  reloaded.events.unhandledrejection({ reason: { stack: 'New failure stack' } });
  assert.equal(reloaded.diagnostics.lastError.message, 'New failure stack');
  const unavailable = load({ getItem() { throw new Error('Storage denied'); }, setItem() { throw new Error('Storage denied'); } });
  unavailable.sandbox.Module.print('\x1b[1;31m[ERROR] GPU validation\x1b[0m');
  assert.equal(unavailable.diagnostics.state, 'failed');
  assert.equal(unavailable.diagnostics.lastError.message, '[ERROR] GPU validation');
  assert.equal(unavailable.elements['runtime-error'].hidden, false);
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

test('touch gamepad detects capability, handles multitouch and releases all held input', async () => {
  const source = await readFile(new URL('./touch-controls.js', import.meta.url), 'utf8');
  function load(touchPoints) {
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
    runInNewContext(source, { window, document, navigator: { maxTouchPoints: touchPoints }, Atomics, Event,
      matchMedia: () => ({ matches: false, addEventListener() {} }) });
    const memory = new Int32Array(new SharedArrayBuffer(32));
    window.t850Touch.attach(memory);
    events['t850-runtime-ready']();
    const send = (element, name, id, clientX = 60, clientY = 60) => element.handlers[name]({ pointerId: id, button: 0, clientX, clientY, preventDefault() {}, stopPropagation() {} });
    return { move, look, jump, panel, toggle, label, onTop, onTopLabel, cameraControls, commands, classes, events, memory, window, send };
  }
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
  const desktop = load(0);
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
  assert.equal(desktop.panel.hidden, false);
  assert.equal(desktop.onTopLabel.hidden, false);
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