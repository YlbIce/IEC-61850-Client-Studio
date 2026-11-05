/** Bounded packet pages independent of DOM, transport and capture-file size. */
export class PacketRepository {
  constructor({ request, pageSize = 200, maxPages = 20 }) {
    this.request = request;
    this.pageSize = pageSize;
    this.maxPages = maxPages;
    this.pages = new Map();
    this.pending = new Map();
    this.total = 0;
    this.filter = "";
    this.generation = 0;
  }
  reset(filter = "") {
    this.filter = filter;
    this.generation++;
    this.pages.clear();
    this.pending.clear();
    this.total = 0;
  }
  get(index) {
    return this.pages.get(Math.floor(index / this.pageSize))?.[
      index % this.pageSize
    ];
  }
  async load(index) {
    const number = Math.max(0, Math.floor(index / this.pageSize));
    if (this.pages.has(number)) {
      const page = this.pages.get(number);
      this.pages.delete(number);
      this.pages.set(number, page);
      return page;
    }
    if (this.pending.has(number)) return this.pending.get(number);
    const generation = this.generation;
    const filter = this.filter;
    const pending = Promise.resolve()
      .then(() =>
        this.request({
          offset: number * this.pageSize,
          limit: this.pageSize,
          filter,
        }),
      )
      .then((result) => {
        if (generation !== this.generation) return [];
        const page = result.packets || [];
        this.total = Number(result.total ?? page.length);
        this.pages.set(number, page);
        while (this.pages.size > this.maxPages)
          this.pages.delete(this.pages.keys().next().value);
        return page;
      })
      .finally(() => {
        if (generation === this.generation) this.pending.delete(number);
      });
    this.pending.set(number, pending);
    return pending;
  }
}

/** Returns a bounded DOM window including overscan, regardless of file size. */
export function visibleRange({
  scrollTop,
  height,
  total,
  rowHeight = 27,
  overscan = 6,
}) {
  const begin = Math.min(
    Math.max(0, total),
    Math.max(0, Math.floor(Math.max(0, scrollTop) / rowHeight) - overscan),
  );
  const end = Math.min(
    total,
    begin + Math.ceil(height / rowHeight) + overscan * 2 + 2,
  );
  return {
    begin,
    end,
    before: begin * rowHeight,
    after: Math.max(0, total - end) * rowHeight,
  };
}
