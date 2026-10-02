export interface QuoteEngine {
  previewSatang(energyWh: number, satangPerKwh: number): number;
}

interface BillingModule {
  _billing_preview_satang(energyWh: number, satangPerKwh: number): number;
}

export async function loadQuoteEngine(assetBase = '/wasm'): Promise<QuoteEngine> {
  // Called on mount only. Emscripten's browser runtime must never execute during SSR.
  const moduleUrl = `${assetBase}/billing.js`;
  const { default: createModule } = await import(/* @vite-ignore */ moduleUrl) as {
    default: (options: { locateFile: (file: string) => string }) => Promise<BillingModule>;
  };
  const module = await createModule({ locateFile: (file) => `${assetBase}/${file}` });
  return {
    previewSatang(energyWh, satangPerKwh) {
      if (!Number.isInteger(energyWh) || energyWh < 0 || energyWh > 1_000_000_000 ||
          !Number.isInteger(satangPerKwh) || satangPerKwh < 0 || satangPerKwh > 1_000_000) {
        throw new RangeError('Quote inputs must be bounded integers');
      }
      const subtotal = module._billing_preview_satang(energyWh, satangPerKwh);
      if (!Number.isSafeInteger(subtotal) || subtotal < 0) {
        throw new RangeError('C++ quote validation failed');
      }
      return subtotal;
    }
  };
}
