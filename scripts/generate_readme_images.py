#!/usr/bin/env python3
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "assets" / "images"
OUT.mkdir(parents=True, exist_ok=True)

W, H = 1440, 900


def font(size, bold=False, mono=False):
    if mono:
        candidates = [
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
        ]
    elif bold:
        candidates = [
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        ]
    else:
        candidates = [
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        ]
    for path in candidates:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


F_TITLE = font(24, True)
F_SUB = font(15)
F_BODY = font(14)
F_BODY_BOLD = font(14, True)
F_SMALL = font(12)
F_MONO = font(13, mono=True)
F_MONO_BIG = font(15, mono=True)

BG = "#1f2329"
TOP = "#2b3037"
PANEL = "#252a31"
PANEL_ALT = "#20242b"
BORDER = "#3b424d"
TEXT = "#d8dee9"
TEXT_MUTED = "#9ca3af"
TEXT_DIM = "#7b8492"
BLUE = "#2563eb"
RED = "#b91c1c"
GREEN = "#22c55e"
AMBER = "#f59e0b"


def rounded(draw, xy, fill, outline=None, radius=8, width=1):
    draw.rounded_rectangle(xy, radius=radius, fill=fill, outline=outline, width=width)


def text(draw, xy, value, fill=TEXT, fnt=F_BODY):
    draw.text(xy, value, fill=fill, font=fnt)


def button(draw, x, y, label, kind="normal", w=118):
    fill = {"primary": BLUE, "danger": RED, "normal": "#343b45"}.get(kind, "#343b45")
    outline = {"primary": "#1d4ed8", "danger": "#991b1b", "normal": "#4b5563"}.get(kind, "#4b5563")
    rounded(draw, (x, y, x + w, y + 32), fill, outline=outline, radius=4)
    text(draw, (x + 14, y + 7), label, "#ffffff" if kind in ("primary", "danger") else "#e5e7eb", F_SMALL)


def input_box(draw, x, y, label, value, w=170):
    text(draw, (x, y + 8), label, "#cbd5e1", F_SMALL)
    rounded(draw, (x + 48, y, x + 48 + w, y + 32), "#1c2026", outline="#444c58", radius=4)
    text(draw, (x + 60, y + 7), value, "#d8dee9", F_SMALL)


def chrome(active):
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W, 34), fill=TOP)
    for i, item in enumerate(["文件", "运行", "可观测性", "工具"]):
        text(d, (18 + i * 74, 9), item, TEXT, F_SMALL)

    d.rectangle((0, 34, W, 82), fill=TOP)
    text(d, (20, 49), "CorpCron 管理控制台", "#f3f4f6", font(22, True))
    input_box(d, 330, 43, "Host", "127.0.0.1", 150)
    input_box(d, 560, 43, "Port", "8081", 76)
    input_box(d, 710, 43, "Token", "", 160)
    button(d, 940, 43, "连接", "primary", 78)
    button(d, 1028, 43, "断开", "normal", 78)
    rounded(d, (1328, 47, 1410, 72), "#11324d", radius=12)
    text(d, (1346, 52), "已连接", "#7dd3fc", F_SMALL)

    nav_x, nav_y, nav_w = 0, 82, 188
    d.rectangle((nav_x, nav_y, nav_x + nav_w, H - 190), fill="#1c2026")
    items = ["概览", "RPC", "任务", "执行记录", "服务发现", "Metrics", "运维"]
    y = 104
    for item in items:
        selected = item == active
        rounded(d, (14, y, 174, y + 38), "#334155" if selected else "#1c2026", radius=4)
        text(d, (32, y + 10), item, "#ffffff" if selected else "#aeb7c2", F_BODY_BOLD if selected else F_BODY)
        y += 44

    d.rectangle((0, H - 190, W, H), fill="#1b1f25")
    d.line((0, H - 190, W, H - 190), fill=BORDER)
    rounded(d, (14, H - 174, 120, H - 140), "#334155", radius=4)
    text(d, (38, H - 164), "控制台", "#ffffff", F_SMALL)
    rounded(d, (126, H - 174, 220, H - 140), PANEL, radius=4)
    text(d, (158, H - 164), "事件", TEXT_MUTED, F_SMALL)
    return img, d


