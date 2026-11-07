// Assets the Backyard water scene (scene 7) reads in the browser: its four scene files, the pool
// layout and meshes, the water normal map, every ball map the palette can pick, the authored sky
// and the shared rendering resources. The water site publishes these and nothing else.
const backyardMeshes = ['Ground', 'House', 'Pool', 'Wall', 'Props', 'Water', 'Ball'];

export function backyardAssetSelection(scene, palette) {
  const required = new Set([
    'Scenes/BackyardScene.json', 'Scenes/BackyardScene_RenderGraph.json',
    'Scenes/BackyardWater.json', 'Scenes/BackyardBallMaterials.json',
    'Models/Backyard/backyard_layout.json',
    ...backyardMeshes.map(name => `Models/Backyard/Backyard_${name}.glb`),
    'Textures/Backyard/WaterNormal.png',
    `Textures/${scene.environment_map}`,
    ...palette.materials.flatMap(material => Object.values(palette.maps).map(map => `Textures/Ball/${material.id}/${map}`)),
    'Fonts/Martius-LV9L4.ttf', 'Fonts/tahomabd.ttf',
    'Layouts/imgui_runtime_layout.ini',
  ]);
  for (const path of required) {
    if (typeof path !== 'string' || !path || path.includes('\\') || path.startsWith('/') ||
        path.split('/').some(part => part === '' || part === '..' || part === '.'))
      throw new Error('Unsafe Backyard dependency');
  }
  return {
    required,
    includes(resource) {
      return required.has(resource) || resource.startsWith('Shaders/') || resource.startsWith('WebShaders/') ||
        /^Textures\/GeneratedIBLCache\/[a-z0-9_]+\.t8ibl$/.test(resource) ||
        /^Textures\/LUT\/lut_(charlie|ggx|sheen_E)\.dds$/.test(resource) ||
        /^Textures\/lens[1-9]\.png$/.test(resource);
    },
  };
}
