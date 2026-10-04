<script lang="ts">
  import { onMount } from "svelte";
  import {
    Api,
    apiDefault,
    money,
    openDialog,
    type Module,
    type Row,
  } from "../lib/backoffice/api";
  import Dashboard from "../lib/backoffice/Dashboard.svelte";
  import RecordList from "../lib/backoffice/RecordList.svelte";
  import EntityEditor from "../lib/backoffice/EntityEditor.svelte";
  import FinancialAction from "../lib/backoffice/FinancialAction.svelte";
  import RecordDetail from "../lib/backoffice/RecordDetail.svelte";
  import QuoteCalculator from "@billing/ui/components/QuoteCalculator.svelte";
  import { loadQuoteEngine } from "../lib/wasm/quote";
  let endpoint = $state("http://127.0.0.1:5500"),
    token = $state(""),
    api = $state<Api | null>(null),
    role = $state("");
  let specs: Module[] = $state([]),
    section = $state("overview"),
    rows: Row[] = $state([]),
    overview: Row = $state({}),
    wallet: Row | null = $state(null);
  let busy = $state(false),
    error = $state(""),
    notice = $state(""),
    query = $state(""),
    offset = $state(0),
    total = $state(0);
  let editor = $state(false),
    editing: Row | undefined = $state(undefined),
    detail: Row | null = $state(null),
    action = $state(""),
    target: Row | undefined = $state(undefined),
    archive: Row | null = $state(null);
  let customers: Row[] = $state([]),
    selectedCustomer = $state("");
  let generation = 0;
  const labels: Record<string, string> = {
    overview: "ภาพรวม",
    sessions: "ประวัติการชาร์จ",
    wallet: "กระเป๋าเงิน",
    refunds: "รายการคืนเงิน",
    bills: "บิลและการชำระ",
    "tax-invoices": "ใบกำกับภาษี",
    audit: "Audit trail",
    trash: "ถังขยะ",
    calculator: "เครื่องคำนวณ WASM",
  };
  const groups = [
    {
      label: "WORKSPACE",
      items: [
        "overview",
        "companies",
        "projects",
        "stations",
        "chargers",
        "connectors",
        "maintenance",
      ],
    },
    {
      label: "BILLING",
      items: [
        "customers",
        "tariffs",
        "sessions",
        "wallet",
        "refunds",
        "bills",
        "tax-invoices",
      ],
    },
    {
      label: "ADMINISTRATION",
      items: ["admins", "settings", "audit", "trash", "calculator"],
    },
  ];
  const title = $derived(
    labels[section] ?? specs.find((s) => s.key === section)?.label ?? section,
  );
  const spec = $derived(specs.find((s) => s.key === section));
  const canWrite = $derived(role !== "reader");
  const canEdit = $derived(
    canWrite && (role === "admin" || !["admins", "settings"].includes(section)),
  );
  onMount(() => {
    endpoint = apiDefault();
  });
  async function connect(event: SubmitEvent) {
    event.preventDefault();
    busy = true;
    error = "";
    try {
      const client = new Api(endpoint, token.trim());
      const me = await client.request("/me");
      specs = await client.request("/modules");
      role = me.role;
      api = client;
      token = "";
      await reload();
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
      api = null;
    } finally {
      busy = false;
    }
  }
  function disconnect() {
    generation++;
    api = null;
    token = "";
    rows = [];
    wallet = null;
    overview = {};
    role = "";
    detail = null;
    editor = false;
    action = "";
    archive = null;
    section = "overview";
  }
  async function reload() {
    if (!api) return;
    const current = ++generation;
    busy = true;
    error = "";
    try {
      if (section === "overview") {
        const value = await api.request("/overview");
        if (current === generation) overview = value;
      } else if (section === "wallet") {
        if (!customers.length)
          customers = (await api.request("/customers")).items;
        if (selectedCustomer) {
          const value = await api.request(`/wallet/${selectedCustomer}`);
          if (current === generation) {
            wallet = value;
            rows = value.items.map((r: Row) => ({
              ...JSON.parse(r.data),
              id: r.id,
              created_at: r.created_at,
            }));
            total = rows.length;
          }
        } else {
          rows = [];
          wallet = null;
          total = 0;
        }
      } else if (section !== "calculator") {
        const result = await api.request(
          `/${section}?offset=${offset}&q=${encodeURIComponent(query)}`,
        );
        if (current === generation) {
          rows = result.items;
          total = result.total;
        }
      }
    } catch (e) {
      if (current === generation)
        error = e instanceof Error ? e.message : String(e);
    } finally {
      if (current === generation) busy = false;
    }
  }
  async function navigate(key: string) {
    section = key;
    query = "";
    offset = 0;
    rows = [];
    notice = "";
    detail = null;
    wallet = null;
    await reload();
  }
  function financial(kind: string, row?: Row) {
    action = kind;
    target = row;
  }
  async function completed() {
    notice = "บันทึกสำเร็จ";
    customers = [];
    await reload();
  }
  async function restore(row: Row) {
    if (!api) return;
    busy = true;
    try {
      await api.request(`/${row.kind}/${row.id}/restore`, "POST", {
        version: row.version,
      });
      await completed();
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      busy = false;
    }
  }
  async function confirmArchive() {
    if (!api || !archive) return;
    busy = true;
    try {
      await api.request(`/${section}/${archive.id}`, "DELETE", {
        version: archive.version,
      });
      archive = null;
      await completed();
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      busy = false;
    }
  }
  async function show(row: Row) {
    if (!api) return;
    try {
      detail = await api.request(`/${section}/${row.id}`);
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }
  async function syncCharger(row: Row) {
    if (!api) return;
    busy = true;
    error = "";
    try {
      await api.request(`/chargers/${row.id}/sync`, "POST", {});
      notice = "ส่งคำขอ sync OCPP แล้ว";
      await reload();
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    } finally {
      busy = false;
    }
  }
</script>

<svelte:head
  ><title>EV Billing · Management</title><meta
    name="description"
    content="พื้นที่จัดการสถานี ลูกค้า กระเป๋าเงิน และบิลทดสอบ"
  /></svelte:head
>
{#if !api}
  <main class="access-screen">
    <section class="access-card">
      <div class="brand-mark">ev<span>·</span></div>
      <p class="eyebrow">BILLING MANAGEMENT</p>
      <h1>Everything.<br />In one workspace.</h1>
      <p>ดูแลเครือข่ายชาร์จและงาน billing<br />จากพื้นที่ทำงานเดียว</p>
      <form onsubmit={connect}>
        <label
          >API endpoint<input
            type="url"
            bind:value={endpoint}
            required
          /></label
        ><label
          >Access token<input
            type="password"
            bind:value={token}
            autocomplete="off"
            required
            placeholder="reader / operator / admin token"
          /></label
        >{#if error}<p class="notice error" role="alert">{error}</p>{/if}<button
          disabled={busy}
          >{busy ? "กำลังเชื่อมต่อ…" : "เข้าสู่ workspace →"}</button
        >
      </form>
      <p class="access-note">
        Token เก็บในหน่วยความจำของหน้านี้ · ใช้ BILLING_API_TOKEN จากไฟล์
        environment ของ management backend
      </p>
      <span class="sandbox-pill">Sandbox · ไม่มีการเรียกเก็บเงินจริง</span>
    </section>
  </main>
{:else}
  <div class="workspace">
    <aside class="sidebar no-print">
      <div class="brand">
        <div class="brand-mark small">ev<span>·</span></div>
        <div><strong>Billing</strong><small>Management workspace</small></div>
      </div>
      <div class="workspace-tag">
        <span class="status-dot"></span> Sandbox workspace
      </div>
      <nav>
        {#each groups as group}<p class="nav-group">{group.label}</p>
          {#each group.items as item}<button
              class:active={section === item}
              onclick={() => navigate(item)}
              ><span
                >{labels[item] ??
                  specs.find((s) => s.key === item)?.label ??
                  item}</span
              >{#if overview.counts?.[item]}<small
                  >{overview.counts[item]}</small
                >{/if}</button
            >{/each}{/each}
      </nav>
      <div class="sidebar-bottom">
        <span class="role-label">{role.toUpperCase()} ACCESS</span><button
          class="secondary"
          onclick={disconnect}>ออกจากระบบ</button
        >
      </div>
    </aside>
    <main class="workspace-main">
      <header class="topbar no-print">
        <span>Workspace <span class="crumb">/</span> {title}</span>
        <div>
          <span class="sandbox-pill">Sandbox</span><button
            class="text-button"
            onclick={reload}
            disabled={busy}>↻ รีเฟรช</button
          ><button class="text-button" onclick={disconnect}>ออก</button>
        </div>
      </header>
      <div class="page-content">
        <div class="page-heading no-print">
          <div>
            <p class="eyebrow">EV OPERATIONS</p>
            <h1>{title}</h1>
            <p>
              {section === "overview"
                ? "มองเห็นทุกส่วนของธุรกิจในภาพเดียว"
                : "ข้อมูลจากฐานข้อมูลทดสอบของ management backend"}
            </p>
          </div>
          <div class="toolbar-actions">
            {#if spec && canEdit}<button
                onclick={() => {
                  editing = undefined;
                  editor = true;
                }}>＋ เพิ่ม{spec.label}</button
              >{/if}{#if section === "sessions" && canWrite}<button
                onclick={() => financial("session")}>＋ บันทึกการชาร์จ</button
              >{/if}{#if section === "bills" && canWrite}<button
                onclick={() => financial("bill")}>＋ สร้างบิล</button
              >{/if}
          </div>
        </div>
        {#if error}<div class="notice error" role="alert">
            {error}
          </div>{/if}{#if notice}<div class="notice success" role="status">
            {notice}
          </div>{/if}
        {#if section === "overview"}
          <Dashboard {overview} {navigate} />
        {:else if section === "calculator"}<section class="calculator-wrap">
            <QuoteCalculator loadEngine={loadQuoteEngine} />
          </section>
        {:else}
          {#if section === "wallet"}<section class="panel wallet-panel">
              <label
                >เลือกลูกค้า<select
                  aria-label="Customer wallet"
                  bind:value={selectedCustomer}
                  onchange={() => reload()}
                  ><option value="">เลือกลูกค้า</option
                  >{#each customers as customer}<option value={customer.id}
                      >{customer.name} · {customer.code}</option
                    >{/each}</select
                ></label
              >
              <div>
                <span class="muted">ยอดคงเหลือ</span><strong
                  class="wallet-balance">{money(wallet?.balance_satang)}</strong
                >
              </div>
              {#if canWrite}<div class="toolbar-actions">
                  <button
                    onclick={() =>
                      financial(
                        "top-up",
                        selectedCustomer
                          ? { customer_id: Number(selectedCustomer) }
                          : undefined,
                      )}>เติมเงิน</button
                  ><button
                    class="secondary"
                    onclick={() =>
                      financial(
                        "withdraw",
                        selectedCustomer
                          ? { customer_id: Number(selectedCustomer) }
                          : undefined,
                      )}>ถอนเงิน</button
                  >
                </div>{/if}
            </section>{/if}
          {#if section === "tariffs"}<p class="notice">
              ราคาใช้สตางค์ / kWh และ VAT % · การแก้ราคาไม่เปลี่ยน snapshot
              ของประวัติชาร์จเดิม
            </p>{/if}
          {#if section === "chargers"}<p class="notice">
              รหัสเครื่องคือ ChargeBoxId เดียวกับ OCPP เมื่อสร้างเครื่อง
              ระบบจะลงทะเบียนใน CSMS อัตโนมัติ เครื่องจะออนไลน์ได้หลังตั้ง
              credential และเชื่อมต่อจริง
            </p>{/if}
          {#if section === "admins"}<p class="notice">
              ทะเบียนทีมแยกจากการยืนยันตัวตน API · สิทธิ์จริงขึ้นกับ role token
              ที่ใช้เข้าระบบ
            </p>{/if}
          <RecordList
            {spec}
            {section}
            {rows}
            {role}
            {canEdit}
            {busy}
            {total}
            bind:query
            bind:offset
            {reload}
            {show}
            {financial}
            {restore}
            onsync={syncCharger}
            onarchive={(row) => (archive = row)}
            onedit={(row) => {
              editing = row;
              editor = true;
            }}
          />
        {/if}
        <footer class="workspace-footer no-print">
          EV Billing Management · Sandbox workspace
        </footer>
      </div>
    </main>
  </div>
  {#if editor && spec}<EntityEditor
      {api}
      {spec}
      record={editing}
      done={completed}
      close={() => (editor = false)}
    />{/if}
  {#if action}<FinancialAction
      {api}
      {action}
      record={target}
      done={completed}
      close={() => (action = "")}
    />{/if}
  {#if detail}<RecordDetail
      record={detail}
      kind={section}
      close={() => (detail = null)}
    />{/if}
  {#if archive}<div class="modal-backdrop">
      <dialog
        class="sheet small-sheet"
        use:openDialog
        oncancel={(e) => {
          e.preventDefault();
          if (!busy) archive = null;
        }}
        aria-label="ยืนยันเก็บเข้าถังขยะ"
      >
        <h2>เก็บ {archive.name} เข้าถังขยะ?</h2>
        <p>
          ข้อมูลจะกู้คืนได้
          รายการที่มีข้อมูลอ้างอิงหรือประวัติการเงินจะถูกปฏิเสธ
        </p>
        <div class="actions">
          <button
            class="secondary"
            disabled={busy}
            onclick={() => (archive = null)}>ยกเลิก</button
          ><button disabled={busy} onclick={confirmArchive}>ยืนยัน</button>
        </div>
      </dialog>
    </div>{/if}
{/if}
