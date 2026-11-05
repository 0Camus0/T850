"""Summarise a runtime-telemetry JSON: per-frame medians over the second half of the run."""
import json
import statistics
import sys

path, api, tag, balls, errors = sys.argv[1:6]
with open(path, encoding="utf-8") as handle:
    data = json.load(handle)

frames = [f for f in data["frames"] if not f["startup"]]
frames = frames[len(frames) // 2:]   # skip load, the scripted drop and the spawned balls landing

WATCH = ["frame.total", "frame.update", "scene.update", "frame.draw", "gpu.gpu_wait",
         "gpu.present", "frame.swap_buffers"]


def median_of(name):
    values = []
    for frame in frames:
        total = 0.0
        for scope in frame["scopes"]:
            if scope["name"] == name:
                total += scope["totalMs"]
        values.append(total)
    return statistics.median(values) if values else float("nan")


total = median_of("frame.total")
totals = sorted(sum(s["totalMs"] for s in f["scopes"] if s["name"] == "frame.total") for f in frames)
mean = statistics.fmean(totals) if totals else float("nan")
spikes = sum(1 for t in totals if t > 3.0 * total)
p99 = totals[int(0.99 * (len(totals) - 1))] if totals else float("nan")
parts = "  ".join("%s=%.3f" % (name.split(".", 1)[1], median_of(name)) for name in WATCH[1:])
print("%-7s %-14s balls=%-3s frames=%-5d fps(mean)=%6.1f fps(median)=%6.1f p99=%.2f ms spikes=%d max=%.1f ms  %s  errors=%s"
      % (api, tag, balls, len(frames), 1000.0 / mean if mean > 0 else 0.0,
         1000.0 / total if total > 0 else 0.0, p99, spikes, totals[-1] if totals else 0.0, parts, errors))

if "--scopes" in sys.argv:
    names = sorted({s["name"] for f in frames for s in f["scopes"]})
    rows = sorted(((median_of(n), n) for n in names), reverse=True)
    for value, name in rows[:25]:
        print("    %8.3f ms  %s" % (value, name))
