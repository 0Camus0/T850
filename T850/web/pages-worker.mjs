const isolationHeaders = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
  'Cross-Origin-Resource-Policy': 'same-origin',
  'X-Content-Type-Options': 'nosniff',
};

export function createAssetHandler(routes) {
  return async function fetchAsset(request, env) {
    const url = new URL(request.url);
    const finish = response => {
      const headers = new Headers(response.headers);
      for (const [name, value] of Object.entries(isolationHeaders)) headers.set(name, value);
      return new Response(request.method === 'HEAD' ? null : response.body, {
        status: response.status, headers,
      });
    };
    if (!['GET', 'HEAD'].includes(request.method))
      return finish(new Response('Method not allowed', { status: 405, headers: { Allow: 'GET, HEAD' } }));
    let path;
    try { path = decodeURIComponent(url.pathname); }
    catch { return finish(new Response('Invalid path', { status: 400 })); }
    if (path.includes('\\') || path.split('/').some(part => part === '..' || part === '.'))
      return finish(new Response('Invalid path', { status: 400 }));
    if (!path.startsWith('/assets/')) return finish(await env.ASSETS.fetch(request));
    const route = routes[path.slice('/assets/'.length)];
    if (!route || !Object.hasOwn(routes, path.slice('/assets/'.length)))
      return finish(new Response('Asset not found', { status: 404 }));
    if (route.static) return finish(await env.ASSETS.fetch(request));
    try {
      if (env.ASSET_SOURCE === 'public-mirror' && route.url) {
        const headers = new Headers();
        for (const name of ['Range', 'If-None-Match']) {
          if (request.headers.has(name)) headers.set(name, request.headers.get(name));
        }
        const response = await globalThis.fetch(route.url, { method: request.method, headers, redirect: 'manual' });
        if (![200, 206, 304, 404, 416].includes(response.status)) throw new Error('Upstream failure');
        const safeHeaders = new Headers();
        for (const name of ['Content-Length', 'Content-Range', 'ETag', 'Accept-Ranges']) {
          if (response.headers.has(name)) safeHeaders.set(name, response.headers.get(name));
        }
        safeHeaders.set('Content-Type', route.contentType ?? 'application/octet-stream');
        safeHeaders.set('Cache-Control', 'public, max-age=300');
        return finish(new Response(response.body, { status: response.status, headers: safeHeaders }));
      }
      const bucket = env[route.binding];
      if (!bucket) throw new Error('Missing bucket binding');
      const object = request.method === 'HEAD'
        ? await bucket.head(route.key)
        : await bucket.get(route.key);
      if (!object) return finish(new Response('Asset not found', { status: 404 }));
      const headers = new Headers({
        'Content-Type': route.contentType ?? 'application/octet-stream',
        'Content-Length': String(object.size),
        'Cache-Control': 'public, max-age=300',
        ETag: object.httpEtag,
      });
      if (request.headers.get('If-None-Match') === object.httpEtag)
        return finish(new Response(null, { status: 304, headers }));
      return finish(new Response(object.body ?? null, { headers }));
    } catch (error) {
      console.error('Asset request failed', route.key, String(error));
      return finish(new Response('Asset unavailable', { status: 502, headers: { 'Cache-Control': 'no-store' } }));
    }
  };
}