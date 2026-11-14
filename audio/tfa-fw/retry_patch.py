p = '/mnt/c/Users/1342c/workspace/Begonia/wifi/audio/codecs/tfa9874.c'
s = open(p, encoding='utf-8').read()


def rep(old, new):
    global s
    assert old in s, old[:70]
    s = s.replace(old, new, 1)


rep(r'''	struct delayed_work mon;
''', r'''	struct delayed_work mon;
	struct delayed_work retry;	/* automatic restart after a pause while DAPM holds the stream */
	bool want_on;			/* DAPM wants the amplifier running (a stream is on) */
''')

# after a hard fault without a latch schedule a restart
rep(r'''	dev_err(t->dev, "PROTECTION: FAULT #%u: %s (STATUS 0x10=%04x 0x11=%04x), amplifier off%s\n",''',
    r'''	if (!t->latched)
		schedule_delayed_work(&t->retry, msecs_to_jiffies(TFA_FAULT_COOLDOWN_MS + 100));

	dev_err(t->dev, "PROTECTION: FAULT #%u: %s (STATUS 0x10=%04x 0x11=%04x), amplifier off%s\n",''')

retry = r'''static int tfa9874_power_up(struct tfa9874 *t);

static void tfa9874_retry(struct work_struct *w)
{
	struct tfa9874 *t = container_of(to_delayed_work(w), struct tfa9874, retry);

	mutex_lock(&t->lock);
	if (t->want_on && !t->powered && !t->latched && allow_amp) {
		dev_info(t->dev, "PROTECTION: pause is over, trying to start the amplifier again\n");
		if (tfa9874_power_up(t) == -EBUSY)
			schedule_delayed_work(&t->retry, HZ);
	}
	mutex_unlock(&t->lock);
}

'''
rep(r'''static void tfa9874_monitor(struct work_struct *w)
{''', retry + r'''static void tfa9874_monitor(struct work_struct *w)
{''')

rep(r'''		if (!allow_amp) {
			dev_info(t->dev, "there is a stream to the speaker, but allow_amp=0: not turning the amplifier on\n");
			break;
		}
		if (!t->powered && tfa9874_power_up(t))
			dev_err(t->dev, "amplifier not started, the speaker stays silent\n");
		break;
	case SND_SOC_DAPM_PRE_PMD:
		if (t->powered)''', r'''		t->want_on = true;
		if (!allow_amp) {
			dev_info(t->dev, "there is a stream to the speaker, but allow_amp=0: not turning the amplifier on\n");
			break;
		}
		if (!t->powered && tfa9874_power_up(t))
			dev_err(t->dev, "amplifier not started, the speaker stays silent\n");
		break;
	case SND_SOC_DAPM_PRE_PMD:
		t->want_on = false;
		cancel_delayed_work(&t->retry);
		if (t->powered)''')

rep(r'''	INIT_DELAYED_WORK(&t->mon, tfa9874_monitor);''', r'''	INIT_DELAYED_WORK(&t->mon, tfa9874_monitor);
	INIT_DELAYED_WORK(&t->retry, tfa9874_retry);''')
rep(r'''		cancel_delayed_work_sync(&t->mon);
	}''', r'''		cancel_delayed_work_sync(&t->mon);
		cancel_delayed_work_sync(&t->retry);
	}''')
open(p, 'w', encoding='utf-8').write(s)
print('patched')