def content_area(draw, title, subtitle):
    x, y, w, h = 210, 132, W - 232, H - 346
    text(draw, (x, y - 44), title, "#f9fafb", F_TITLE)
    text(draw, (x, y - 16), subtitle, TEXT_MUTED, F_SUB)
    rounded(draw, (x, y, x + w, y + h), PANEL, outline=BORDER, radius=6)
    return x, y, w, h


def console(draw, lines):
    x, y = 24, H - 128
    for line, color in lines:
        text(draw, (x, y), line, color, F_MONO)
        y += 24


def stat_card(draw, x, y, w, h, title, value, hint):
    rounded(draw, (x, y, x + w, y + h), PANEL, outline=BORDER, radius=6)
    text(draw, (x + 18, y + 16), title, TEXT_MUTED, F_SMALL)
    text(draw, (x + 18, y + 43), value, "#f9fafb", font(28, True))
    text(draw, (x + 18, y + h - 28), hint, TEXT_DIM, F_SMALL)


def table(draw, x, y, cols, rows, widths, row_h=40):
    total_w = sum(widths)
    header_h = 40
    rounded(draw, (x, y, x + total_w, y + header_h + row_h * len(rows)), PANEL_ALT, outline=BORDER, radius=5)
    d = draw
    d.rectangle((x, y, x + total_w, y + header_h), fill=TOP)
    cx = x
    for col, w in zip(cols, widths):
        text(d, (cx + 10, y + 11), col, "#cbd5e1", F_BODY_BOLD)
        cx += w
    for r, row in enumerate(rows):
        ry = y + header_h + r * row_h
        if r % 2:
            d.rectangle((x, ry, x + total_w, ry + row_h), fill=PANEL)
        cx = x
        for val, w in zip(row, widths):
            text(d, (cx + 10, ry + 11), str(val), TEXT, F_BODY)
            cx += w


