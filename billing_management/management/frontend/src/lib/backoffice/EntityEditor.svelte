<script lang="ts">
  import { openDialog, type Api, type Module, type Row } from "./api";
  import { onMount } from "svelte";
  let {
    api,
    spec,
    record,
    done,
    close,
  }: {
    api: Api;
    spec: Module;
    record?: Row;
    done: () => Promise<void>;
    close: () => void;
  } = $props();
  function initialValues() {
    return Object.fromEntries(
      spec.fields.map((f) => [
        f.key,
        record?.[f.key] ?? (f.type === "select" ? f.options[0] : ""),
      ]),
    );
  }
  let values: Row = $state(initialValues());
  let options: Record<string, Row[]> = $state({});
  let error = $state("");
  let busy = $state(false);
  let ready = $state(false);
  onMount(() => {
    void (async () => {
      try {
        for (const field of spec.fields.filter((f) => f.reference)) {
          const rows: Row[] = [];
          for (let offset = 0; offset < 1000; offset += 100) {
            const result = await api.request(
              `/${field.reference}?offset=${offset}`,
            );
            rows.push(...result.items);
            if (rows.length >= result.total) break;
          }
          options[field.key] = rows;
        }
        ready = true;
      } catch (e) {
        error = String(e instanceof Error ? e.message : e);
      }
    })();
  });
  async function submit(event: SubmitEvent) {
    event.preventDefault();
    busy = true;
    error = "";
    try {
      const data = Object.fromEntries(
        spec.fields.map((f) => [
          f.key,
          ["number", "reference"].includes(f.type)
            ? Number(values[f.key])
            : values[f.key],
        ]),
      );
      if (record) data.version = record.version;
      await api.request(
        `/${spec.key}${record ? `/${record.id}` : ""}`,
        record ? "PATCH" : "POST",
        data,
      );
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
    aria-label={`${record ? "แก้ไข" : "เพิ่ม"} ${spec.label}`}
  >
    <div class="section-heading">
      <div>
        <p class="eyebrow">WORKSPACE RECORD</p>
        <h2>{record ? "แก้ไข" : "เพิ่ม"} {spec.label}</h2>
      </div>
      <button
        class="icon-button"
        onclick={close}
        disabled={busy}
        aria-label="ปิด">×</button
      >
    </div>
    {#if error}<p class="notice error" role="alert">{error}</p>{/if}
    <form onsubmit={submit}>
      <div class="form-grid">
        {#each spec.fields as field (field.key)}
          <label
            >{field.label}{field.required ? " *" : ""}
            {#if field.type === "reference"}
              <select bind:value={values[field.key]} required={field.required}
                ><option value="">เลือก{field.label}</option
                >{#each options[field.key] ?? [] as row (row.id)}<option
                    value={row.id}>{row.name} · {row.code}</option
                  >{/each}</select
              >
            {:else if field.type === "select"}
              <select bind:value={values[field.key]}
                >{#each field.options as value}<option {value}>{value}</option
                  >{/each}</select
              >
            {:else if field.type === "number"}
              <input
                type="number"
                min="0"
                max={field.key === "vat_percent" ? 25 : 1000000}
                step="1"
                bind:value={values[field.key]}
                required={field.required}
              />
            {:else}<input
                bind:value={values[field.key]}
                readonly={spec.key === "chargers" &&
                  field.key === "code" &&
                  !!record}
                required={field.required}
                maxlength={field.key === "notes" || field.key === "address"
                  ? 1000
                  : 200}
              />{/if}
          </label>
        {/each}
      </div>
      {#if spec.key === "admins"}<p class="muted">
          ทะเบียนทีมใช้เก็บข้อมูลผู้ดูแล สิทธิ์เข้า API กำหนดด้วย token reader /
          operator / admin แยกต่างหาก
        </p>{/if}
      {#if spec.key === "settings"}<p class="muted">
          ข้อมูลการตั้งค่าธุรกิจ ไม่ใช้เก็บรหัสผ่านหรือ API secret
        </p>{/if}
      <div class="actions">
        <button type="button" class="secondary" onclick={close} disabled={busy}
          >ยกเลิก</button
        ><button disabled={busy || !ready}
          >{busy ? "กำลังบันทึก…" : "บันทึกข้อมูล"}</button
        >
      </div>
    </form>
  </dialog>
</div>
