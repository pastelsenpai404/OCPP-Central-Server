<script lang="ts">
  import { onMount } from "svelte";
  import { openDialog, money, type Api, type Row } from "./api";
  let {
    api,
    action,
    record,
    done,
    close,
  }: {
    api: Api;
    action: string;
    record?: Row;
    done: () => Promise<void>;
    close: () => void;
  } = $props();
  const titles: Record<string, string> = {
    "top-up": "เติมเงินทดสอบ",
    withdraw: "ถอนเงินทดสอบ",
    session: "บันทึกการชาร์จทดสอบ",
    refund: "ปรับลดค่าชาร์จก่อนออกบิล",
    bill: "สร้างบิล",
    pay: "บันทึกชำระบิล",
    tax: "ออกใบกำกับภาษีทดสอบ",
  };
  let customers: Row[] = $state([]),
    connectors: Row[] = $state([]);
  function initialCustomer() {
    return record?.customer_id ?? "";
  }
  let customer = $state(initialCustomer()),
    connector = $state(""),
    amount = $state(0),
    energy = $state(10000),
    reference = $state(""),
    reason = $state("");
  let from = $state(new Date().toISOString().slice(0, 8) + "01"),
    to = $state(new Date().toISOString().slice(0, 10)),
    payment = $state("manual");
  let error = $state(""),
    busy = $state(false),
    ready = $state(false),
    payload = $state<Row | null>(null);
  const key = crypto.randomUUID();
  onMount(() => {
    void (async () => {
      try {
        for (const kind of ["customers", "connectors"]) {
          const items: Row[] = [];
          for (let offset = 0; offset < 1000; offset += 100) {
            const result = await api.request(`/${kind}?offset=${offset}`);
            items.push(...result.items);
            if (items.length >= result.total) break;
          }
          if (kind === "customers") customers = items;
          else connectors = items;
        }
        ready = true;
      } catch (e) {
        error = e instanceof Error ? e.message : String(e);
      }
    })();
  });
  async function submit(event: SubmitEvent) {
    event.preventDefault();
    busy = true;
    error = "";
    try {
      let path = "";
      let data: Row = {};
      if (action === "top-up" || action === "withdraw") {
        path = `/wallet/${action}`;
        data = {
          customer_id: Number(customer),
          amount_satang: amount,
          reference,
        };
      }
      if (action === "session") {
        path = "/sessions";
        data = {
          customer_id: Number(customer),
          connector_id: Number(connector),
          energy_wh: energy,
          reference,
        };
      }
      if (action === "refund") {
        path = `/sessions/${record?.id}/refund`;
        data = { amount_satang: amount, reason };
      }
      if (action === "bill") {
        path = "/bills";
        data = {
          customer_id: Number(customer),
          period_from: from,
          period_to: to,
        };
      }
      if (action === "pay") {
        path = `/bills/${record?.id}/pay`;
        data = { method: payment, reference };
      }
      if (action === "tax") {
        path = `/bills/${record?.id}/issue-tax`;
      }
      // Preserve exact payload and key when network outcome is uncertain.
      payload ??= data;
      await api.request(path, "POST", payload, key);
      await done();
      close();
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      busy = false;
    }
  }
</script>