def overview():
    img, d = chrome("概览")
    x, y, _, _ = content_area(d, "概览", "连接状态、任务数量、服务发现和运行指标总览")
    cards = [
        ("连接状态", "已连接", "RPC 通道"),
        ("任务数量", "12", "当前列表"),
        ("历史记录", "50", "最近查询"),
        ("服务节点", "4", "2 控制 / 2 Worker"),
        ("RPC 请求", "8,432", "累计"),
        ("RPC 错误", "3", "累计"),
        ("活跃连接", "4", "当前"),
        ("任务成功", "126", "累计"),
    ]
    for i, card in enumerate(cards):
        stat_card(d, x + 24 + (i % 4) * 285, y + 28 + (i // 4) * 132, 250, 104, *card)
    table(d, x + 24, y + 326, ["事件", "状态", "时间"], [
        ("SubmitTask", "成功", "20:31:12"),
        ("ExecuteTask", "成功", "20:31:20"),
        ("GetMetrics", "成功", "20:31:24"),
        ("Lock renew", "成功", "20:31:26"),
    ], [420, 180, 180])
    console(d, [
        ("[20:31:24] 运行指标已刷新", GREEN),
        ("[20:31:26] worker:Echo 已发现 2 个执行节点", "#93c5fd"),
    ])
    img.save(OUT / "qt-overview.png")


def tasks():
    img, d = chrome("任务")
    x, y, _, _ = content_area(d, "任务", "任务分页筛选、编辑、启停、删除和立即执行")
    button(d, x + 24, y + 22, "刷新任务", "normal", 96)
    input_box(d, x + 150, y + 22, "关键字", "Echo", 210)
    table(d, x + 24, y + 78, ["ID", "Handler", "状态", "Cron", "下次执行", "重试"], [
        ("task-a7f1", "Echo", "待调度", "* * * * * ?", "2026-07-16 20:35:00", "0/3"),
        ("task-b4c2", "Echo", "执行中", "*/5 * * * * ?", "2026-07-16 20:40:00", "1/3"),
        ("task-c91e", "Echo", "停用", "0 0 * * * ?", "2026-07-17 00:00:00", "3/3"),
    ], [180, 130, 110, 190, 260, 90])
    rounded(d, (x + 24, y + 300, x + 980, y + 530), PANEL_ALT, outline=BORDER, radius=6)
    text(d, (x + 48, y + 322), "任务详情 / 操作", "#f9fafb", F_BODY_BOLD)
    fields = [("ID", "task-a7f1"), ("Cron", "* * * * * ?"), ("Handler", "Echo"), ("Params", "demo payload"), ("最大重试", "3")]
    fy = y + 360
    for k, v in fields:
        text(d, (x + 48, fy + 7), k, TEXT_MUTED, F_SMALL)
        rounded(d, (x + 150, fy, x + 520, fy + 30), "#1c2026", outline="#444c58", radius=4)
        text(d, (x + 164, fy + 6), v, TEXT, F_SMALL)
        fy += 36
    for i, (name, kind) in enumerate([("保存修改", "primary"), ("启用", "normal"), ("禁用", "normal"), ("立即执行", "primary"), ("删除", "danger")]):
        button(d, x + 560 + i * 112, y + 468, name, kind, 96)
    console(d, [
        ("[20:35:12] 任务列表已刷新，本页 3 条 / 共 12 条", "#93c5fd"),
        ("[20:35:18] 立即执行成功: Echo: demo payload", GREEN),
    ])
    img.save(OUT / "qt-task-management.png")


def metrics():
    img, d = chrome("Metrics")
    x, y, _, _ = content_area(d, "Metrics", "RPC、连接、任务、锁和调度延迟指标")
    groups = [
        ("RPC", [("rpc_requests_total", "8432"), ("rpc_success_total", "8429"), ("rpc_error_total", "3")]),
        ("连接", [("active_connections", "4"), ("rejected_connections", "0"), ("malformed_frames", "1")]),
        ("任务", [("task_success_total", "126"), ("task_failure_total", "2"), ("max_task_duration_ms", "37")]),
        ("Redis Lock", [("lock_acquire_success_total", "128"), ("lock_acquire_failure_total", "6")]),
        ("调度延迟", [("schedule_delay_p95_ms", "12"), ("schedule_delay_p99_ms", "18")]),
        ("任务耗时", [("task_duration_p95_ms", "9"), ("task_duration_p99_ms", "14")]),
    ]
    for idx, (name, items) in enumerate(groups):
        gx = x + 24 + (idx % 3) * 380
        gy = y + 28 + (idx // 3) * 194
        rounded(d, (gx, gy, gx + 340, gy + 152), PANEL_ALT, outline=BORDER, radius=6)
        text(d, (gx + 18, gy + 18), name, "#f9fafb", F_BODY_BOLD)
        iy = gy + 52
        for key, val in items:
            text(d, (gx + 20, iy), key, TEXT_MUTED, F_SMALL)
            text(d, (gx + 245, iy - 2), val, "#f9fafb", F_BODY_BOLD)
            iy += 30
    console(d, [
        ("[20:37:08] 运行指标已刷新", GREEN),
        ("$ curl -sS http://127.0.0.1:9091/alerts", TEXT_MUTED),
    ])
    img.save(OUT / "qt-metrics.png")


def demo_console():
    img, d = chrome("运维")
    x, y, _, _ = content_area(d, "运维", "依赖、控制节点、Worker、监控、测试和压测")
    sections = [
        ("运行环境", [("启动依赖", "primary"), ("停止依赖", "danger"), ("重置依赖", "danger"), ("环境检查", "normal")]),
        ("控制节点", [("启动服务端", "primary"), ("停止服务端", "danger"), ("启动二节点", "primary"), ("停止二节点", "danger")]),
        ("Worker", [("启动 Worker", "primary"), ("停止 Worker", "danger"), ("启动二号 Worker", "primary"), ("停止二号 Worker", "danger")]),
        ("可观测性", [("启动监控栈", "primary"), ("停止监控栈", "danger"), ("打开 Grafana", "normal"), ("查看 Alerts", "normal")]),
        ("质量与交付", [("构建测试", "normal"), ("集成/E2E", "normal"), ("构建镜像", "normal"), ("查看部署文档", "normal")]),
        ("压测", [("短连接压测", "primary"), ("长连接压测", "primary"), ("查看压测结果", "normal")]),
        ("协议异常", [("鉴权失败", "normal"), ("未知方法", "normal"), ("坏包断连", "danger")]),
    ]
    for si, (title, buttons) in enumerate(sections):
        sx = x + 24 + (si % 3) * 380
        sy = y + 22 + (si // 3) * 158
        rounded(d, (sx, sy, sx + 340, sy + 142), PANEL_ALT, outline=BORDER, radius=6)
        text(d, (sx + 18, sy + 16), title, "#f9fafb", F_BODY_BOLD)
        for i, (label, kind) in enumerate(buttons):
            button(d, sx + 18 + (i % 2) * 158, sy + 52 + (i // 2) * 42, label, kind, 138)
    console(d, [
        ("$ ./scripts/benchmark.sh 127.0.0.1 8081 16 1000 reuse", TEXT),
        ("requests=1000 concurrency=16 mode=reuse success=1000 failure=0 qps=12480 p95_ms=2", GREEN),
    ])
    img.save(OUT / "qt-demo-console.png")


def redis_discovery():
    img = Image.new("RGB", (W, 560), "#0f172a")
    d = ImageDraw.Draw(img)
    text(d, (42, 36), "Redis 服务发现快照", "#f8fafc", F_TITLE)
    rounded(d, (38, 92, W - 38, 520), "#111827", outline="#334155", radius=8)
    lines = [
        "== services:rpc members ==",
        "127.0.0.1:8081",
        "127.0.0.1:8082",
        "",
        "== services:worker members ==",
        "127.0.0.1:8181",
        "127.0.0.1:8182",
        "",
        "== services:worker:Echo capabilities ==",
        "127.0.0.1:8181",
        "127.0.0.1:8182",
        "",
        "services:worker:127.0.0.1:8181 ttl=27",
        "services:worker:Echo:127.0.0.1:8181 ttl=27",
    ]
    y = 120
    for line in lines:
        color = "#93c5fd" if line.startswith("==") else "#86efac" if line.startswith("127") or "ttl=" in line else "#cbd5e1"
        text(d, (68, y), line, color, F_MONO)
        y += 29
    img.save(OUT / "redis-service-discovery.png")


def benchmark_result():
    img = Image.new("RGB", (W, 650), BG)
    d = ImageDraw.Draw(img)
    text(d, (42, 36), "压测结果", "#f8fafc", F_TITLE)
    stats = [("模式", "reuse", "长连接复用"), ("QPS", "12480", "成功请求"), ("p95", "2 ms", "延迟"), ("p99", "4 ms", "延迟"), ("失败", "0", "请求数")]
    for i, card in enumerate(stats):
        stat_card(d, 42 + i * 270, 96, 230, 112, *card)
    rounded(d, (42, 250, W - 42, 604), "#111827", outline="#334155", radius=8)
    lines = [
        "# CorpCron Benchmark Result",
        "host=127.0.0.1 port=8081 concurrency=16 requests=1000 mode=reuse",
        "## server_before",
        "PID %CPU %MEM   RSS    VSZ ELAPSED CMD",
        "8421  0.3  0.1 18432 102400 00:01:12 build/corpcron_server --config config/server.conf",
        "## client_result",
        "requests=1000 concurrency=16 mode=reuse success=1000 failure=0 elapsed_sec=0.0801 qps=12480 p50_ms=1 p95_ms=2 p99_ms=4",
        "## alerts_after",
        "status=ok alerts_firing=0",
    ]
    y = 278
    for line in lines:
        color = "#93c5fd" if line.startswith("#") else "#86efac" if "success=1000" in line or "status=ok" in line else "#e5e7eb"
        text(d, (70, y), line, color, F_MONO)
        y += 32
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
