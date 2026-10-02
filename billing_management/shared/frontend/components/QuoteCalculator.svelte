<script lang="ts">
  import { onMount } from 'svelte';
  import type { QuoteEngine } from '../wasm/quote';

  let { loadEngine }: { loadEngine: () => Promise<QuoteEngine> } = $props();
  let engine = $state<QuoteEngine | null>(null);
  let energyWh = $state<number | undefined>(1500);
  let rate = $state<number | undefined>(750);
  let status = $state('กำลังโหลด WebAssembly…');
  let result = $state('—');
  let failed = $state(false);

  onMount(() => {
    let cancelled = false;
    loadEngine().then((loaded) => {
      if (cancelled) return;
      engine = loaded;
      status = 'WebAssembly ready · Sandbox';
    }).catch(() => {
      if (cancelled) return;
      failed = true;
      status = 'โหลด WebAssembly ไม่สำเร็จ กรุณา build C++ แล้ว sync assets อีกครั้ง';
    });
    return () => { cancelled = true; };
  });

  function calculate(event: SubmitEvent) {
    event.preventDefault();
    if (!engine) return;
    try {
      const subtotal = engine.previewSatang(energyWh ?? Number.NaN, rate ?? Number.NaN);
      result = new Intl.NumberFormat('th-TH', { style: 'currency', currency: 'THB' }).format(subtotal / 100);
    } catch {
      result = 'กรุณากรอกจำนวนเต็มภายในช่วงที่กำหนด';
    }
  }
</script>

<section class="card">
  <h2>ลองคำนวณค่าไฟ</h2>
  <p id="wasm-status" role="status" class:error={failed}>{status}</p>
  <form id="quote-form" onsubmit={calculate}>
    <label for="energy">พลังงาน (Wh) · 1,000 Wh = 1 kWh</label>
    <input id="energy" name="energy" type="number" min="0" max="1000000000" step="1" bind:value={energyWh} required>
    <label for="rate">อัตรา (สตางค์/kWh) · 750 = 7.50 บาท/kWh</label>
    <input id="rate" name="rate" type="number" min="0" max="1000000" step="1" bind:value={rate} required>
    <button type="submit" disabled={!engine}>คำนวณตัวอย่าง</button>
    <output id="result" aria-live="polite">{result}</output>
  </form>
  <p>ยอดตัวอย่างเฉพาะค่าไฟ ยังไม่รวมภาษี ส่วนลด หรือค่าธรรมเนียม</p>
</section>
