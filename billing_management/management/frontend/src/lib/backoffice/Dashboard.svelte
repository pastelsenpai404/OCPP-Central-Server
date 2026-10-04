<script lang="ts">
  import { money, type Row } from "./api";
  let {
    overview,
    navigate,
  }: { overview: Row; navigate: (key: string) => Promise<void> } = $props();
</script>

<div class="stat-grid">
  <section class="stat-card">
    <span>สถานีในระบบ</span><strong>{overview.counts?.stations ?? 0}</strong
    ><small
      >{overview.counts?.chargers ?? 0} เครื่อง · {overview.counts
        ?.connectors ?? 0} หัวชาร์จ</small
    >
  </section>
  <section class="stat-card">
    <span>ลูกค้า</span><strong>{overview.counts?.customers ?? 0}</strong><small
      >บัญชีในฐานข้อมูลทดสอบ</small
    >
  </section>
  <section class="stat-card">
    <span>พลังงานทั้งหมด</span><strong
      >{((overview.totals?.energy_wh ?? 0) / 1000).toLocaleString()}<em>
        kWh</em
      ></strong
    ><small>{overview.counts?.sessions ?? 0} รายการชาร์จ</small>
  </section>
  <section class="stat-card">
    <span>ค่าชาร์จก่อนภาษี</span><strong
      >{money(overview.totals?.subtotal_satang)}</strong
    ><small>คืนเงิน {money(overview.refund_satang)}</small>
  </section>
</div>
<div class="dashboard-grid">
  <section class="panel start-panel">
    <span class="feature-icon">↗</span>
    <h2>Build your charging network.</h2>
    <p>
      เริ่มจากบริษัทและโครงการ กำหนด tariff<br />แล้วเพิ่มสถานี เครื่องชาร์จ
      และหัวชาร์จ
    </p>
    <div class="quick-links">
      <button class="secondary" onclick={() => navigate("companies")}
        >บริษัทและโครงการ →</button
      ><button class="secondary" onclick={() => navigate("stations")}
        >จัดการสถานี →</button
      >
    </div>
    <p class="muted">
      สถานะเครื่องเป็นทะเบียนที่บันทึกโดยทีมงาน ยังไม่ใช่ telemetry สดจาก CSMS
    </p>
  </section>
  <section class="panel">
    <div class="section-heading">
      <h2>กิจกรรมล่าสุด</h2>
      <button class="text-button" onclick={() => navigate("audit")}
        >ดูทั้งหมด →</button
      >
    </div>
    {#each overview.recent ?? [] as event}<div class="activity-row">
        <span class="activity-dot"></span>
        <div>
          <strong>{event.action} · {event.kind} #{event.record_id}</strong
          ><small>{event.actor} · {event.created_at}</small>
        </div>
      </div>{:else}<div class="empty-state compact">
        <span>○</span>
        <h3>ยังไม่มีกิจกรรม</h3>
        <p>รายการที่สร้างและแก้ไขจะแสดงที่นี่</p>
      </div>{/each}
  </section>
</div>
<section class="panel flow-panel">
  <h2>From charging to settlement.</h2>
  <div class="workflow">
    <button onclick={() => navigate("sessions")}
      ><b>01</b><span>บันทึกการชาร์จ</span></button
    ><span>→</span><button onclick={() => navigate("bills")}
      ><b>02</b><span>สร้างบิล</span></button
    ><span>→</span><button onclick={() => navigate("wallet")}
      ><b>03</b><span>ชำระ / กระเป๋าเงิน</span></button
    ><span>→</span><button onclick={() => navigate("tax-invoices")}
      ><b>04</b><span>เอกสารทดสอบ</span></button
    >
  </div>
</section>
