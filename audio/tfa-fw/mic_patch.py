p = '/mnt/c/Users/1342c/workspace/Begonia/wifi/audio/codecs/mt6359.c'
s = open(p, encoding='utf-8').read()
old = r'''static int mic_type0 = MIC_TYPE_MUX_DCC;
module_param(mic_type0, int, 0444);
MODULE_PARM_DESC(mic_type0, "mic type of MIC0 (default 3 = DCC: needed on begonia, otherwise the preamplifier saturates on the DC offset; -1: from DT/idle)");
'''
new = old + r'''
/* secondary microphones (noise suppression): default -1 = do not touch (from DT/idle); 3 = DCC for experiments */
static int mic_type1 = -1;
module_param(mic_type1, int, 0444);
MODULE_PARM_DESC(mic_type1, "mic type of MIC1 (AIN1, micbias1): -1 from DT/idle, 1 acc, 3 dcc");
static int mic_type2 = -1;
module_param(mic_type2, int, 0444);
MODULE_PARM_DESC(mic_type2, "mic type of MIC2 (AIN2/AIN3, micbias2): -1 from DT/idle, 1 acc, 3 dcc");
'''
assert old in s
s = s.replace(old, new, 1)
old2 = r'''	if (mic_type0 >= 0)
		priv->mux_select[MUX_MIC_TYPE_0] = mic_type0;
'''
new2 = old2 + r'''	if (mic_type1 >= 0)
		priv->mux_select[MUX_MIC_TYPE_1] = mic_type1;
	if (mic_type2 >= 0)
		priv->mux_select[MUX_MIC_TYPE_2] = mic_type2;
'''
assert old2 in s
s = s.replace(old2, new2, 1)
open(p, 'w', encoding='utf-8').write(s)
print('ok')
