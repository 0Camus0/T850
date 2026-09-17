import { Builder, By } from 'selenium-webdriver';
import firefox from 'selenium-webdriver/firefox.js';
import chrome from 'selenium-webdriver/chrome.js';
import edge from 'selenium-webdriver/edge.js';
import { PNG } from 'pngjs';
import { mkdir, writeFile } from 'node:fs/promises';
import { openSync } from 'node:fs';
import { resolve } from 'node:path';
import { parseArgs } from 'node:util';

const { values } = parseArgs({ allowNegative: true, options: {
  url: { type: 'string', default: 'http://127.0.0.1:8765/?scene=6&width=960&height=540' },
  browser: { type: 'string', default: 'firefox' },
  output: { type: 'string' },
  headless: { type: 'boolean', default: false },
  probe: { type: 'boolean', default: false },
  launcher: { type: 'boolean', default: false },
  'minecraft-welcome': { type: 'boolean', default: false },
  soak: { type: 'string', default: '0' },
  interactions: { type: 'boolean', default: true },
  'input-rounds': { type: 'string', default: '1' },
  'hold-frames': { type: 'string', default: '30' },
  'focus-cycles': { type: 'boolean', default: false },
  maximize: { type: 'boolean', default: false },
  'worker-diagnostics': { type: 'boolean', default: false },
  'url-diagnostics': { type: 'boolean', default: false },
  'mouse-stress': { type: 'boolean', default: false },
  'block-edits': { type: 'boolean', default: false },
  'dig-seconds': { type: 'string', default: '0' },
  touch: { type: 'boolean', default: false },
  'disable-bc': { type: 'boolean', default: false },
  'disable-float-filtering': { type: 'boolean', default: false },
  gui: { type: 'boolean', default: false },
  'profile-frames': { type: 'string', default: '0' },
  binary: { type: 'string' },
} });
if (!['firefox', 'chrome', 'edge'].includes(values.browser)) throw new Error('Unsupported browser');
const inputRounds = Number(values['input-rounds']);
if (!Number.isInteger(inputRounds) || inputRounds < 1 || inputRounds > 100) throw new Error('Invalid input round count');
const holdFrames = Number(values['hold-frames']);
if (!Number.isInteger(holdFrames) || holdFrames < 30 || holdFrames > 5000) throw new Error('Invalid held input frame count');
const digSeconds = Number(values['dig-seconds']);
if (!Number.isInteger(digSeconds) || digSeconds < 0 || digSeconds > 300) throw new Error('Invalid digging duration');
const output = resolve(values.output ?? `../build/web/${values.browser}`);
await mkdir(output, { recursive: true });
const builder = new Builder().forBrowser(values.browser === 'edge' ? 'MicrosoftEdge' : values.browser);
if (values['url-diagnostics']) builder.setCapability('webSocketUrl', true);
const driverLog = openSync(resolve(output, values.browser === 'edge' ? 'msedgedriver.log' : values.browser === 'chrome' ? 'chromedriver.log' : 'geckodriver.log'), 'w');
if (values.browser === 'edge') {
  const options = new edge.Options();
  if (values.binary) options.setEdgeChromiumBinaryPath(values.binary);
  if (values.headless) options.addArguments('--headless=new');
  const service = new edge.ServiceBuilder().enableVerboseLogging().setStdio(['ignore', driverLog, driverLog]);
  builder.setEdgeOptions(options).setEdgeService(service);
} else if (values.browser === 'chrome') {
  const options = new chrome.Options();
  if (values.binary) options.setChromeBinaryPath(values.binary);
  if (values.headless) options.addArguments('--headless=new');
  const service = new chrome.ServiceBuilder().enableVerboseLogging().setStdio(['ignore', driverLog, driverLog]);
  builder.setChromeOptions(options).setChromeService(service);
} else {
  const options = new firefox.Options();
  const binary = values.binary ?? (process.platform === 'win32' ? 'C:\\Program Files\\Mozilla Firefox\\firefox.exe' : undefined);
  if (binary) options.setBinary(binary);
  options.addArguments('-no-remote');
  if (values['url-diagnostics']) options.addArguments('--devtools');
  if (values.headless) options.addArguments('-headless');
  options.setPreference('browser.shell.checkDefaultBrowser', false);
  options.setPreference('browser.aboutwelcome.enabled', false);
  const service = new firefox.ServiceBuilder().enableVerboseLogging().setStdio(['ignore', driverLog, driverLog]);
  if (values['url-diagnostics']) service.addArguments('--allow-system-access');
  builder.setFirefoxOptions(options).setFirefoxService(service);
}
const driver = await builder.build();
const report = { url: values.url };
let captureWorkers;
let finishWorkerDiagnostics;
let finishFeatureEmulation;
const profileFrames = Number(values['profile-frames']);
if (!Number.isInteger(profileFrames) || profileFrames < 0 || profileFrames > 100000) throw new Error('Invalid profile frame count');
if (profileFrames) {
  const profileUrl = new URL(values.url);
  profileUrl.searchParams.set('profile', '');
  profileUrl.searchParams.set('profileFrames', String(profileFrames));
  report.url = profileUrl.href;
}
try {
  if (values['url-diagnostics']) {
    report.urlDiagnostics = { localFileRequests: [], failures: [], messages: [] };
    const bidi = await driver.getBidi();
    const socket = await bidi.socket;
    socket.on('message', data => {
      const { method, params } = JSON.parse(data.toString());
      if (method === 'network.beforeRequestSent' && params.request.url.startsWith('file:'))
        report.urlDiagnostics.localFileRequests.push({ url: params.request.url, initiator: params.initiator });
      if (method === 'network.fetchError') report.urlDiagnostics.failures.push({ url: params.request.url, error: params.errorText });
      if (method === 'log.entryAdded' && (params.level === 'error' || params.level === 'warn' || params.text?.includes('file:')))
        report.urlDiagnostics.messages.push(params);
    });
    await bidi.subscribe(['log.entryAdded', 'network.beforeRequestSent', 'network.fetchError']);
    await bidi.send({ method: 'script.addPreloadScript', params: { functionDeclaration: `() => {
      const trace = value => { if (String(value?.url ?? value).startsWith('file:')) console.warn('Local file URL attempted', String(value?.url ?? value), new Error().stack); };
      const originalFetch = globalThis.fetch;
      globalThis.fetch = function(resource, ...args) { trace(resource); return originalFetch.call(this, resource, ...args); };
      const originalOpen = XMLHttpRequest.prototype.open;
      XMLHttpRequest.prototype.open = function(method, url, ...args) { trace(url); return originalOpen.call(this, method, url, ...args); };
      const originalWindowOpen = globalThis.open;
      globalThis.open = function(url, ...args) { trace(url); return originalWindowOpen.call(this, url, ...args); };
    }` } });
  }
  await driver.manage().setTimeouts({ pageLoad: 180000, script: 120000 });
  await driver.manage().window().setRect({ width: 1280, height: 800 });
  if (values.maximize) await driver.manage().window().maximize();
  if (values['disable-bc'] || values['disable-float-filtering']) {
    if (values.browser === 'firefox') throw new Error('BC feature emulation requires Chrome or Edge');
    const override = `(() => {
      if (!globalThis.navigator?.gpu) return false;
      if (globalThis.t850FeatureOverride) return true;
      globalThis.t850FeatureOverride = true;
      const requestAdapter = navigator.gpu.requestAdapter.bind(navigator.gpu);
      navigator.gpu.requestAdapter = async function(...args) {
        const adapter = await requestAdapter(...args);
        if (!adapter) return adapter;
        const requestDevice = adapter.requestDevice.bind(adapter);
        adapter.requestDevice = function(descriptor = {}) {
          return requestDevice({ ...descriptor,
          requiredFeatures: [...(descriptor.requiredFeatures ?? [])].filter(feature =>
            !(${!!values['disable-bc']} && feature.startsWith('texture-compression-bc')) &&
            !(${!!values['disable-float-filtering']} && feature === 'float32-filterable')) });
        };
        return adapter;
      };
      return true;
    })()`;
    await driver.sendDevToolsCommand('Page.addScriptToEvaluateOnNewDocument', { source: override });
    const connection = await driver.createCDPConnection('page');
    connection._wsConnection.setMaxListeners(128);
    const send = (sessionId, method, params) => {
      const previous = connection.sessionId;
      connection.sessionId = sessionId;
      const result = connection.send(method, params);
      connection.sessionId = previous;
      return result;
    };
    report.featureEmulation = { attached: 0, errors: [] };
    connection._wsConnection.on('message', async data => {
      const message = JSON.parse(data.toString());
      if (message.method !== 'Target.attachedToTarget') return;
      const { sessionId, targetInfo } = message.params;
      try {
        if (targetInfo.type === 'worker') {
          const result = await send(sessionId, 'Runtime.evaluate', { expression: override, returnByValue: true });
          if (result.error || result.result?.exceptionDetails) throw new Error(JSON.stringify(result));
          if (result.result?.result?.value === true) ++report.featureEmulation.attached;
        }
      } catch (error) { report.featureEmulation.errors.push(String(error)); }
      finally { await send(sessionId, 'Runtime.runIfWaitingForDebugger', {}).catch(error => report.featureEmulation.errors.push(String(error))); }
    });
    await connection.send('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: true, flatten: true });
    finishFeatureEmulation = () => connection._wsConnection.close();
  }
  if (values.touch) {
    if (values.browser === 'firefox') throw new Error('Touch emulation requires Chrome or Edge');
    await driver.sendDevToolsCommand('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
    await driver.sendDevToolsCommand('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
  }
  const capabilities = await driver.getCapabilities();
  report.browser = { name: capabilities.get('browserName'), version: capabilities.get('browserVersion') };
  if (values['worker-diagnostics'] && values.browser !== 'firefox') {
    const connection = await driver.createCDPConnection('page');
    const pageSession = connection.sessionId;
    const workers = new Map();
    const events = [];
    let pendingSave = Promise.resolve();
    const retain = event => {
      events.push(event);
      if (events.length > 100) events.shift();
      const contents = JSON.stringify(events, null, 2);
      pendingSave = pendingSave.then(() => writeFile(resolve(output, 'workers.json'), contents));
    };
    const send = (sessionId, method, params = {}) => {
      const original = connection.sessionId;
      connection.sessionId = sessionId;
      const response = connection.send(method, params);
      connection.sessionId = original;
      return response;
    };
    connection._wsConnection.on('message', async data => {
      const message = JSON.parse(data.toString());
      try {
        if (message.method === 'Target.targetCreated' && message.params.targetInfo.type === 'worker') {
          await send(null, 'Target.attachToTarget', { targetId: message.params.targetInfo.targetId, flatten: true });
        } else if (message.method === 'Target.attachedToTarget') {
          const { sessionId, targetInfo } = message.params;
          workers.set(sessionId, targetInfo);
          retain({ attached: targetInfo });
          connection._wsConnection.setMaxListeners(workers.size + 20);
          await send(sessionId, 'Runtime.enable');
        } else if (message.method === 'Runtime.exceptionThrown') {
          retain({ target: workers.get(message.sessionId), exception: message.params.exceptionDetails });
        } else if (message.method === 'Debugger.paused') {
          retain({ target: workers.get(message.sessionId) ?? { type: 'page' }, paused: message.params });
          await send(message.sessionId, 'Debugger.resume');
        }
      } catch (error) { retain({ diagnosticError: String(error) }); }
    });
    await connection.send('Debugger.enable');
    await send(null, 'Target.setDiscoverTargets', { discover: true });
    captureWorkers = async () => {
      await send(pageSession, 'Debugger.pause');
      for (const [sessionId, target] of workers) {
        if (target.type !== 'worker') continue;
        const state = await send(sessionId, 'Runtime.evaluate', { expression: 'JSON.stringify({ asyncify: typeof Asyncify === "undefined" ? null : Asyncify.state, thread: typeof _pthread_self === "function" ? _pthread_self() : null, mainLoop: typeof MainLoop === "undefined" ? null : { running: MainLoop.running, currentlyRunningMainloop: MainLoop.currentlyRunningMainloop } })', returnByValue: true });
        retain({ target, state });
        await send(sessionId, 'Debugger.enable');
        await send(sessionId, 'Debugger.pause');
      }
    };
    finishWorkerDiagnostics = async () => { await pendingSave; connection._wsConnection.close(); };
  }
  if (values['minecraft-welcome']) {
    if (profileFrames || values.launcher) throw new Error('Welcome smoke test must run without profiling or the general launcher');
    const target = new URL(report.url);
    const welcome = new URL('/minecraft-wssi.html', target);
    const count = target.searchParams.get('minecraftEnemyCount') ?? '1';
    const resolution = `${target.searchParams.get('width') ?? 960}x${target.searchParams.get('height') ?? 540}`;
    await driver.get(welcome.href);
    await driver.wait(async () => {
      const status = await driver.findElement(By.id('status')).getText();
      if (/unavailable|does not support/i.test(status)) throw new Error(status);
      return driver.findElement(By.id('launch')).isEnabled();
    }, 30000, 'Minecraft welcome did not become ready');
    const inspectWelcome = () => driver.executeScript(`return {
      title: document.title, heading: document.querySelector('h1').textContent.replace(/\\s+/g, ' ').trim(),
      engineRequested: performance.getEntriesByType('resource').some(entry => /DayScene\\.(js|wasm)/.test(entry.name)),
      width: innerWidth, contentWidth: document.documentElement.scrollWidth,
      controlsVisible: document.querySelector('.controls').getBoundingClientRect().top < innerHeight,
      imageReady: document.querySelector('.landscape').complete && document.querySelector('.landscape').naturalWidth > 0,
      enemies: document.getElementById('enemies').value, resolution: document.getElementById('resolution').value
    };`);
    report.welcome = { desktop: await inspectWelcome(), count, resolution };
    if (report.welcome.desktop.heading !== 'Hackathon 2026: WSSI Web GPU Minecraft Demo' || report.welcome.desktop.engineRequested)
      throw new Error('Invalid welcome title or eager engine download');
    await writeFile(resolve(output, 'welcome-desktop.png'), Buffer.from(await driver.takeScreenshot(), 'base64'));
    await driver.manage().window().setRect({ width: 480, height: 800 });
    await driver.wait(() => driver.executeScript('return document.getElementById("resolution").value === "720x1280"'),
      10000, 'Portrait resolution options did not settle');
    report.welcome.narrow = await inspectWelcome();
    if (report.welcome.narrow.resolution !== '720x1280') throw new Error('Portrait welcome did not switch its resolution options');
    for (const layout of [report.welcome.desktop, report.welcome.narrow]) {
      if (layout.contentWidth > layout.width || !layout.controlsVisible || !layout.imageReady)
        throw new Error('Minecraft welcome layout or image check failed');
    }
    await writeFile(resolve(output, 'welcome-narrow.png'), Buffer.from(await driver.takeScreenshot(), 'base64'));
    await driver.manage().window().setRect({ width: 1280, height: 800 });
    await driver.wait(() => driver.executeScript('return [...document.getElementById("resolution").options].some(option => option.value === arguments[0])', resolution),
      10000, 'Desktop resolution options did not settle');
    await driver.executeScript(`document.getElementById('enemies').value = arguments[0];
      document.getElementById('resolution').value = arguments[1];`, count, resolution);
    await driver.findElement(By.id('launch')).click();
    await driver.wait(async () => (await driver.getCurrentUrl()).includes('demo=wssi'), 30000, 'Welcome Launch did not navigate');
    report.url = await driver.getCurrentUrl();
  } else if (values.launcher) {
    if (profileFrames) throw new Error('Launcher smoke tests do not enable profiling');
    const target = new URL(report.url);
    const launchUrl = new URL('/launcher.html', target);
    await driver.get(launchUrl.href);
    await driver.wait(async () => {
      const status = await driver.findElement(By.id('status')).getText();
      if (status.includes('unavailable')) throw new Error(status);
      return status === 'WebGPU ready';
    }, 30000, 'Launcher did not become ready');
    report.launcher = await driver.executeScript(`return {
      scenes: document.querySelectorAll('#scenes a').length,
      engineRequested: performance.getEntriesByType('resource').some(entry => /DayScene\\.(js|wasm)/.test(entry.name))
    };`);
    if (report.launcher.scenes !== 7 || report.launcher.engineRequested) throw new Error('Launcher must list seven scenes without downloading the engine');
    await driver.executeAsyncScript(function (done) {
      Promise.all([...document.querySelectorAll('#scenes img')].map(image => image.decode()))
        .then(() => done(true), error => done(String(error)));
    }).then(result => { if (result !== true) throw new Error(`Launcher preview failed: ${result}`); });
    await writeFile(resolve(output, 'launcher.png'), Buffer.from(await driver.takeScreenshot(), 'base64'));
    await driver.manage().window().setRect({ width: 480, height: 800 });
    const launcherLayout = await driver.executeScript(`return {
      width: innerWidth, contentWidth: document.documentElement.scrollWidth,
      previewRatiosValid: [...document.querySelectorAll('#scenes img')].every(image => {
        const rect = image.getBoundingClientRect(); return Math.abs(rect.height - rect.width * 9 / 16) < 2;
      }),
      imagesReady: [...document.querySelectorAll('#scenes img')].every(image => image.complete && image.naturalWidth > 0)
    };`);
    if (launcherLayout.contentWidth > launcherLayout.width || !launcherLayout.imagesReady || !launcherLayout.previewRatiosValid)
      throw new Error('Launcher narrow layout overflow or invalid previews');
    report.launcher.narrow = launcherLayout;
    await writeFile(resolve(output, 'launcher-narrow.png'), Buffer.from(await driver.takeScreenshot(), 'base64'));
    await driver.manage().window().setRect({ width: 1280, height: 800 });
    await driver.executeScript(`const select = document.getElementById('resolution');
      select.value = arguments[0]; select.dispatchEvent(new Event('change'));`,
      `${target.searchParams.get('width') ?? 960}x${target.searchParams.get('height') ?? 540}`);
    await driver.findElement(By.css(`a[data-scene="${Number(target.searchParams.get('scene') ?? 6)}"]`)).click();
    report.url = await driver.getCurrentUrl();
  } else {
    await driver.get(report.url);
  }
  await driver.executeScript(`window.t850InputEvents = [];
    for (const type of ['focus', 'blur', 'pointerlockchange', 'keydown', 'keyup']) {
      window.addEventListener(type, event => {
        window.t850InputEvents.push({ type, key: event.key, focus: document.hasFocus(),
          active: document.activeElement?.id, locked: !!document.pointerLockElement, time: performance.now() });
        if (window.t850InputEvents.length > 40) window.t850InputEvents.shift();
      }, true);
    }`);
  report.gpu = await driver.executeAsyncScript(function (done) {
    if (!navigator.gpu) { done({ available: false, isolated: crossOriginIsolated }); return; }
    navigator.gpu.requestAdapter().then(adapter => done({
      available: !!adapter,
      isolated: crossOriginIsolated,
      limits: adapter ? {
        maxColorAttachments: adapter.limits.maxColorAttachments,
        maxColorAttachmentBytesPerSample: adapter.limits.maxColorAttachmentBytesPerSample,
        maxBufferSize: adapter.limits.maxBufferSize,
      } : null,
      features: adapter ? [...adapter.features] : [],
      info: adapter?.info ? {
        vendor: adapter.info.vendor,
        architecture: adapter.info.architecture,
        device: adapter.info.device,
        description: adapter.info.description,
        isFallbackAdapter: adapter.info.isFallbackAdapter,
      } : null,
    })).catch(error => done({ available: false, error: String(error) }));
  });
  console.log(JSON.stringify({ browser: report.browser, gpu: report.gpu }, null, 2));
  if (!report.gpu.available || !report.gpu.isolated) throw new Error(`${values.browser} WebGPU/isolation prerequisite failed`);
  if (!values.probe) {
    await driver.wait(async () => {
      const state = await driver.executeScript('return { state: window.t850?.state, errors: window.t850?.errors }');
      if (state?.state === 'failed') throw new Error(state.errors.join('\n'));
      return state?.state === 'running';
    }, 180000, 'Scene did not reach runtime-ready state');
    if (values['minecraft-welcome']) {
      report.welcome.runtime = await driver.executeScript(`return { arguments: Module.arguments,
        scene: window.t850.scene, selectorHidden: document.getElementById('scene').hidden,
        home: document.querySelector('header a').getAttribute('href') };`);
      const runtime = report.welcome.runtime;
      const countIndex = runtime.arguments.indexOf('--minecraftEnemyCount');
      if (runtime.scene !== 'Minecraft' || !runtime.selectorHidden || countIndex < 0 || runtime.arguments[countIndex + 1] !== report.welcome.count)
        throw new Error('Minecraft welcome settings did not reach the runtime');
    }
    if (values.launcher) {
      report.launchSelection = await driver.executeAsyncScript(function (done) {
        fetch('scenes.json').then(response => response.json()).then(catalog => {
          const scene = catalog.scenes.find(entry => entry.id === Number(new URLSearchParams(location.search).get('scene')));
          const modelIndex = scene.arguments?.indexOf('--model') ?? -1;
          const resource = scene.sceneFile ?? (modelIndex >= 0 ? scene.arguments[modelIndex + 1] : null);
          done({ id: scene.id, resource, arguments: Module.arguments,
            loadedBytes: resource ? Module.FS.stat('/assets/' + resource).size : null });
        }).catch(error => done({ error: String(error) }));
      });
      const selection = report.launchSelection;
      if (selection.error || (selection.resource && (!(selection.loadedBytes > 0) || !selection.arguments.includes(selection.resource))))
        throw new Error(`Selected scene resource was not loaded: ${JSON.stringify(selection)}`);
    }
    report.inputChecks = [];
    const canvas = await driver.findElement(By.id('canvas'));
    const captureCanvas = async () => {
      const readBounds = () => driver.executeScript(`const rect = document.getElementById('canvas').getBoundingClientRect();
        return { x: rect.x, y: rect.y, width: rect.width, height: rect.height, viewportWidth: innerWidth, viewportHeight: innerHeight };`);
      for (let attempt = 0; attempt < 3; ++attempt) {
        const bounds = await readBounds();
        const viewport = PNG.sync.read(Buffer.from(await driver.takeScreenshot(), 'base64'));
        const after = await readBounds();
        if (Object.keys(bounds).some(key => Math.abs(bounds[key] - after[key]) > 0.5)) continue;
        const scale = viewport.width / bounds.viewportWidth;
        if (Math.abs(viewport.height - bounds.viewportHeight * scale) > 2) continue;
        const left = Math.max(0, Math.round(bounds.x * scale));
        const top = Math.max(0, Math.round(bounds.y * scale));
        const right = Math.min(viewport.width, Math.round((bounds.x + bounds.width) * scale));
        const bottom = Math.min(viewport.height, Math.round((bounds.y + bounds.height) * scale));
        if (bounds.x < -1 || bounds.y < -1 || bounds.x + bounds.width > bounds.viewportWidth + 1 ||
            bounds.y + bounds.height > bounds.viewportHeight + 1 || right <= left || bottom <= top)
          throw new Error('Scene canvas extends outside the viewport');
        const cropped = new PNG({ width: right - left, height: bottom - top });
        PNG.bitblt(viewport, cropped, left, top, cropped.width, cropped.height, 0, 0);
        return PNG.sync.write(cropped).toString('base64');
      }
      throw new Error('Scene viewport did not settle for capture');
    };
    const waitFrames = async count => {
      const initial = await driver.executeScript('return window.t850.frames ?? 0');
      await driver.wait(async () => {
        const state = await driver.executeScript('return { state: window.t850.state, frames: window.t850.frames, errors: window.t850.errors }');
        if (state.state === 'failed') throw new Error(state.errors.join('\n'));
        return state.frames >= initial + count;
      }, 60000, 'Scene stopped advancing frames');
      if (values['minecraft-welcome']) {
        await driver.wait(async () => driver.executeScript(`
          const portrait = matchMedia('(max-width: 700px) and (orientation: portrait)').matches;
          const aspect = portrait ? 9 / 16 : 16 / 9;
          const rect = document.getElementById('canvas').getBoundingClientRect();
          const size = window.t850.renderSize;
          return Math.abs(rect.width / rect.height - aspect) < 0.003 &&
            Math.abs(size[0] / size[1] - aspect) < 0.003 &&
            rect.right <= innerWidth + 1 && rect.bottom <= innerHeight + 1;
        `), 10000, 'WSSI canvas or render target lost its viewport aspect ratio');
      }
    };
    await waitFrames(30);
    report.initialRenderSize = await driver.executeScript('return window.t850.renderSize');
    const initialImage = await captureCanvas();
    await writeFile(resolve(output, 'initial.png'), Buffer.from(initialImage, 'base64'));
    if (values.touch) {
      await driver.wait(() => driver.executeScript('return !document.getElementById("touch-controls").hidden && window.t850.touch?.active'),
        15000, 'Touch controls did not activate on a touch-capable viewport');
      const point = async (selector, id, offsetX = 0, offsetY = 0) => {
        const center = await driver.executeScript(`const rect = document.querySelector(arguments[0]).getBoundingClientRect();
          return { x: rect.x + rect.width / 2, y: rect.y + rect.height / 2 };`, selector);
        return { x: center.x + offsetX, y: center.y + offsetY, id, radiusX: 4, radiusY: 4, force: 1 };
      };
      const dispatchTouch = (type, touchPoints) => driver.sendDevToolsCommand('Input.dispatchTouchEvent', { type, touchPoints });
      const start = await point('#touch-move', 1);
      await dispatchTouch('touchStart', [start]);
      const walking = { ...start, y: start.y - 40 };
      await dispatchTouch('touchMove', [walking]);
      await waitFrames(500);
      report.touch = { moving: await driver.executeScript('return window.t850.touch') };
      if (report.touch.moving.moveY > -0.8) throw new Error('Touch stick did not drive the engine gamepad');
      const look = await point('#touch-look', 2, 0, 24);
      await dispatchTouch('touchStart', [walking, look]);
      await waitFrames(40);
      report.touch.simultaneous = await driver.executeScript('return window.t850.touch');
      if (report.touch.simultaneous.moveY > -0.8 || report.touch.simultaneous.lookY < 0.4)
        throw new Error('Simultaneous movement/look touch failed');
      await dispatchTouch('touchEnd', []);
      await waitFrames(30);
      for (const [label, field] of [['Jump', 'jump'], ['Sprint', 'sprint'], ['Break block', 'breakBlock'], ['Place block', 'placeBlock']]) {
        await dispatchTouch('touchStart', [await point(`button[aria-label="${label}"]`, 3)]);
        await waitFrames(30);
        const state = await driver.executeScript('return window.t850.touch');
        if (!state[field]) throw new Error(`Touch ${label} did not reach the engine`);
        await dispatchTouch('touchEnd', []);
        await waitFrames(30);
      }
      await dispatchTouch('touchStart', [walking]);
      await waitFrames(30);
      await dispatchTouch('touchCancel', []);
      await waitFrames(30);
      report.touch.released = await driver.executeScript('return window.t850.touch');
      if (report.touch.released.moveX || report.touch.released.moveY || report.touch.released.lookX || report.touch.released.lookY ||
        report.touch.released.jump || report.touch.released.sprint || report.touch.released.breakBlock || report.touch.released.placeBlock)
        throw new Error('Canceled touches left engine input held');
      report.touch.positions = await driver.executeScript(`return window.t850.logs.filter(line => line.includes('[Minecraft] Player pos='));`);
      if (report.touch.positions.length < 2 || new Set(report.touch.positions.map(line => line.match(/Player pos=([^ ]+)/)?.[1])).size < 2)
        throw new Error('Touch test did not verify actual player movement; use logLevel=info');
      report.touch.edits = await driver.executeScript(`return window.t850.logs.filter(line => line.includes('[Minecraft] Block edit'));`);
      if (report.touch.edits.length < 2) throw new Error('Touch block buttons did not produce actual voxel edits');
      const touchLayout = () => driver.executeScript(`
        const controls = [...document.querySelectorAll('#touch-controls button')].map(element => element.getBoundingClientRect());
        return { width: innerWidth, height: innerHeight, renderSize: window.t850.renderSize,
          iconsReady: [...document.querySelectorAll('#touch-controls img')].every(image => image.complete && image.naturalWidth > 0),
          withinViewport: controls.every(rect => rect.left >= 0 && rect.top >= 36 && rect.right <= innerWidth + 1 && rect.bottom <= innerHeight + 1),
          overlaps: controls.some((rect, index) => controls.slice(index + 1).some(other => rect.left < other.right && rect.right > other.left && rect.top < other.bottom && rect.bottom > other.top)),
          controlsBelowGame: controls.every(rect => rect.top >= document.getElementById('canvas').getBoundingClientRect().bottom - 1) };`);
      report.touch.portrait = await touchLayout();
      if (!report.touch.portrait.iconsReady || !report.touch.portrait.withinViewport || report.touch.portrait.overlaps || !report.touch.portrait.controlsBelowGame)
        throw new Error('Portrait touch layout has overlap, missing icons, or clipped controls');
      await writeFile(resolve(output, 'touch-controls.png'), Buffer.from(await driver.takeScreenshot(), 'base64'));
      await driver.findElement(By.id('touch-enabled')).click();
      await waitFrames(30);
      if (await driver.executeScript('return window.t850.touch.active')) throw new Error('Touch disable did not clear the engine gamepad');
      await driver.findElement(By.id('touch-enabled')).click();
      await waitFrames(30);
      await driver.sendDevToolsCommand('Emulation.setDeviceMetricsOverride', { width: 844, height: 390, deviceScaleFactor: 1, mobile: true });
      await waitFrames(60);
      report.touch.landscape = await touchLayout();
      if (!report.touch.landscape.iconsReady || !report.touch.landscape.withinViewport || report.touch.landscape.overlaps)
        throw new Error('Landscape touch controls overlap or leave the viewport');
      await writeFile(resolve(output, 'touch-landscape.png'), Buffer.from(await driver.takeScreenshot(), 'base64'));
    }
    if (profileFrames) {
      await driver.wait(() => driver.executeScript('return !!window.t850.telemetry'), 240000, 'Profile did not finish');
      const telemetry = await driver.executeScript('return window.t850.telemetry');
      if (telemetry.sampleCount !== profileFrames) throw new Error(`Expected ${profileFrames} complete profile frames, received ${telemetry.sampleCount}`);
      await writeFile(resolve(output, 'telemetry.json'), JSON.stringify(telemetry, null, 2));
      report.profileFrames = telemetry.sampleCount;
    } else {
    const soakSeconds = Number(values.soak);
    if (!Number.isFinite(soakSeconds) || soakSeconds < 0) throw new Error('Invalid soak duration');
    if (soakSeconds > 0) {
      report.samples = [];
      const started = Date.now();
      let nextSample = started;
      await driver.wait(async () => {
        const now = Date.now();
        if (now >= nextSample) {
          const state = await driver.executeScript(`return {
            frames: window.t850.frames, draws: window.t850.draws,
            renderSize: window.t850.renderSize,
            workerTiming: window.t850.workerTiming,
            memoryBytes: window.t850.memoryBytes, state: window.t850.state
          }`);
          report.samples.push({ seconds: (now - started) / 1000, ...state });
          console.log(JSON.stringify(report.samples.at(-1)));
          await writeFile(resolve(output, 'samples.json'), JSON.stringify(report.samples, null, 2));
          if (state.state === 'failed') throw new Error('Engine failed during soak');
          nextSample = now + 5000;
        }
        return now - started >= soakSeconds * 1000;
      }, (soakSeconds + 30) * 1000, 'Soak did not complete');
      const rates = report.samples.slice(1).map((sample, index) =>
        (sample.frames - report.samples[index].frames) / (sample.seconds - report.samples[index].seconds));
      if (rates.some(rate => !Number.isFinite(rate) || rate <= 0)) throw new Error('Frames stalled during soak');
      if (rates.length >= 6) {
        const average = samples => samples.reduce((sum, value) => sum + value, 0) / samples.length;
        report.sustainedFps = { first: average(rates.slice(0, 3)), last: average(rates.slice(-3)) };
        if (report.sustainedFps.last < report.sustainedFps.first * 0.6)
          throw new Error('Sustained frame rate degraded by more than 40%');
      }
    }
    if (digSeconds) {
      report.digging = { samples: [], logs: [] };
      const retainedLogs = new Set();
      const samplePhase = async (phase, seconds) => {
        const started = Date.now();
        let nextSample = started;
        await driver.wait(async () => {
          const now = Date.now();
          if (now >= nextSample) {
            const state = await driver.executeScript(`return {
              frames: window.t850.frames, draws: window.t850.draws,
              workerTiming: window.t850.workerTiming, memoryBytes: window.t850.memoryBytes,
              state: window.t850.state, errors: window.t850.errors,
              logs: window.t850.logs.filter(line => /Player pos=|Block edit|remesh uploaded/.test(line))
            };`);
            for (const line of state.logs) retainedLogs.add(line);
            delete state.logs;
            const previous = report.digging.samples.at(-1);
            const sample = { phase, timeMs: now, seconds: (now - started) / 1000, ...state };
            if (previous) sample.fps = (sample.frames - previous.frames) * 1000 / (now - previous.timeMs);
            report.digging.samples.push(sample);
            report.digging.logs = [...retainedLogs];
            await writeFile(resolve(output, 'digging.json'), JSON.stringify(report.digging, null, 2));
            console.log(JSON.stringify(sample));
            if (state.state === 'failed') throw new Error(state.errors.join('\n'));
            nextSample = now + 1000;
          }
          return now - started >= seconds * 1000;
        }, (seconds + 30) * 1000, `${phase} did not finish`);
      };
      await canvas.click();
      await driver.wait(() => driver.executeScript('return document.pointerLockElement === document.getElementById("canvas")'),
        10000, 'Digging did not acquire pointer lock');
      for (let move = 0; move < 8; ++move) await driver.actions().move({ origin: canvas, x: 0, y: 180 }).perform();
      await samplePhase('baseline', 5);
      await writeFile(resolve(output, 'dig-start.png'), Buffer.from(await captureCanvas(), 'base64'));
      await driver.actions().press(0).perform();
      try { await samplePhase('digging', digSeconds); }
      finally { await driver.actions().release(0).perform(); }
      await samplePhase('recovery', 15);
      await writeFile(resolve(output, 'dig-end.png'), Buffer.from(await captureCanvas(), 'base64'));
      await driver.executeScript('document.exitPointerLock()');
      const heights = report.digging.logs.map(line => line.match(/Player pos=\([^,]+,\s*([-\d.]+)/)?.[1]).filter(Boolean).map(Number);
      report.digging.descent = heights.length ? Math.max(...heights) - Math.min(...heights) : 0;
      report.digging.edits = report.digging.logs.filter(line => line.includes('Block edit')).length;
      if (report.digging.descent < 8 || report.digging.edits < 8)
        throw new Error('Digging did not remove enough blocks and descend; use logLevel=info and verify the downward aim');
    }
    if (values.interactions) {
    const beforeMove = initialImage;
    await canvas.click();
    await driver.wait(() => driver.executeScript('return document.pointerLockElement === document.getElementById("canvas")'),
      10000, 'Scene did not acquire pointer lock');
    report.pointerLock = true;
    if (values['block-edits']) {
      const before = await driver.executeScript(`return {
        edits: window.t850.logs.filter(line => line.includes('[Minecraft] Block edit')).length,
        uploads: window.t850.logs.filter(line => line.includes('remesh uploaded')).length
      };`);
      await driver.actions().move({ origin: canvas, x: 0, y: 200 }).perform();
      await waitFrames(30);
      await writeFile(resolve(output, 'block-target.png'), Buffer.from(await captureCanvas(), 'base64'));
      const counts = [];
      for (const button of [0, 2, 0, 2]) {
        await driver.actions().press(button).perform();
        try { await waitFrames(30); }
        finally { await driver.actions().release(button).perform(); }
        await driver.wait(async () => {
          const state = await driver.executeScript(`return { state: window.t850.state, errors: window.t850.errors,
            edits: window.t850.logs.filter(line => line.includes('[Minecraft] Block edit')).length,
            uploads: window.t850.logs.filter(line => line.includes('remesh uploaded')).length };`);
          if (state.state === 'failed') throw new Error(state.errors.join('\n'));
          const previous = counts.at(-1) ?? before;
          if (state.edits > previous.edits && state.uploads > previous.uploads) { counts.push(state); return true; }
          return false;
        }, 20000, 'Block edit did not finish remeshing');
        await waitFrames(60);
      }
      report.blockEdits = counts;
      await writeFile(resolve(output, 'blocks-edited.png'), Buffer.from(await captureCanvas(), 'base64'));
    }
    for (let round = 0; round < inputRounds; ++round) {
    const beforeInput = await driver.executeScript('return window.t850.input');
    await driver.actions().move({ origin: canvas, x: round % 2 ? 20 : -20, y: 0 }).perform();
    await driver.actions().keyDown('w').perform();
    if (values['mouse-stress']) {
      const actions = driver.actions();
      for (let move = 0; move < 150; ++move)
        actions.move({ origin: canvas, x: move % 2 ? 12 : -12, y: move % 3 ? 4 : -4, duration: 5 });
      const watchdog = captureWorkers ? setTimeout(() => {
        captureWorkers().catch(error => { report.workerCaptureFailure = String(error); });
      }, 20000) : null;
      try { await actions.perform(); } finally { if (watchdog) clearTimeout(watchdog); }
    }
    await waitFrames(holdFrames);
    const heldInput = await driver.executeScript('return window.t850.input');
    if (heldInput && !heldInput.forward) throw new Error('Held movement key was cleared before keyup');
    await driver.actions().keyUp('w').perform();
    if (inputRounds > 1) await waitFrames(300);
    const inputState = await driver.executeScript(`return { frames: window.t850.frames,
      focus: document.hasFocus(), active: document.activeElement?.id,
      locked: document.pointerLockElement === document.getElementById('canvas'),
      engineInput: window.t850.input };`);
    report.inputChecks.push(inputState);
    if (!inputState.locked) throw new Error('Pointer lock lost during movement');
    if (beforeInput && (inputState.engineInput.keys <= beforeInput.keys || inputState.engineInput.mouse <= beforeInput.mouse))
      throw new Error('SDL stopped receiving keyboard or mouse events');
    if (values['focus-cycles']) {
      await driver.executeScript('document.exitPointerLock(); document.getElementById("scene").focus()');
      await waitFrames(30);
      await canvas.click();
      await driver.wait(() => driver.executeScript('return document.pointerLockElement === document.getElementById("canvas")'),
        10000, 'Scene did not reacquire pointer lock');
    }
    }
    await driver.executeScript('document.exitPointerLock()');
    await waitFrames(30);
    report.movementFramesAdvanced = true;
    const afterMove = await captureCanvas();
    report.imageChangedWithMovement = beforeMove !== afterMove;
    if (!report.imageChangedWithMovement) throw new Error('Movement input did not change the rendered view');
    }
    if (values.gui) {
      const toggleGui = async () => {
        await driver.actions().keyDown('g').perform();
        await waitFrames(10);
        await driver.actions().keyUp('g').perform();
        await waitFrames(30);
      };
      await driver.executeScript('document.getElementById("canvas").focus()');
      const beforeGui = await captureCanvas();
      await toggleGui();
      const guiImage = await captureCanvas();
      report.guiChangedImage = beforeGui !== guiImage;
      if (!report.guiChangedImage) throw new Error('GUI toggle did not change the rendered view');
      await writeFile(resolve(output, 'gui.png'), Buffer.from(guiImage, 'base64'));
      await toggleGui();
    }
    await driver.manage().window().setRect({ width: 900, height: 680 });
    await waitFrames(30);
    report.resized = await driver.executeScript('return window.t850.renderSize');
    const png = PNG.sync.read(Buffer.from(await captureCanvas(), 'base64'));
    let sum = 0;
    let squared = 0;
    for (let pixel = 0; pixel < png.data.length; pixel += 4) {
      const luminance = (png.data[pixel] + png.data[pixel + 1] + png.data[pixel + 2]) / 3;
      sum += luminance;
      squared += luminance * luminance;
    }
    const count = png.width * png.height;
    report.image = { width: png.width, height: png.height,
      standardDeviation: Math.sqrt(Math.max(0, squared / count - (sum / count) ** 2)) };
    if (report.image.standardDeviation < 3) throw new Error('Scene canvas is blank or uniform');
    await driver.manage().window().setRect({ width: 480, height: 800 });
    await waitFrames(30);
    report.narrowRenderSize = await driver.executeScript('return window.t850.renderSize');
    await writeFile(resolve(output, 'narrow.png'), Buffer.from(await captureCanvas(), 'base64'));
    }
  }
  const finalState = await driver.executeScript('return { errors: window.t850?.errors }');
  if (finalState?.errors?.length) throw new Error(finalState.errors.join('\n'));
  if (values['disable-bc'] || values['disable-float-filtering']) {
    report.deviceFeatures = await driver.executeScript(`return window.t850.logs.find(line => line.includes('[WebGPU] Device optional features:'));`);
    if (!report.deviceFeatures || report.featureEmulation.errors.length || !report.featureEmulation.attached ||
      (values['disable-bc'] && !report.deviceFeatures.includes('BC=0')) ||
      (values['disable-float-filtering'] && !report.deviceFeatures.includes('float32-filterable=0')))
      throw new Error('Optional device features were not disabled for the compatibility test');
  }
  if (values['disable-bc']) {
    report.bcFallback = await driver.executeScript(`return window.t850.logs.filter(line => line.includes('BC unavailable; decoded'));`);
    if (!report.bcFallback.some(line => line.includes('faces=6'))) throw new Error('BC fallback was not exercised; use logLevel=info');
  }
  if (values['minecraft-welcome']) {
    report.runtime = await driver.executeScript('const { telemetry, ...runtime } = window.t850 ?? {}; return runtime');
    await driver.findElement(By.css('a[aria-label="Back to WSSI welcome"]')).click();
    await driver.wait(() => driver.findElement(By.id('launch')).isEnabled(), 30000, 'Return to welcome failed');
    report.welcome.returned = await driver.executeScript(`return {
      enemies: document.getElementById('enemies').value, resolution: document.getElementById('resolution').value,
      portrait: matchMedia('(max-width: 700px) and (orientation: portrait)').matches,
      engineRequested: performance.getEntriesByType('resource').some(entry => /DayScene\\.(js|wasm)/.test(entry.name)) };`);
    const expectedDimensions = report.welcome.resolution.split('x').map(Number).sort((first, second) => second - first);
    if (report.welcome.returned.portrait) expectedDimensions.reverse();
    if (report.welcome.returned.enemies !== report.welcome.count || report.welcome.returned.resolution !== expectedDimensions.join('x') || report.welcome.returned.engineRequested)
      throw new Error('Returning to welcome lost settings or loaded the engine');
  } else if (values.launcher) {
    report.runtime = await driver.executeScript('const { telemetry, ...runtime } = window.t850 ?? {}; return runtime');
    await driver.findElement(By.css('a[aria-label="Back to scenes"]')).click();
    await driver.wait(async () => (await driver.findElement(By.id('status')).getText()) === 'WebGPU ready',
      30000, 'Return to scene launcher failed');
    report.launcher.returned = true;
    if (await driver.executeScript('return performance.getEntriesByType("resource").some(entry => /DayScene\\.(js|wasm)/.test(entry.name))'))
      throw new Error('Returning to the launcher downloaded the engine');
  }
  report.passed = true;
} catch (error) {
  report.passed = false;
  report.failure = String(error);
  if (captureWorkers) {
    try { await captureWorkers(); } catch (diagnosticError) { report.workerCaptureFailure = String(diagnosticError); }
  }
  process.exitCode = 1;
} finally {
  try {
    if (values.browser !== 'firefox') report.browserLogs = await driver.manage().logs().get('browser');
    report.inputEvents = await driver.executeScript('return window.t850InputEvents');
    if (!report.runtime) report.runtime = await driver.executeScript('const { telemetry, ...runtime } = window.t850 ?? {}; return runtime');
    await writeFile(resolve(output, `${values.browser}.png`), Buffer.from(await driver.takeScreenshot(), 'base64'));
  } catch (error) { report.captureFailure = String(error); }
  await writeFile(resolve(output, 'report.json'), JSON.stringify(report, null, 2));
  if (values['url-diagnostics'] && values.browser === 'firefox') {
    try {
      await driver.setContext(firefox.Context.CHROME);
      report.urlDiagnostics.securityMessages = await driver.executeScript(`return Services.console.getMessageArray()
        .filter(entry => entry.message.includes('file:///') || entry.message.includes('Security Error'))
        .map(entry => ({ message: entry.message, sourceName: entry.sourceName, category: entry.category }));`);
    } catch (error) {
      report.urlDiagnostics.securityCaptureError = String(error);
      report.passed = false;
      report.failure = 'Firefox security-console capture failed: ' + String(error);
      process.exitCode = 1;
    }
    finally { await driver.setContext(firefox.Context.CONTENT); }
    await writeFile(resolve(output, 'report.json'), JSON.stringify(report, null, 2));
  }
  console.log(JSON.stringify({ passed: report.passed, failure: report.failure, image: report.image, output }, null, 2));
  if (finishWorkerDiagnostics) await finishWorkerDiagnostics();
  if (finishFeatureEmulation) finishFeatureEmulation();
  await driver.quit();
}