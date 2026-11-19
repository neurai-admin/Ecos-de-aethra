# Which CSV stats grow in the slowest frames? Usage: spikes.py <capture.csv> [top_n]
import csv, statistics as st, sys
csv.field_size_limit(1 << 30)
def num(v):
    try: return float(v)
    except (TypeError, ValueError): return None
rows = [r for r in csv.DictReader(open(sys.argv[1], newline='')) if num(r.get('FrameTime')) is not None]
rows.sort(key=lambda r: num(r['FrameTime']))
slow = rows[int(len(rows) * 0.99):]            # worst 1% = the frames that set p99
typical = rows[len(rows) // 4: 3 * len(rows) // 4]
def mean(rs, k):
    v = [x for x in (num(r.get(k)) for r in rs) if x is not None]
    return st.mean(v) if v else 0.0
keys = [k for k in rows[0] if k and k != 'EVENTS' and not k.startswith('GPUMem') and 'Memory' not in k and 'MB' not in k]
delta = sorted(((mean(slow, k) - mean(typical, k), k) for k in keys), reverse=True)
print(f"slow frames={len(slow)} avg={mean(slow,'FrameTime'):.2f}ms typical avg={mean(typical,'FrameTime'):.2f}ms")
for d, k in delta[: int(sys.argv[2]) if len(sys.argv) > 2 else 25]:
    print(f"  +{d:7.3f}  {k}  (slow {mean(slow,k):.3f} / typical {mean(typical,k):.3f})")
events = [r['EVENTS'] for r in slow if r.get('EVENTS')]
print('events in slow frames:', events[:10])
