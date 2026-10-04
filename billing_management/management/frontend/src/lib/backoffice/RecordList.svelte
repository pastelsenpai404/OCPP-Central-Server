<script lang="ts">
  import { csvDownload, money, type Module, type Row } from "./api";
  let {
    spec,
    section,
    rows,
    role,
    canEdit,
    busy,
    total,
    query = $bindable(""),
    offset = $bindable(0),
    reload,
    show,
    financial,
    restore,
    onarchive,
    onedit,
    onsync,
  }: {
    spec: Module | undefined;
    section: string;
    rows: Row[];
    role: string;
    canEdit: boolean;
    busy: boolean;
    total: number;
    query: string;
    offset: number;
    reload: () => Promise<void>;
    show: (row: Row) => Promise<void>;
    financial: (kind: string, row?: Row) => void;
    restore: (row: Row) => Promise<void>;
    onarchive: (row: Row) => void;
    onedit: (row: Row) => void;
    onsync: (row: Row) => Promise<void>;
  } = $props();
  const canWrite = $derived(role !== "reader");
  const columns = $derived(
    spec
      ? [
          "id",
          "code",
          "name",
          ...spec.fields
            .filter((f) => ["select", "reference", "number"].includes(f.type))
            .map((f) => f.key),
          "updated_at",
          ...(section === "chargers" ? ["ocpp_sync_status"] : []),
        ]
      : section === "audit"
        ? ["id", "actor", "action", "kind", "record_id", "created_at"]
        : section === "trash"
          ? ["id", "kind", "deleted_at"]
          : section === "wallet"
            ? [
                "id",
                "type",
                "amount_satang",
                "balance_satang",
                "reference",
                "created_at",
              ]
            : section === "sessions"
              ? [
                  "id",
                  "customer_id",
                  "connector_id",
                  "energy_wh",
                  "subtotal_satang",
                  "status",
                  "created_at",
                ]
              : section === "refunds"
                ? [
                    "id",
                    "session_id",
                    "customer_id",
                    "amount_satang",
                    "reason",
                    "created_at",
                  ]
                : section === "bills"
                  ? [
                      "id",
                      "customer_id",
                      "period_from",
                      "period_to",
                      "total_satang",
                      "status",
                    ]
                  : ["id", "number", "bill_id", "total_satang", "status"],
  );
  function format(key: string, value: any) {
    if (value == null) return "—";
    if (key.endsWith("_satang")) return money(value);
    if (key === "energy_wh") return `${(value / 1000).toFixed(3)} kWh`;
    return String(value);
  }
</script>

<section class="panel table-panel">
  <div class="list-toolbar no-print">
    <form
      onsubmit={(e) => {
        e.preventDefault();
        offset = 0;
        void reload();
      }}
    >
      <input
        bind:value={query}
        placeholder="ค้นหาในรายการ…"
        aria-label="ค้นหา"
      /><button
        class="secondary"
        disabled={busy || ["wallet", "audit", "trash"].includes(section)}
        >ค้นหา</button
      >
    </form>
    <div>
      <span class="muted">{total.toLocaleString()} รายการ</span><button
        class="text-button"
        onclick={() => csvDownload(section, rows)}
        disabled={!rows.length}>Export หน้านี้ ↓</button
      >
    </div>
  </div>
  {#if busy}<div class="loading-state" role="status">กำลังโหลดข้อมูล…</div>{/if}
  {#if rows.length}<div class="table-scroll">
      <table>
        <thead
          ><tr
            >{#each columns as column}<th
                >{spec?.fields.find((f) => f.key === column)?.label ??
                  (column === "ocpp_sync_status" ? "ทะเบียน OCPP" : column)}</th
              >{/each}{#if section !== "audit" && section !== "wallet"}<th
                class="no-print">จัดการ</th
              >{/if}</tr
          ></thead
        ><tbody
          >{#each rows as row (row.id)}<tr
              >{#each columns as column}<td
                  >{#if column === "status"}<span
                      class="state-badge"
                      class:positive={[
                        "active",
                        "available",
                        "resolved",
                        "paid",
                        "completed",
                      ].includes(row[column])}>{row[column]}</span
                    >{:else if column === "ocpp_sync_status"}<span
                      class="state-badge"
                      class:positive={row.ocpp_sync_status === "synced"}
                      title={row.ocpp_last_error || ""}
                      >{row.ocpp_sync_status === "synced"
                        ? row.ocpp_credentials_configured
                          ? "ลงทะเบียนแล้ว"
                          : "ลงทะเบียนแล้ว · รอตั้ง credential"
                        : row.ocpp_sync_status === "retrying"
                          ? "รอลองใหม่"
                          : "กำลังรอ sync"}</span
                    >{:else}{format(column, row[column])}{/if}</td
                >{/each}{#if section !== "audit" && section !== "wallet"}<td
                  class="row-actions no-print"
                  >{#if section === "trash"}{#if role === "admin"}<button
                        class="text-button"
                        onclick={() => restore(row)}
                        disabled={busy}>กู้คืน</button
                      >{/if}{:else}<button
                      class="text-button"
                      onclick={() => show(row)}>รายละเอียด</button
                    >{#if section === "chargers" && canWrite}<button
                        class="text-button"
                        onclick={() => onsync(row)}
                        disabled={busy}>Sync OCPP</button
                      >{/if}{#if spec && canEdit}<button
                        class="text-button"
                        onclick={() => onedit(row)}>แก้ไข</button
                      >{/if}{#if spec && role === "admin"}<button
                        class="text-button danger"
                        onclick={() => onarchive(row)}>เก็บเข้าถังขยะ</button
                      >{/if}{#if section === "sessions" && canWrite}<button
                        class="text-button"
                        onclick={() => financial("refund", row)}>คืนเงิน</button
                      >{/if}{#if section === "bills" && canWrite}{#if row.status === "issued"}<button
                          class="text-button"
                          onclick={() => financial("pay", row)}>ชำระ</button
                        >{:else if row.status === "paid"}<button
                          class="text-button"
                          onclick={() => financial("tax", row)}
                          >ออกใบกำกับ</button
                        >{/if}{/if}{/if}</td
                >{/if}</tr
            >{/each}</tbody
        >
      </table>
    </div>{:else if !busy}<div class="empty-state">
      <span>◇</span>
      <h3>
        {query ? "ไม่พบรายการที่ค้นหา" : "พื้นที่นี้พร้อมสำหรับข้อมูลของคุณ"}
      </h3>
      <p>
        {spec
          ? `เพิ่ม${spec.label}เพื่อเริ่มจัดการข้อมูล`
          : section === "wallet"
            ? "เลือกลูกค้าเพื่อดูยอดเงินและประวัติ"
            : "เมื่อทำรายการแล้ว ข้อมูลจะแสดงที่นี่"}
      </p>
    </div>{/if}
  {#if section !== "wallet"}<div class="pagination no-print">
      <span
        >{total ? offset + 1 : 0}–{Math.min(offset + 100, total)} จาก {total}</span
      >
      <div>
        <button
          class="secondary"
          disabled={offset === 0 || busy}
          onclick={() => {
            offset -= 100;
            void reload();
          }}>← ก่อนหน้า</button
        ><button
          class="secondary"
          disabled={offset + 100 >= total || busy}
          onclick={() => {
            offset += 100;
            void reload();
          }}>ถัดไป →</button
        >
      </div>
    </div>{/if}
</section>
