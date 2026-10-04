<script lang="ts">
  import { openDialog, money, type Row } from "./api";
  let {
    record,
    kind,
    close,
  }: { record: Row; kind: string; close: () => void } = $props();
  const bill = $derived(
    kind === "tax-invoices" ? record.bill_snapshot : record,
  );
</script>

<div class="modal-backdrop">
  <dialog
    class="sheet detail-sheet"
    use:openDialog
    oncancel={(e) => {
      e.preventDefault();
      close();
    }}
    aria-label="รายละเอียดรายการ"
  >
    <div class="section-heading no-print">
      <div>
        <p class="eyebrow">RECORD DETAILS</p>
        <h2>รายละเอียด #{record.id}</h2>
      </div>
      <button class="icon-button" onclick={close} aria-label="ปิด">×</button>
    </div>
    {#if kind === "bills" || kind === "tax-invoices"}
      <article class="document">
        <div class="document-head">
          <strong>EV Billing</strong><span>TEST DOCUMENT</span>
        </div>
        <h2>{kind === "bills" ? `บิลทดสอบ #${record.id}` : record.number}</h2>
        <p>
          {bill.customer_snapshot?.name} · {bill.customer_snapshot?.tax_id ||
            "ไม่ได้ระบุเลขผู้เสียภาษี"}<br />{bill.customer_snapshot?.address}
        </p>
        <p>รอบบิล {bill.period_from} — {bill.period_to} (UTC)</p>
        <table>
          <thead
            ><tr
              ><th>รายการชาร์จ</th><th>พลังงาน</th><th>ยอดสุทธิ</th><th>VAT</th
              ></tr
            ></thead
          ><tbody
            >{#each bill.lines ?? [] as line}<tr
                ><td>#{line.session_id}</td><td
                  >{(line.energy_wh / 1000).toFixed(3)} kWh</td
                ><td>{money(line.subtotal_satang)}</td><td
                  >{money(line.vat_satang)}</td
                ></tr
              >{/each}</tbody
          >
        </table>
        <div class="document-total">
          <p>ยอดก่อนภาษี <strong>{money(bill.subtotal_satang)}</strong></p>
          <p>VAT <strong>{money(bill.vat_satang)}</strong></p>
          <h3>รวม <strong>{money(bill.total_satang)}</strong></h3>
        </div>
        <p class="muted">
          เอกสารทดสอบเท่านั้น ไม่ใช่ใบกำกับภาษีทางกฎหมาย · {bill.status}
        </p>
      </article>
      <button class="no-print secondary" onclick={() => window.print()}
        >พิมพ์ / บันทึก PDF</button
      >
    {:else}<dl>
        {#each Object.entries(record) as [key, value]}<div>
            <dt>{key}</dt>
            <dd>
              {typeof value === "object" && value !== null
                ? JSON.stringify(value)
                : (value ?? "—")}
            </dd>
          </div>{/each}
      </dl>{/if}
  </dialog>
</div>
