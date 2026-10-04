import { mkdir, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import process from 'node:process';

function parseArguments(values) {
  const options = new Map();
  for (let index = 2; index < values.length; ++index) {
    const name = values[index];
    if (!name.startsWith('--') || index + 1 >= values.length) throw new Error(`Invalid argument: ${name}`);
    options.set(name.slice(2), values[++index]);
  }
  const integer = (name, fallback, minimum, maximum) => {
    const value = Number(options.get(name) ?? fallback);
    if (!Number.isInteger(value) || value < minimum || value > maximum) throw new Error(`Invalid --${name}`);
    return value;
  };
  return {
    endpoint: options.get('endpoint') ?? 'http://127.0.0.1:9222',
    baseUrl: options.get('base-url') ?? 'http://127.0.0.1:8876/',
    output: resolve(options.get('output') ?? 'edge-offline-result.json'),
    machine: options.get('machine') ?? process.env.COMPUTERNAME ?? 'unknown',
    architecture: options.get('architecture') ?? process.arch,
    repetitions: integer('repetitions', 5, 1, 10),
    frames: integer('frames', 600, 30, 10000),
    holdFrame: integer('hold-frame', 3000, 1, 20000),
    width: integer('width', 1920, 320, 7680),
    height: integer('height', 1080, 240, 4320),
    timeoutMs: integer('timeout-ms', 900000, 30000, 3600000),
  };
}

class CdpConnection {
  constructor(url) {
    this.nextId = 1;
    this.pending = new Map();
    this.socket = new WebSocket(url);
  }

  async open() {
    await new Promise((resolveOpen, rejectOpen) => {
      this.socket.addEventListener('open', resolveOpen, { once: true });
      this.socket.addEventListener('error', () => rejectOpen(new Error('CDP WebSocket connection failed')), { once: true });
    });
    this.socket.addEventListener('message', event => {
      const message = JSON.parse(event.data);
      if (!message.id || !this.pending.has(message.id)) return;
      const { resolveRequest, rejectRequest } = this.pending.get(message.id);
      this.pending.delete(message.id);
      if (message.error) rejectRequest(new Error(`${message.error.message} (${message.error.code})`));
      else resolveRequest(message.result);
    });
    this.socket.addEventListener('close', () => {
      for (const { rejectRequest } of this.pending.values()) rejectRequest(new Error('CDP WebSocket closed'));
      this.pending.clear();
    });
  }

  request(method, params = {}, sessionId) {
    const id = this.nextId++;
    const message = { id, method, params };
    if (sessionId) message.sessionId = sessionId;
    return new Promise((resolveRequest, rejectRequest) => {
      this.pending.set(id, { resolveRequest, rejectRequest });
      this.socket.send(JSON.stringify(message));
    });
  }

  close() {
    this.socket.close();
  }
}

async function connect(endpoint) {
  const response = await fetch(new URL('/json/version', endpoint));
  if (!response.ok) throw new Error(`Cannot read Edge CDP version endpoint: HTTP ${response.status}`);
  const version = await response.json();
  const endpointUrl = new URL(endpoint);
  const webSocketUrl = new URL(version.webSocketDebuggerUrl);
  webSocketUrl.protocol = endpointUrl.protocol === 'https:' ? 'wss:' : 'ws:';
  webSocketUrl.hostname = endpointUrl.hostname;
  webSocketUrl.port = endpointUrl.port;
  const connection = new CdpConnection(webSocketUrl);
  await connection.open();
  return { connection, version };
}

function benchmarkUrl(options, mode, repetition) {
  const url = new URL('DayScene.html', options.baseUrl);
  url.search = new URLSearchParams({
    scene: '1', width: String(options.width), height: String(options.height),
    postProcessMode: mode, shaderFlow: 'wgsl', benchmarkNoPresent: '1',
    benchmarkFrames: String(options.frames), benchmarkHoldFrame: String(options.holdFrame),
    logLevel: 'info', capture: `${repetition}-${mode}-${Date.now()}`,
  });
  return url.href;
}

const delay = milliseconds => new Promise(resolveDelay => setTimeout(resolveDelay, milliseconds));

async function waitForPageContext(connection, sessionId, url, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const evaluation = await connection.request('Runtime.evaluate', {
        expression: `location.href === ${JSON.stringify(url)} && document.readyState !== 'loading'`,
        returnByValue: true,
      }, sessionId);
      if (evaluation.result.value === true) return;
    } catch (error) {
      if (!/context|navigate|closed/i.test(error.message)) throw error;
    }
    await delay(100);
  }
  throw new Error('Browser page navigation timed out');
}

