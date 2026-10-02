<script lang="ts">
  import QuoteCalculator from './QuoteCalculator.svelte';
  import type { QuoteEngine } from '../wasm/quote';

  let { area, title, subtitle, features, loadEngine }: {
    area: string;
    title: string;
    subtitle: string;
    features: string[];
    loadEngine: () => Promise<QuoteEngine>;
  } = $props();
</script>

<svelte:head>
  <title>{title} — Billing sandbox</title>
  <meta name="description" content={subtitle}>
</svelte:head>

<main>
  <header>
    <span class="badge">{area.toUpperCase()} · SANDBOX</span>
    <h1>{title}</h1>
    <p>{subtitle}</p>
  </header>
  <div class="grid">
    <QuoteCalculator {loadEngine} />
    <section class="card">
      <h2>ขอบเขตที่จะพัฒนาต่อ</h2>
      <ul>{#each features as feature (feature)}<li>{feature}</li>{/each}</ul>
      <p>รายการข้างต้นยังไม่ได้เชื่อมระบบจริง หน้านี้เป็นต้นแบบคำนวณด้วย C++ WebAssembly เท่านั้น</p>
    </section>
  </div>
  <footer>โหมดทดสอบ · ไม่มีการเรียกเก็บเงินหรือบันทึกธุรกรรมจริง · Backend ต้องคำนวณและตรวจสิทธิ์อีกครั้งก่อนบันทึก</footer>
</main>
