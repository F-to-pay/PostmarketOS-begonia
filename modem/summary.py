import sys, collections
d = open(sys.argv[1], 'rb').read().decode('utf-8', 'replace').split('\n')
rows = []
for l in d:
    if 'md-bringup' in l and not l.startswith(' '):
        hdr, msg = l.split(';', 1)
        rows.append((int(hdr.split(',')[2]) / 1e6, msg.encode('latin-1', 'replace').decode('utf-8', 'replace')[len('md-bringup soc:md-bringup: '):]))
c = collections.Counter()
for ts, t in rows:
    for k in ('RX q0', 'RX q1', 'RX q2', 'RX q3', 'RX q4', 'RX q5', 'TX q4', 'CONTROL', 'RAISED', 'bad'):
        if t.startswith(k) or k in t[:24]:
            c[k] += 1
print(dict(c), 'module lines:', len(rows))
other = collections.Counter()
for ts, t in rows:
    if t.startswith('RX q') and 'ch=14' not in t and 'ch=32' not in t:
        other[t[:40]] += 1
print('other channels:', dict(other))
for ts, t in rows:
    if t.startswith('CONTROL') or 'RAISED' in t or 'HS1 received' in t:
        print(round(ts, 2), t[:200])
fs = [(ts, t) for ts, t in rows if t.startswith('RX q4')]
if fs:
    print('FS requests:', len(fs), 'from', round(fs[0][0], 2), 'to', round(fs[-1][0], 2))
    print('last:', fs[-1][1][:200])
