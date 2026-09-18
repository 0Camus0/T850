const form = document.getElementById('launch-form');
const enemies = document.getElementById('enemies');
const resolution = document.getElementById('resolution');
const launch = document.getElementById('launch');
const status = document.getElementById('status');
let ready = false;
let starting = false;
const resolutions = new Set(['960x540', '1280x720', '1920x1080', '540x960', '720x1280', '1080x1920']);
const portrait = window.matchMedia('(max-width: 700px) and (orientation: portrait)');
const saved = new URLSearchParams(location.search);
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
  url.search = new URLSearchParams({ demo: 'wssi', scene: '6', minecraftEnemyCount: String(count), width, height, culling: 'frustum', logLevel: 'error' });
  starting = true;
  launch.disabled = true;
  status.textContent = 'Launching Minecraft...';
  location.assign(url.href);
});
window.addEventListener('pageshow', () => { starting = false; launch.disabled = !ready; });

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