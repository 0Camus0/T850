const status = document.getElementById('status');
const container = document.getElementById('scenes');
const resolution = document.getElementById('resolution');
let ready = false;
function updateLinks() {
  const [width, height] = resolution.value.split('x');
  for (const link of container.querySelectorAll('a')) {
    const url = new URL('DayScene.html', location.href);
    url.search = new URLSearchParams({ scene: link.dataset.scene, width, height, culling: 'frustum', logLevel: 'error', entry: 'launcher' });
    link.href = url.href;
    link.setAttribute('aria-disabled', String(!ready));
  }
}
try {
  const response = await fetch('scenes.json');
  if (!response.ok) throw new Error('Scene catalog unavailable');
  const catalog = await response.json();
  for (const scene of catalog.scenes) {
    const link = document.createElement('a');
    link.className = 'scene';
    link.dataset.scene = String(scene.id);
    const image = document.createElement('img');
    image.src = `previews/${scene.id}.png`;
    image.alt = scene.name;
    image.width = 960;
    image.height = 540;
    const footer = document.createElement('footer');
    const name = document.createElement('h2');
    name.textContent = scene.name;
    const play = document.createElement('span');
    play.className = 'play';
    play.textContent = 'Play';
    footer.append(name, play);
    link.append(image, footer);
    link.addEventListener('click', event => { if (!ready) event.preventDefault(); });
    container.append(link);
  }
  updateLinks();
  if (!crossOriginIsolated) throw new Error('Cross-origin isolation unavailable');
  if (!navigator.gpu || !await navigator.gpu.requestAdapter()) throw new Error('WebGPU unavailable');
  ready = true;
  status.textContent = 'WebGPU ready';
  updateLinks();
} catch (error) {
  status.textContent = error.message;
}
resolution.addEventListener('change', updateLinks);