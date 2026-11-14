import re, os, sys
root = os.path.expanduser('~/gen4m-port')
src = open(root + '/os/linux/gl_init.c', encoding='utf-8', errors='replace').read().split('\n')
errs = open(os.path.expanduser('~/gen4m-port/errors.txt')).read().split('\n')
want = []
for e in errs:
    m = re.match(r'\S*gl_init\.c:(\d+):\d+: error: (incompatible function pointer|field designator)', e)
    if m: want.append(int(m.group(1)))
names = []
for ln in sorted(set(want)):
    t = src[ln - 1].strip()
    m = re.match(r'\.(\w+)\s*=\s*(\w+)', t)
    names.append((m.group(1), m.group(2)) if m else (t, ''))
print('ops failing:', names)
hdr = open(os.path.expanduser('~/mt6785-linux/include/net/cfg80211.h'), encoding='utf-8', errors='replace').read()
for op, impl in names:
    m = re.search(r'\n\t(?:int|void|struct [\w ]+ \*|\w+)\s*\(\*%s\)\s*\((.*?)\);' % re.escape(op), hdr, re.S)
    print('\n6.16 %-28s (%s)' % (op, re.sub(r'\s+', ' ', m.group(1)) if m else 'NOT IN cfg80211_ops'))
