export type Row = Record<string, any>;
export type Field = {
  key: string;
  label: string;
  type: string;
  required: boolean;
  reference: string;
  options: string[];
};
export type Module = { key: string; label: string; fields: Field[] };
export function openDialog(node: HTMLDialogElement) {
  node.showModal();
  return { destroy: () => node.close() };
}
export const money = (value: number = 0) =>
  new Intl.NumberFormat("th-TH", { style: "currency", currency: "THB" }).format(
    value / 100,
  );
export const apiDefault = () =>
  typeof window === "undefined" ||
  ["localhost", "127.0.0.1"].includes(window.location.hostname)
    ? "http://127.0.0.1:5500"
    : `https://${window.location.hostname.replace("ev.admin.", "ev.admin.api.")}`;
export class Api {
  constructor(
    private endpoint: string,
    private token: string,
  ) {
    const url = new URL(endpoint);
    if (
      !["https:", "http:"].includes(url.protocol) ||
      (url.protocol === "http:" &&
        !["localhost", "127.0.0.1"].includes(url.hostname)) ||
      url.username ||
      url.password ||
      url.pathname !== "/" ||
      url.search ||
      url.hash
    )
      throw new Error(
        "ใช้ HTTPS หรือ HTTP บน localhost เท่านั้น และไม่ใส่ path",
      );
    this.endpoint = url.origin;
  }
  async request(
    path: string,
    method = "GET",
    data?: Row,
    idempotency?: string,
  ): Promise<any> {
    const response = await fetch(`${this.endpoint}/api/v1/management${path}`, {
      method,
      headers: {
        Authorization: `Bearer ${this.token}`,
        ...(data ? { "Content-Type": "application/json" } : {}),
        ...(idempotency ? { "Idempotency-Key": idempotency } : {}),
      },
      body: data ? JSON.stringify(data) : undefined,
      signal: AbortSignal.timeout(15000),
      credentials: "omit",
      redirect: "error",
    });
    const result = await response.json();
    if (!response.ok)
      throw new Error(result.error || `HTTP ${response.status}`);
    return result;
  }
}
// Neutralize spreadsheet formula injection in exported administrator-entered fields.
export function csvDownload(name: string, rows: Row[]) {
  const keys = [...new Set(rows.flatMap((row) => Object.keys(row)))];
  const cell = (value: any) => {
    let text =
      value == null
        ? ""
        : typeof value === "object"
          ? JSON.stringify(value)
          : String(value);
    if (/^[\s]*[=+\-@]/.test(text)) text = `'${text}`;
    return `"${text.replaceAll('"', '""')}"`;
  };
  const contents =
    "\uFEFF" +
    [keys, ...rows.map((row) => keys.map((key) => row[key]))]
      .map((row) => row.map(cell).join(","))
      .join("\r\n");
  const url = URL.createObjectURL(
    new Blob([contents], { type: "text/csv;charset=utf-8" }),
  );
  const link = document.createElement("a");
  link.href = url;
  link.download = `${name}.csv`;
  link.click();
  URL.revokeObjectURL(url);
}
