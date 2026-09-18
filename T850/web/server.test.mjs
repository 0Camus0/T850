import assert from 'node:assert/strict';
import { test } from 'node:test';
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { mkdtemp, mkdir, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { once } from 'node:events';

test('browser launch isolates ports, reuses matching servers and preserves URL data', async () => {
  const root = await mkdtemp(join(tmpdir(), 'T850 browser server '));
  const processes = [];
  const blocker = createServer((request, response) => response.end('{}'));
  try {
    for (const directory of ['site', 'assets', 'shaders']) await mkdir(join(root, directory));
    for (const file of ['DayScene.html', 'DayScene.js', 'DayScene.wasm', 'scenes.json']) await writeFile(join(root, 'site', file), 'test');
    await writeFile(join(root, 'site', 'icon.svg'), '<svg xmlns="http://www.w3.org/2000/svg"/>');
    await writeFile(join(root, 'assets', 'model with spaces.glb'), 'asset');
    await writeFile(join(root, 'shaders', 'test.json'), '{"version":1}');
    blocker.listen(0, '127.0.0.1');
    await once(blocker, 'listening');
    const port = blocker.address().port;
    const query = new URLSearchParams({ scene: '4', sceneFile: 'Scenes/Test & map.t8scene' }).toString();
    const args = ['--site', join(root, 'site'), '--assets', join(root, 'assets'), '--shaders', join(root, 'shaders'), '--port', String(port), '--open', '--query', query];
    const launch = browser => {
      const launchArgs = browser ? [...args, '--browser', browser] : args;
      const wrapper = `import childProcess from 'node:child_process';
      import { syncBuiltinESMExports } from 'node:module';
      childProcess.execFile = (file, args, done) => { console.log('OPEN ' + JSON.stringify({ file, args })); done(null); };
      syncBuiltinESMExports();
      process.argv = [process.execPath, ...${JSON.stringify(launchArgs)}];
      await import(${JSON.stringify(new URL('./server.mjs', import.meta.url).href)});`;
      const child = spawn(process.execPath, ['--input-type=module', '--eval', wrapper], { stdio: ['ignore', 'pipe', 'pipe'] });
      processes.push(child);
      return new Promise((resolveLaunch, rejectLaunch) => {
        let output = '';
        const timeout = setTimeout(() => rejectLaunch(new Error('Server launch timed out: ' + output)), 10000);
        child.on('error', error => { clearTimeout(timeout); rejectLaunch(error); });
        child.stderr.on('data', data => { output += data; });
        child.stdout.on('data', data => {
          output += data;
          const opened = output.match(/^OPEN (.+)$/m);
          if (opened) {
            clearTimeout(timeout);
            const command = JSON.parse(opened[1]);
            resolveLaunch({ child, url: new URL(command.args.at(-1)), command, output });
          }
        });
        child.on('exit', code => { clearTimeout(timeout); if (code !== 0) rejectLaunch(new Error(output)); });
      });
    };
    const first = await launch();
    assert.notEqual(Number(first.url.port), port);
    assert.equal(first.url.searchParams.get('sceneFile'), 'Scenes/Test & map.t8scene');
    const response = await fetch(first.url);
    assert.equal(response.headers.get('cross-origin-opener-policy'), 'same-origin');
    assert.equal(response.headers.get('cross-origin-embedder-policy'), 'require-corp');
    assert.equal(await response.text(), 'test');
    const icon = await fetch(new URL('icon.svg', first.url));
    assert.equal(icon.headers.get('content-type'), 'image/svg+xml');
    const reused = await launch(process.execPath);
    assert.equal(reused.url.port, first.url.port);
    assert.match(reused.output, /Reusing T850/);
    assert.equal(reused.command.file, process.execPath);
    assert.deepEqual(reused.command.args, [reused.url.href]);
    await writeFile(join(root, 'assets', 'model with spaces.glb'), 'fresh');
    await writeFile(join(root, 'shaders', 'test.json'), '{"version":2}');
    const updated = await launch(process.execPath);
    assert.equal(updated.url.port, first.url.port);
    assert.match(updated.output, /Reusing T850/);
    const asset = await fetch(new URL('assets/model%20with%20spaces.glb', updated.url));
    assert.equal(asset.headers.get('cache-control'), 'no-cache');
    assert.equal(await asset.text(), 'fresh');
    const shader = await fetch(new URL('assets/WebShaders/test.json', updated.url));
    assert.deepEqual(await shader.json(), { version: 2 });
    await writeFile(join(root, 'assets', 'new.glb'), 'new asset');
    const refreshed = await launch(process.execPath);
    assert.notEqual(refreshed.url.port, first.url.port);
    assert.equal(refreshed.command.file, process.execPath);
    const index = await fetch(new URL('assets/index.json', refreshed.url)).then(result => result.json());
    assert.deepEqual(index, ['WebShaders/test.json', 'model with spaces.glb', 'new.glb']);
    await assert.rejects(launch(join(root, 'missing browser.exe')), /Browser executable missing/);
  } finally {
    for (const child of processes) {
      if (child.exitCode === null) { const exited = once(child, 'exit'); child.kill(); await exited; }
    }
    blocker.close();
    await rm(root, { recursive: true, force: true });
  }
});