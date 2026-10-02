<script lang="ts">
  import BillingPreview from '@billing/ui/components/BillingPreview.svelte';
  import { loadQuoteEngine } from '../lib/wasm/quote';

  const features = ["Tariffs และกฎการคิดเงิน","Invoices และการตรวจสอบรายการ","Reconciliation และ audit"];
</script>

<BillingPreview
  area="management"
  title="Billing workspace"
  subtitle="พื้นที่ดูแลระบบ billing สำหรับทีมงาน"
  {features}
  loadEngine={loadQuoteEngine}
/>
