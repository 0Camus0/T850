const form = document.getElementById('launch-form');
const resolution = document.getElementById('resolution');
const consoleLogs = document.getElementById('console-logs');
const launch = document.getElementById('launch');
const status = document.getElementById('status');

// Phone, tablet or PC: the same rule as touch-controls.js, which picks the in-game controls
// (pages.test.mjs checks that both agree). Windows and ChromeOS are PCs even with a touch
// screen; a touch-first device is a tablet when its shortest screen side is 600 CSS px or more.
function mobileDevice() {
  const agent = navigator.userAgent ?? '';
  const platform = navigator.userAgentData?.platform ?? navigator.platform ?? '';
  if (/Windows|Win32|Win64|CrOS|Chrome OS/i.test(platform + ' ' + agent)) return false;
  if (/Android|iPhone|iPad|iPod/i.test(platform + ' ' + agent)) return true;
  if (/MacIntel|Macintosh/.test((navigator.platform ?? '') + ' ' + agent) && navigator.maxTouchPoints > 1) return true;
  return navigator.userAgentData?.mobile === true;
}
const screenSize = globalThis.screen;
const device = !mobileDevice() ? 'pc'
  : Math.min(screenSize?.width ?? 0, screenSize?.height ?? 0) >= 600 ? 'tablet' : 'phone';
document.body.dataset.device = device;
document.getElementById('device-label').textContent =
  { pc: 'keyboard and mouse', tablet: 'tablet, touch', phone: 'phone, touch' }[device];
for (const help of document.querySelectorAll('[data-help]')) help.hidden = help.dataset.help !== device;

let ready = false;
let starting = false;
const resolutions = new Set(['960x540', '1280x720', '1920x1080', '540x960', '720x1280', '1080x1920']);
const portrait = window.matchMedia('(max-width: 700px) and (orientation: portrait)');
const saved = new URLSearchParams(location.search);
consoleLogs.checked = saved.get('logLevel') === 'trace';
if (resolutions.has(saved.get('resolution'))) {
  resolution.value = saved.get('resolution').split('x').map(Number).sort((first, second) => second - first).join('x');
} else if (device === 'phone') {
  resolution.value = '960x540';
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
  if (!ready || starting || !resolutions.has(resolution.value)) return;
  const [width, height] = resolution.value.split('x');
  const url = new URL('DayScene.html', location.href);
  url.search = new URLSearchParams({ demo: 'water', scene: '7', width, height, logLevel: consoleLogs.checked ? 'trace' : 'error' });
  starting = true;
  launch.disabled = true;
  status.textContent = 'Launching the water simulation...';
  location.assign(url.href);
});
window.addEventListener('pageshow', () => { starting = false; launch.disabled = !ready; });

try {
  if (!crossOriginIsolated) throw new Error('This browser cannot enable the isolation the simulation needs. Open this link in an up-to-date standalone Safari, Chrome, Edge or Firefox browser using the app menu.');
  if (!navigator.gpu) throw new Error('WebGPU is unavailable in this browser.');
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) throw new Error('No WebGPU adapter is available.');
  if (adapter.limits.maxColorAttachments < 7 || adapter.limits.maxColorAttachmentBytesPerSample < 64)
    throw new Error('This GPU does not support the simulation render targets.');
  ready = true;
  launch.disabled = false;
  status.textContent = 'Ready';
} catch (error) {
  status.dataset.error = 'true';
  status.textContent = error.message;
}
