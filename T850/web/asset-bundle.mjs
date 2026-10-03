import { unzipSync } from 'fflate';

export function mountAssetBundle(filesystem, archive) {
  const files = unzipSync(archive);
  const catalog = files['index.json'];
  if (!catalog) throw new Error('Bundled asset catalog is missing');
  const resources = JSON.parse(new TextDecoder().decode(catalog));
  if (!Array.isArray(resources)) throw new Error('Invalid bundled asset catalog');
  const names = new Set();
  for (const resource of [...resources, 'index.json']) {
    if (typeof resource !== 'string' || !resource || resource.includes('\\') || resource.includes(':') ||
        resource.split('/').some(part => !part || part === '.' || part === '..') || names.has(resource.toLowerCase()))
      throw new Error('Unsafe or duplicate bundled resource');
    names.add(resource.toLowerCase());
    if (!Object.hasOwn(files, resource)) throw new Error(`Missing bundled resource: ${resource}`);
  }
  if (names.size !== Object.keys(files).length) throw new Error('Uncatalogued bundled resource');
  let bytes = 0;
  for (const [resource, data] of Object.entries(files)) {
    const path = '/assets/' + resource;
    filesystem.mkdirTree(path.slice(0, path.lastIndexOf('/')));
    filesystem.writeFile(path, data, { canOwn: true });
    bytes += data.byteLength;
  }
  return { resources: resources.length, bytes, archiveBytes: archive.byteLength };
}

export async function loadAssetBundle(filesystem, name) {
  if (!/^minecraft-assets\.[a-f0-9]{64}\.zip$/.test(name)) throw new Error('Invalid asset bundle name');
  const response = await fetch(name);
  if (!response.ok) throw new Error(`Cannot load bundled scene assets (${response.status})`);
  const archive = new Uint8Array(await response.arrayBuffer());
  const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', archive)), value => value.toString(16).padStart(2, '0')).join('');
  if (!name.includes(digest)) throw new Error('Bundled scene assets failed integrity verification');
  return mountAssetBundle(filesystem, archive);
}