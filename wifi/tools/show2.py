import os, sys
root = os.path.expanduser('~/gen4m-port')
cur = None
for a in sys.argv[1:]:
    if '-' in a and a.replace('-', '').isdigit():
        lo, hi = map(int, a.split('-'))
        lines = open(os.path.join(root, cur), encoding='utf-8', errors='replace').read().replace('\r\n', '\n').split('\n')
        print('----- %s %d-%d' % (cur, lo, hi))
        for i in range(lo, min(hi, len(lines)) + 1):
            print('%5d  %s' % (i, lines[i - 1][:150]))
    else:
        cur = a
