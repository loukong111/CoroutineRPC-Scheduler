#!/usr/bin/env python3
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "assets" / "images"
OUT.mkdir(parents=True, exist_ok=True)

W, H = 1360, 860


def font(size, bold=False):
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    ]
    for path in candidates:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


F_TITLE = font(28, True)
F_SUB = font(16)
F_BODY = font(15)
F_BODY_BOLD = font(15, True)
F_SMALL = font(12)
F_MONO = font(14)


def rounded(draw, xy, fill, outline=None, radius=12, width=1):
    draw.rounded_rectangle(xy, radius=radius, fill=fill, outline=outline, width=width)


def text(draw, xy, value, fill="#1f2937", fnt=F_BODY):
    draw.text(xy, value, fill=fill, font=fnt)


def base(title, subtitle):
    img = Image.new("RGB", (W, H), "#f6f8fb")
    d = ImageDraw.Draw(img)
    rounded(d, (28, 28, 250, H - 28), "#111827", radius=18)
    text(d, (58, 58), "CorpCron", "#ffffff", F_TITLE)
    text(d, (58, 96), "RPC Scheduler Console", "#9ca3af", F_SMALL)
    nav = ["Overview", "RPC Ops", "Tasks", "History", "Services", "Metrics", "Demo", "Logs"]
    y = 150
    for item in nav:
        fill = "#2563eb" if item.lower() in title.lower() else "#111827"
        rounded(d, (48, y, 230, y + 42), fill, radius=8)
        text(d, (66, y + 12), item, "#ffffff" if fill == "#2563eb" else "#cbd5e1", F_BODY)
        y += 52
    text(d, (286, 46), title, "#0f172a", F_TITLE)
    text(d, (286, 84), subtitle, "#64748b", F_SUB)
    rounded(d, (286, 118, W - 34, H - 32), "#ffffff", outline="#d9e1ec", radius=16)
    return img, d


def stat_card(d, x, y, w, h, title, value, hint):
    rounded(d, (x, y, x + w, y + h), "#ffffff", outline="#d9e1ec", radius=12)
    text(d, (x + 22, y + 18), title, "#64748b", F_SMALL)
    text(d, (x + 22, y + 46), value, "#0f172a", font(32, True))
    text(d, (x + 22, y + h - 28), hint, "#94a3b8", F_SMALL)


def table(d, x, y, cols, rows, widths, row_h=42):
    header_h = 42
    rounded(d, (x, y, x + sum(widths), y + header_h + row_h * len(rows)), "#ffffff", outline="#d9e1ec", radius=10)
    d.rectangle((x, y, x + sum(widths), y + header_h), fill="#f1f5f9")
    cx = x
    for col, w in zip(cols, widths):
        text(d, (cx + 12, y + 13), col, "#475569", F_BODY_BOLD)
        cx += w
    for r, row in enumerate(rows):
        ry = y + header_h + r * row_h
        if r % 2:
            d.rectangle((x, ry, x + sum(widths), ry + row_h), fill="#f8fafc")
        cx = x
        for val, w in zip(row, widths):
            text(d, (cx + 12, ry + 12), str(val), "#1f2937", F_BODY)
            cx += w


