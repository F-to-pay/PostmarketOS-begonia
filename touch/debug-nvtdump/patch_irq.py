import sys
p = sys.argv[1]
s = open(p).read()
# param declaration right after the nvt_ts_irq_type table
anchor = 'static irqreturn_t nvt_ts_irq(int irq, void *dev_id)'
assert s.count(anchor) == 1
decl = ('static int irqmode;\nmodule_param(irqmode, int, 0444);\n'
        'MODULE_PARM_DESC(irqmode, "0 = trigger from firmware param, 1 = rising edge (downstream), 2 = falling edge");\n\n')
s = s.replace(anchor, decl + anchor, 1)
old = '''IRQF_ONESHOT | IRQF_NO_AUTOEN |
						nvt_ts_irq_type[irq_type],'''
new = '''IRQF_ONESHOT | IRQF_NO_AUTOEN |
						(irqmode == 1 ? IRQF_TRIGGER_RISING :
						 irqmode == 2 ? IRQF_TRIGGER_FALLING :
						 nvt_ts_irq_type[irq_type]),'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new, 1)
open(p, 'w').write(s)
print('core patched')
