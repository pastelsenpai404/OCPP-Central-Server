import { copyFile, mkdir, readFile } from 'node:fs/promises';

const root = new URL('../', import.meta.url);
// Generated assets contain no secrets. Never copy config files or native binaries.
for (const area of ['management', 'customer']) {
  const source = new URL(`build/wasm/${area}/frontend/dist/`, root);
  const destination = new URL(`${area}/frontend/static/wasm/`, root);
  try {
    const bytes = await readFile(new URL('billing.wasm', source));
    if (bytes[0] !== 0 || bytes[1] !== 0x61 || bytes[2] !== 0x73 || bytes[3] !== 0x6d) {
      throw new Error('Invalid WebAssembly binary');
    }
    await mkdir(destination, { recursive: true });
    for (const file of ['billing.js', 'billing.wasm']) {
      await copyFile(new URL(file, source), new URL(file, destination));
    }
    console.log(`${area}: WASM assets prepared`);
  } catch (error) {
    throw new Error(`${area}: build C++ WASM first (scripts/build-wasm.ps1 or emcmake on Linux).`, { cause: error });
  }
}
