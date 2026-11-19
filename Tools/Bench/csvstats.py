# Summarise an Unreal csvprofile capture. Usage: csvstats.py <capture.csv> [warmup_frames]
import csv
import statistics as st
import sys

csv.field_size_limit(1 << 30)  # the trailing metadata row is huge


def num(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


rows = [r for r in csv.DictReader(open(sys.argv[1], newline='')) if num(r.get('FrameTime')) is not None]
rows = rows[int(sys.argv[2]) if len(sys.argv) > 2 else 0:]


def col(name):
    return sorted(x for x in (num(r.get(name)) for r in rows) if x is not None)


def pct(values, q):
    return values[min(len(values) - 1, int(q * len(values)))]


ft = col('FrameTime')
assert ft, 'no FrameTime samples'
print(f"frames={len(ft)} avgFPS={1000 * len(ft) / sum(ft):.1f} avgFT={st.mean(ft):.2f}ms "
      f"p50={pct(ft, .5):.2f} p95={pct(ft, .95):.2f} p99={pct(ft, .99):.2f} max={ft[-1]:.2f}")
for name in ('GameThreadTime', 'RenderThreadTime', 'RHIThreadTime', 'GPUTime', 'PhysicalUsedMB', 'GPUMem/LocalUsedMB'):
    values = col(name)
    if values:
        print(f"  {name}: avg={st.mean(values):.2f} p95={pct(values, .95):.2f} max={values[-1]:.2f}")
for name in sorted(k for k in rows[0] if k and k.startswith('Aethra/')):
    active = [x for x in col(name) if x > 0]
    if active:
        print(f"  {name}: frames={len(active)} avg={st.mean(active):.2f}ms p95={pct(active, .95):.2f} max={active[-1]:.2f}")
