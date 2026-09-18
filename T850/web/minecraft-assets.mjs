export function minecraftAssetSelection(scene) {
  const required = new Set([
    'Scenes/Minecraft.t8scene', scene.render_graph, scene.control_descriptor,
    'Fonts/Martius-LV9L4.ttf', 'Fonts/tahomabd.ttf',
    'Layouts/imgui_runtime_layout.ini',
    `Textures/${scene.voxel_world.atlas_texture}`,
    ...(scene.voxel_world.mob?.skin_texture ? [`Textures/${scene.voxel_world.mob.skin_texture}`] : []),
    ...[scene.voxel_world.environment_map, ...scene.voxel_world.environment_options].map(path => `Textures/${path}`),
  ]);
  for (const path of required) {
    if (typeof path !== 'string' || !path || path.includes('\\') || path.startsWith('/') || path.split('/').some(part => part === '..' || part === '.'))
      throw new Error('Unsafe Minecraft dependency');
  }
  return {
    required,
    includes(resource) {
      if (resource.startsWith('Models/') || resource === 'model-cloud-manifest.json') return false;
      return required.has(resource) || resource.startsWith('Shaders/') || resource.startsWith('WebShaders/') ||
        /^Textures\/GeneratedIBLCache\/[a-z0-9_]+\.t8ibl$/.test(resource) ||
        /^Textures\/LUT\/lut_(charlie|ggx|sheen_E)\.dds$/.test(resource) ||
        /^Textures\/lens[1-9]\.png$/.test(resource);
    },
  };
}