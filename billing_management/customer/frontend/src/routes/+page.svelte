<script lang="ts">
  import BillingPreview from '@billing/ui/components/BillingPreview.svelte';
  import { loadQuoteEngine } from '../lib/wasm/quote';

  const features = ["ประวัติการชาร์จของตัวเอง","ใบแจ้งหนี้และใบเสร็จ","สถานะการชำระเงิน"];
</script>

<BillingPreview
  area="customer"
  title="Your charging, clearly."
  subtitle="พื้นที่ลูกค้าสำหรับติดตามค่าใช้จ่ายการชาร์จ"
  {features}
  loadEngine={loadQuoteEngine}
/>