<div class="modal-backdrop">
  <dialog
    class="sheet"
    use:openDialog
    oncancel={(e) => {
      e.preventDefault();
      if (!busy) close();
    }}
    aria-label={titles[action]}
  >
    <div class="section-heading">
      <div>
        <p class="eyebrow">SANDBOX FINANCE</p>
        <h2>{titles[action]}</h2>
      </div>
      <button
        class="icon-button"
        onclick={close}
        disabled={busy}
        aria-label="ปิด">×</button
      >
    </div>
    <p class="notice">
      รายการนี้ใช้ฐานข้อมูลทดสอบ ไม่ติดต่อธนาคารหรือเรียกเก็บเงินจริง
    </p>
    {#if error}<p class="notice error" role="alert">
        {error}{payload
          ? " · การลองใหม่จะส่งรายการเดิม หากต้องแก้ข้อมูลให้ปิดแล้วเปิดฟอร์มใหม่"
          : ""}
      </p>{/if}
    <form onsubmit={submit}>
      <fieldset disabled={busy || payload !== null}>
        {#if ["top-up", "withdraw", "session", "bill"].includes(action)}<label
            >ลูกค้า<select aria-label="ลูกค้า" bind:value={customer} required
              ><option value="">เลือกลูกค้า</option
              >{#each customers as row}<option value={row.id}
                  >{row.name} · {row.code}</option
                >{/each}</select
            ></label
          >{/if}
        {#if action === "session"}<label
            >หัวชาร์จ<select bind:value={connector} required
              ><option value="">เลือกหัวชาร์จ</option
              >{#each connectors as row}<option value={row.id}
                  >{row.name} · {row.code}</option
                >{/each}</select
            ></label
          ><label
            >พลังงาน (Wh)<input
              type="number"
              min="1"
              max="1000000000"
              step="1"
              bind:value={energy}
              required
            /></label
          >
          <p class="muted">
            Backend ใช้ tariff ของสถานี ณ เวลาบันทึก และเก็บ snapshot ราคา/VAT
            ในรายการ
          </p>{/if}
        {#if ["top-up", "withdraw", "refund"].includes(action)}<label
            >จำนวนเงิน (สตางค์)<input
              type="number"
              min="1"
              max="100000000"
              step="1"
              bind:value={amount}
              required
            /></label
          >
          <p class="amount-preview">{money(amount)}</p>{/if}
        {#if action === "refund"}<label
            >เหตุผลการปรับลด<input
              bind:value={reason}
              maxlength="200"
              required
            /></label
          >
          <p class="muted">
            รายการชาร์จเป็นแบบจ่ายภายหลัง ยอดนี้หักจากบิลที่จะสร้าง
            ไม่เพิ่มเงินในกระเป๋าซ้ำ รายการที่ออกบิลแล้วต้องใช้ credit note
            ซึ่งยังไม่รองรับ
          </p>{/if}
        {#if action === "bill"}<div class="form-grid">
            <label
              >ตั้งแต่วันที่ (UTC)<input
                type="date"
                bind:value={from}
                required
              /></label
            ><label
              >ถึงวันที่ (UTC)<input
                type="date"
                bind:value={to}
                required
              /></label
            >
          </div>
          <p class="muted">
            รวมเฉพาะรายการที่ยังไม่ออกบิล หักเงินคืน และคำนวณ VAT ของแต่ละรายการ
          </p>{/if}
        {#if action === "pay"}<p class="amount-preview">
            ยอดชำระ {money(record?.total_satang)}
          </p>
          <label
            >วิธีชำระ<select bind:value={payment}
              ><option value="manual">บันทึกการชำระทดสอบ</option><option
                value="wallet">หักจากกระเป๋าทดสอบ</option
              ></select
            ></label
          >{/if}
        {#if ["top-up", "withdraw", "session", "pay"].includes(action)}<label
            >เลขอ้างอิงทดสอบ<input
              bind:value={reference}
              maxlength="200"
              required
            /></label
          >{/if}
        {#if action === "tax"}<p>
            สร้างเอกสาร TEST-TAX สำหรับบิล #{record?.id} จำนวน {money(
              record?.total_satang,
            )} เอกสารนี้ไม่ใช่ใบกำกับภาษีที่ใช้ทางกฎหมาย
          </p>{/if}
      </fieldset>
      <div class="actions">
        <button class="secondary" type="button" onclick={close} disabled={busy}
          >ยกเลิก</button
        ><button disabled={busy || !ready}
          >{busy
            ? "กำลังทำรายการ…"
            : payload
              ? "ลองส่งรายการเดิมอีกครั้ง"
              : "ยืนยันรายการทดสอบ"}</button
        >
      </div>
    </form>
  </dialog>
</div>
