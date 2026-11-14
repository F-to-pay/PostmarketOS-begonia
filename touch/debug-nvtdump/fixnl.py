import sys
p = sys.argv[1]
s = open(p).read()
bad = 'status: 0x%02x\n", status);'
assert bad in s
s = s.replace(bad, 'status: 0x%02x' + chr(92) + 'n", status);')
open(p, 'w').write(s)
print('fixed')