def overview():
    img, d = base("Overview", "System health and live runtime metrics")
    cards = [
        ("Connection", "Online", "127.0.0.1:8081"),
        ("Tasks", "12", "current list"),
        ("History", "50", "latest query"),
        ("Services", "2", "Redis discovery"),
        ("RPC Requests", "8,432", "total"),
        ("RPC Errors", "3", "total"),
        ("Active Conn", "4", "current"),
        ("Task Success", "126", "total"),
    ]
    x0, y0 = 314, 150
    for i, card in enumerate(cards):
        x = x0 + (i % 4) * 250
        y = y0 + (i // 4) * 150
        stat_card(d, x, y, 220, 120, *card)
    table(d, 314, 480, ["Recent Event", "Status", "Time"], [
        ("SubmitTask", "success", "20:31:12"),
        ("ExecuteTask", "success", "20:31:20"),
        ("GetMetrics", "success", "20:31:24"),
        ("Lock renew", "success", "20:31:26"),
    ], [360, 180, 180])
    img.save(OUT / "qt-overview.png")


def tasks():
    img, d = base("Tasks", "Task management, editing and manual execution")
    table(d, 314, 154, ["ID", "Handler", "Status", "Cron", "Next Run", "Retry"], [
        ("task-a7f1", "Echo", "enabled", "* * * * * ?", "2026-07-08 20:35:00", "0/3"),
        ("task-b4c2", "Echo", "enabled", "*/5 * * * * ?", "2026-07-08 20:40:00", "1/3"),
        ("task-c91e", "Echo", "disabled", "0 0 * * * ?", "2026-07-09 00:00:00", "3/3"),
    ], [170, 130, 120, 190, 260, 90])
    rounded(d, (314, 380, 1050, 690), "#ffffff", outline="#d9e1ec", radius=12)
    text(d, (340, 408), "Task Detail / Actions", "#0f172a", F_BODY_BOLD)
    labels = [("ID", "task-a7f1"), ("Cron", "* * * * * ?"), ("Handler", "Echo"), ("Params", "demo payload"), ("Max Retries", "3")]
    y = 452
    for k, v in labels:
        text(d, (340, y), k, "#64748b", F_SMALL)
        rounded(d, (450, y - 8, 840, y + 28), "#f8fafc", outline="#cbd5e1", radius=6)
        text(d, (464, y), v, "#1f2937", F_BODY)
        y += 46
    for i, name in enumerate(["Save", "Enable", "Disable", "Run Now", "Delete"]):
        x = 340 + i * 120
        fill = "#dc2626" if name == "Delete" else "#2563eb" if name in ("Save", "Run Now") else "#eef2f7"
        color = "#ffffff" if fill in ("#dc2626", "#2563eb") else "#1f2937"
        rounded(d, (x, 640, x + 100, 678), fill, outline="#cbd5e1", radius=7)
        text(d, (x + 20, 651), name, color, F_SMALL)
    img.save(OUT / "qt-task-management.png")


def metrics():
    img, d = base("Metrics", "RPC, scheduler, traffic and lock observability")
    groups = [
        ("RPC", [("requests", "8432"), ("success", "8429"), ("errors", "3")]),
        ("Connection", [("active", "4"), ("rejected", "0"), ("malformed", "1")]),
        ("Traffic", [("bytes in", "1.7 MB"), ("bytes out", "2.1 MB")]),
        ("Scheduler", [("task success", "126"), ("task failure", "2"), ("max duration", "37 ms")]),
        ("Redis Lock", [("acquire ok", "128"), ("acquire fail", "6")]),
    ]
    x, y = 314, 154
    for idx, (name, items) in enumerate(groups):
        gx = x + (idx % 2) * 380
        gy = y + (idx // 2) * 185
        rounded(d, (gx, gy, gx + 340, gy + 150), "#ffffff", outline="#d9e1ec", radius=12)
        text(d, (gx + 20, gy + 18), name, "#0f172a", F_BODY_BOLD)
        iy = gy + 52
        for key, val in items:
            text(d, (gx + 22, iy), key, "#64748b", F_SMALL)
            text(d, (gx + 190, iy - 4), val, "#0f172a", F_BODY_BOLD)
            iy += 30
    img.save(OUT / "qt-metrics.png")


def demo_console():
    img, d = base("Demo", "One-click environment, tests, benchmark and failure demos")
    sections = [
        ("Environment", ["Start deps", "Stop deps", "Reset deps", "Demo check"]),
        ("Server", ["Start server", "Stop server", "Start node-2", "Stop node-2"]),
        ("Quality", ["Build test", "Integration/E2E", "Docker build", "Deploy doc"]),
        ("Benchmark", ["Short benchmark", "Reuse benchmark", "Latest result"]),
        ("Protocol errors", ["Auth failure", "Unknown RPC", "Bad frame"]),
    ]
    x, y = 314, 150
    for si, (title, buttons) in enumerate(sections):
        sx = x + (si % 2) * 430
        sy = y + (si // 2) * 180
        rounded(d, (sx, sy, sx + 390, sy + 145), "#ffffff", outline="#d9e1ec", radius=12)
        text(d, (sx + 20, sy + 18), title, "#0f172a", F_BODY_BOLD)
        bx, by = sx + 20, sy + 55
        for i, b in enumerate(buttons):
            px = bx + (i % 2) * 175
            py = by + (i // 2) * 44
            color = "#dc2626" if "Stop" in b or "Reset" in b or "Bad" in b else "#2563eb" if "Start" in b or "benchmark" in b.lower() else "#eef2f7"
            txt = "#ffffff" if color in ("#dc2626", "#2563eb") else "#1f2937"
            rounded(d, (px, py, px + 155, py + 32), color, outline="#cbd5e1", radius=7)
            text(d, (px + 14, py + 9), b, txt, F_SMALL)
    rounded(d, (314, 685, 1130, 810), "#0f172a", radius=10)
    text(d, (336, 708), "$ ./scripts/benchmark.sh 127.0.0.1 8081 16 1000 reuse", "#e5e7eb", F_MONO)
    text(d, (336, 738), "requests=1000 concurrency=16 mode=reuse success=1000 failure=0 qps=12480 p95_ms=2", "#86efac", F_MONO)
    img.save(OUT / "qt-demo-console.png")


def redis_discovery():
    img = Image.new("RGB", (W, 520), "#0f172a")
    d = ImageDraw.Draw(img)
    text(d, (38, 32), "Redis service discovery", "#e5e7eb", F_TITLE)
    lines = [
        "$ docker exec -it corpcron-redis redis-cli",
        "127.0.0.1:6379> SMEMBERS services:rpc",
        "1) \"127.0.0.1:8081\"",
        "2) \"127.0.0.1:8082\"",
        "127.0.0.1:6379> KEYS services:rpc:*",
        "1) \"services:rpc:127.0.0.1:8081\"",
        "2) \"services:rpc:127.0.0.1:8082\"",
        "127.0.0.1:6379> TTL services:rpc:127.0.0.1:8081",
        "(integer) 27",
    ]
    y = 96
    for line in lines:
        fill = "#86efac" if line.startswith("127.") else "#cbd5e1"
        text(d, (44, y), line, fill, F_MONO)
        y += 42
    img.save(OUT / "redis-service-discovery.png")


def benchmark_result():
    img = Image.new("RGB", (W, 620), "#f6f8fb")
    d = ImageDraw.Draw(img)
    text(d, (44, 36), "CorpCron Benchmark Result", "#0f172a", F_TITLE)
    stat_card(d, 44, 96, 220, 120, "Mode", "reuse", "persistent RPC")
    stat_card(d, 286, 96, 220, 120, "QPS", "12480", "success only")
    stat_card(d, 528, 96, 220, 120, "p95", "2 ms", "latency")
    stat_card(d, 770, 96, 220, 120, "p99", "4 ms", "latency")
    stat_card(d, 1012, 96, 220, 120, "Failure", "0", "requests")
    rounded(d, (44, 260, 1260, 560), "#0f172a", radius=12)
    lines = [
        "# CorpCron Benchmark Result",
        "host=127.0.0.1 port=8081 concurrency=16 requests=1000 mode=reuse",
        "## server_before",
        "PID %CPU %MEM   RSS    VSZ ELAPSED CMD",
        "8421  0.3  0.1 18432 102400 00:01:12 build/corpcron_server --config config/server.conf",
        "## client_result",
        "requests=1000 concurrency=16 mode=reuse success=1000 failure=0 elapsed_sec=0.0801 qps=12480 p50_ms=1 p95_ms=2 p99_ms=4",
        "## server_after",
        "PID %CPU %MEM   RSS    VSZ ELAPSED CMD",
        "8421  3.8  0.1 19020 102400 00:01:13 build/corpcron_server --config config/server.conf",
    ]
    y = 286
    for line in lines:
        fill = "#93c5fd" if line.startswith("#") or line.startswith("##") else "#e5e7eb"
        text(d, (68, y), line, fill, F_MONO)
        y += 26
    img.save(OUT / "benchmark-result.png")


def main():
    overview()
    tasks()
    metrics()
    demo_console()
    redis_discovery()
    benchmark_result()
    print(f"Generated README images in {OUT}")


if __name__ == "__main__":
    main()