async function captureBrowserMetadata(connection, options) {
  const { browserContextId } = await connection.request('Target.createBrowserContext', { disposeOnDetach: true });
  let targetId;
  try {
    const url = new URL('__t850', options.baseUrl).href;
    ({ targetId } = await connection.request('Target.createTarget', { url: 'about:blank', browserContextId }));
    await connection.request('Target.activateTarget', { targetId });
    const { sessionId } = await connection.request('Target.attachToTarget', { targetId, flatten: true });
    await connection.request('Runtime.enable', {}, sessionId);
    await connection.request('Page.enable', {}, sessionId);
    await connection.request('Page.navigate', { url }, sessionId);
    await waitForPageContext(connection, sessionId, url, options.timeoutMs);
    const metadataEvaluation = await connection.request('Runtime.evaluate', {
      expression: `Promise.race([
        (async () => {
          const adapter = await navigator.gpu.requestAdapter();
          const info = adapter?.info || {};
          return {
            userAgent: navigator.userAgent,
            adapter: { vendor: info.vendor || '', architecture: info.architecture || '',
              device: info.device || '', description: info.description || '' },
            timestampQueryFeature: Boolean(adapter?.features?.has('timestamp-query')),
            writeTimestampMethod: typeof GPUCommandEncoder !== 'undefined' &&
              typeof GPUCommandEncoder.prototype.writeTimestamp === 'function'
          };
        })(),
        new Promise((resolve, reject) => setTimeout(() => reject(new Error('Adapter capability probe timed out')), 30000))
      ])`,
      awaitPromise: true, returnByValue: true,
    }, sessionId);
    if (metadataEvaluation.exceptionDetails) throw new Error(metadataEvaluation.exceptionDetails.exception?.description ?? metadataEvaluation.exceptionDetails.text);
    return metadataEvaluation.result.value;
  } finally {
    if (targetId) await connection.request('Target.closeTarget', { targetId }).catch(() => {});
    await connection.request('Target.disposeBrowserContext', { browserContextId }).catch(() => {});
  }
}

