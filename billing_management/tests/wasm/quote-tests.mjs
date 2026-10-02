import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

for (const area of ['management', 'customer']) {
  const directory = new URL(`../../build/wasm/${area}/frontend/dist/`, import.meta.url);
  const { default: createModule } = await import(new URL('billing.js', directory));
  const module = await createModule({ wasmBinary: await readFile(new URL('billing.wasm', directory)) });
  assert.equal(module._billing_preview_satang(1500, 750), 1125);
  assert.equal(module._billing_preview_satang(1, 499), 0);
  assert.equal(module._billing_preview_satang(1, 500), 1);
  assert.equal(module._billing_preview_satang(-1, 750), -1);
  assert.equal(module._billing_preview_satang(1000, 1_000_001), -1);
  assert.equal(module._billing_preview_satang(1_000_000_000, 1_000_000), 1_000_000_000_000);
  console.log(`${area}: WASM quote checks passed`);
}
