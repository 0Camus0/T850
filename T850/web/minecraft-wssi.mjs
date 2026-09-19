const form = document.getElementById('launch-form');
const enemies = document.getElementById('enemies');
const resolution = document.getElementById('resolution');
const consoleLogs = document.getElementById('console-logs');
const launch = document.getElementById('launch');
const status = document.getElementById('status');
const savedReport = document.getElementById('saved-report');
const savedReportText = document.getElementById('saved-report-text');
const savedReportFeedback = document.getElementById('saved-report-feedback');
function refreshSavedDiagnostics() {
  const report = {};
  for (const [field, key, limit] of [
    ['latestCheckpoint', 't850:last-session:v1', 16000],
    ['previousSession', 't850:previous-session:v1', 16000],
    ['lastError', 't850:last-runtime-error:v1', 40000],
  ]) {
    try {
      const stored = sessionStorage.getItem(key);
      if (!stored || stored.length >= limit) continue;
      const value = JSON.parse(stored);
      if (field === 'lastError' ? typeof value?.message === 'string' : value?.version === 1 && typeof value.capturedAt === 'string')
        report[field] = value;
    } catch {}
  }
  savedReport.hidden = Object.keys(report).length === 0;
  savedReportText.textContent = savedReport.hidden ? '' : JSON.stringify({ source: 'Saved T850 browser diagnostics',
    reloadCause: 'Unknown; a checkpoint is not proof of a crash or memory exhaustion', ...report }, null, 2);
}
document.getElementById('saved-report-download').addEventListener('click', () => {
  refreshSavedDiagnostics();
  if (savedReport.hidden) return;
  try {
    const url = URL.createObjectURL(new Blob([savedReportText.textContent], { type: 'application/json' }));
    const link = document.createElement('a');
    link.href = url;
    link.download = 't850-saved-diagnostics.json';
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    savedReportFeedback.textContent = 'Download requested';
  } catch { savedReportFeedback.textContent = 'Download unavailable'; }
});
refreshSavedDiagnostics();
let ready = false;
let starting = false;
const resolutions = new Set(['960x540', '1280x720', '1920x1080', '540x960', '720x1280', '1080x1920']);
const portrait = window.matchMedia('(max-width: 700px) and (orientation: portrait)');
const saved = new URLSearchParams(location.search);
consoleLogs.checked = saved.get('logLevel') === 'trace';
const savedCount = saved.get('minecraftEnemyCount');
if (savedCount !== null && /^\d+$/.test(savedCount) && Number(savedCount) <= 8) enemies.value = savedCount;
if (resolutions.has(saved.get('resolution'))) {
  const dimensions = saved.get('resolution').split('x').map(Number).sort((first, second) => second - first);
  resolution.value = dimensions.join('x');
}
function updateResolutionOptions() {
  const selectedLongSide = Math.max(...resolution.value.split('x').map(Number));
  for (const option of resolution.options) {
    const longSide = Math.max(...option.value.split('x').map(Number));
    const shortSide = longSide * 9 / 16;
    const dimensions = portrait.matches ? [shortSide, longSide] : [longSide, shortSide];
    option.value = dimensions.join('x');
    option.textContent = dimensions.join(' x ');
  }
  resolution.value = (portrait.matches ? [selectedLongSide * 9 / 16, selectedLongSide] : [selectedLongSide, selectedLongSide * 9 / 16]).join('x');
}
updateResolutionOptions();
portrait.addEventListener('change', updateResolutionOptions);

form.addEventListener('submit', event => {
  event.preventDefault();
  if (!ready || starting || !form.reportValidity()) return;
  const count = enemies.valueAsNumber;
  if (!/^\d+$/.test(enemies.value) || !Number.isInteger(count) || count < 0 || count > 8 || !resolutions.has(resolution.value)) return;
  const [width, height] = resolution.value.split('x');
  const url = new URL('DayScene.html', location.href);
  url.search = new URLSearchParams({ demo: 'wssi', scene: '6', minecraftEnemyCount: String(count), width, height, culling: 'frustum', postProcessMode: 'compute', logLevel: consoleLogs.checked ? 'trace' : 'error' });
  starting = true;
  launch.disabled = true;
  status.textContent = 'Launching Minecraft...';
  location.assign(url.href);
});
window.addEventListener('pageshow', () => { starting = false; launch.disabled = !ready; refreshSavedDiagnostics(); });

try {
  if (!crossOriginIsolated) throw new Error('This browser cannot enable the isolation required by the demo. Open this link in an up-to-date standalone Safari, Chrome, Edge or Firefox browser using the app menu.');
  if (!navigator.gpu) throw new Error('WebGPU is unavailable in this browser.');
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) throw new Error('No WebGPU adapter is available.');
  if (adapter.limits.maxColorAttachments < 7 || adapter.limits.maxColorAttachmentBytesPerSample < 64)
    throw new Error('This GPU does not support the demo render targets.');
  ready = true;
  launch.disabled = false;
  status.textContent = 'Ready';
} catch (error) {
  status.dataset.error = 'true';
  status.textContent = error.message;
}