async function captureRun(connection, options, mode, repetition, browserMetadata) {
  const { browserContextId } = await connection.request('Target.createBrowserContext', { disposeOnDetach: true });
  let targetId;
  try {
    ({ targetId } = await connection.request('Target.createTarget', { url: 'about:blank', browserContextId }));
    await connection.request('Target.activateTarget', { targetId });
    const { sessionId } = await connection.request('Target.attachToTarget', { targetId, flatten: true });
    await connection.request('Runtime.enable', {}, sessionId);
    await connection.request('Page.enable', {}, sessionId);
    await connection.request('Emulation.setDeviceMetricsOverride', {
      width: options.width, height: options.height + 36, deviceScaleFactor: 1, mobile: false,
      screenWidth: options.width, screenHeight: options.height + 36,
    }, sessionId);
    const url = benchmarkUrl(options, mode, repetition);
    await connection.request('Page.navigate', { url }, sessionId);
    await waitForPageContext(connection, sessionId, url, options.timeoutMs);
    const expression = `new Promise(resolve => {
      const deadline = Date.now() + ${options.timeoutMs};
      const inspect = () => {
        const state = window.t850 || {};
        if (state.state === 'failed') resolve({ ok: false,
          error: state.lastError?.message || state.errors?.at(-1) || 'Browser runtime failed',
          recentLogs: state.logs?.slice(-12) || [] });
        else if (state.offlineBenchmark) resolve({ ok: true, benchmark: state.offlineBenchmark,
          renderSize: state.renderSize || [] });
        else if (Date.now() >= deadline) resolve({ ok: false, error: 'Browser benchmark timed out' });
        else setTimeout(inspect, 100);
      };
      inspect();
    })`;
    const evaluation = await connection.request('Runtime.evaluate', {
      expression, awaitPromise: true, returnByValue: true,
    }, sessionId);
    if (evaluation.exceptionDetails) throw new Error(evaluation.exceptionDetails.text);
    const result = evaluation.result.value;
    if (!result?.ok) throw new Error(`${String(result?.error ?? 'Browser benchmark failed')}\n${(result?.recentLogs ?? []).join('\n')}`);
    const benchmark = result.benchmark;
    if (benchmark.frames !== options.frames || benchmark.presents !== 0 ||
        !Number.isFinite(benchmark.elapsedMs) || benchmark.elapsedMs <= 0 ||
        !Number.isFinite(benchmark.completedFps) || benchmark.completedFps <= 0)
      throw new Error(`Invalid browser benchmark result: ${JSON.stringify(benchmark)}`);
    if (result.renderSize?.[0] !== options.width || result.renderSize?.[1] !== options.height)
      throw new Error(`Browser render size mismatch: ${JSON.stringify(result.renderSize)}`);
    return {
      cell: `browser-wgsl-${mode}`, repetition, frames: benchmark.frames,
      elapsedMs: benchmark.elapsedMs, completedFps: benchmark.completedFps,
      frameMs: benchmark.elapsedMs / benchmark.frames, presents: benchmark.presents,
      renderSize: result.renderSize, url, browser: browserMetadata,
    };
  } finally {
    if (targetId) await connection.request('Target.closeTarget', { targetId }).catch(() => {});
    await connection.request('Target.disposeBrowserContext', { browserContextId }).catch(() => {});
  }
}

const options = parseArguments(process.argv);
await mkdir(dirname(options.output), { recursive: true });
const result = {
  schema: 1, startedUtc: new Date().toISOString(), passed: false,
  machine: options.machine, architecture: options.architecture,
  browser: 'Microsoft Edge', shaderFlow: 'wgsl', repetitions: options.repetitions,
  frames: options.frames, holdFrame: options.holdFrame, width: options.width, height: options.height,
  metric: 'GPU-drained browser WebGPU SubmitNoPresent throughput; CPU recording and submission are included',
  runs: [],
};
let connection;
try {
  const identityResponse = await fetch(new URL('__t850', options.baseUrl));
  if (!identityResponse.ok) throw new Error(`Cannot read T850 browser runtime identity: HTTP ${identityResponse.status}`);
  result.runtimeIdentity = (await identityResponse.json()).identity;
  const connected = await connect(options.endpoint);
  connection = connected.connection;
  result.cdpVersion = connected.version;
  result.browserCapability = await captureBrowserMetadata(connection, options);
  for (let repetition = 1; repetition <= options.repetitions; ++repetition) {
    const modes = repetition % 2 === 0 ? ['compute', 'raster'] : ['raster', 'compute'];
    for (const mode of modes) result.runs.push(await captureRun(connection, options, mode, repetition, result.browserCapability));
  }
  result.gpuTimestampStatus = result.browserCapability.writeTimestampMethod ? 'available' : 'capability-blocked';
  result.passed = true;
} catch (error) {
  result.failure = error?.stack ?? String(error);
} finally {
  connection?.close();
  result.completedUtc = new Date().toISOString();
  await writeFile(options.output, JSON.stringify(result, null, 2) + '\n');
}
if (!result.passed) throw new Error(result.failure);
console.log(`PASS: Edge offline GPU matrix at ${options.output}`);