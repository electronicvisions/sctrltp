source `dirname "${BASH_SOURCE[0]}"`/plot_parser.sh

# MB/s (wirespeed)
THEOMAX=`echo "scale=2; 125000000 / 1538 * 1500 / 1024 / 1024" | bc -l`
THEOMAXRED=`echo "scale=2; 125000000 / 1538 * (1500-20-8-16) / 1024 / 1024" | bc -l`

# frames/s
THEOINTMAX=81274

USPERFRAME=12
PACKETSIZE=176

#cat $TMPDATA | head
#cat $TMPDATA | tail

python <<EOF
#import matplotlib as mpl
#mpl.use("pgf")
from pylab import *
data = loadtxt("$TMPDATA")
exclude_ws = [3,5,6,7,9,10]
wsizes = [ws for ws in list(sorted(set(data[:,0]))) if not ws in exclude_ws]
fig = figure()
ax = fig.add_subplot(111)
rows = int(sqrt(len(wsizes)))
cols = int(round(1.0*len(wsizes)/rows))

#ws_vs_ps = zip((32, 64, 128, 256, 512, 1024, 2048), (176, 88, 44, 22, 11, 6, 3))
PS = map(int, "5 6 7 8 9 10 12 14 15 16 18 20 21 24 28 30 32 35 36 40 42 45 48 56 60 63 70 72 80 84 90 96 105 112 120 126 140 144 160 168".split())
WS = map(int, "2016 1680 1440 1260 1120 1008 840 720 672 630 560 504 480 420 360 336 315 288 280 252 240 224 210 180 168 160 144 140 126 120 112 105 96 90 84 80 72 70 63 60".split())

ws_vs_ps = zip(WS,PS)


DEBUG = True

peaks = []
for i, (ws, ps) in enumerate(ws_vs_ps):
    import numpy as np
    # filter high intr/s variation points
    ldata = data[(data[:,0] == ws) & (data[:,2] == ps)]# & (data[:,6] < 15000) & (data[:,8] < 15000)]
    if len(ldata) == 0:
        print "combination %s %s missing" % (ws, ps)
        continue

    maxval = ldata[argmax(ldata[:,3])] #- ldata[:,4])]
    peaks.append(maxval.tolist())

    if not DEBUG:
        continue
    sax = fig.add_subplot(rows, cols, i+1)
    eplot = sax.errorbar(ldata[:,1], ldata[:,3], yerr=ldata[:,4], fmt='.', label='%d' % ws)
    sax.axvline(x=12.0*ws, color=eplot[0].get_color())
    sax.axhline(y=$THEOMAX, color='r', linestyle='dashed', label='max')
    sax.axhline(y=$THEOMAXRED, color='r', linestyle='dashdotted', label='max')
    sax.plot(maxval[1], maxval[3], 'ro')
    sax.set_xlim([5, 55000])
    sax.set_ylim([0,125])
    sax.set_xscale('log', basex=10)
    sax.set_title('%d' % ws)
    bax = sax.twinx()
    intrplot = bax.errorbar(ldata[:,1], ldata[:,5], ldata[:,6], fmt='g.')
    bax.set_ylim([1e4,1e6])
    bax.set_yscale('log', basex=10)

    if i % cols != 0:
        sax.yaxis.set_ticks([])
    else:
        sax.yaxis.set_ticks([0, 40, 80, 120])
    if i % cols != cols - 1:
        bax.yaxis.set_ticks([])
    if (i) / cols != rows - 1:
        sax.set_xticks([])
        bax.set_xticks([])

if DEBUG:
    ax.set_ylabel("T: Throughput [\si[per-mode=symbol]{\mebi\byte\per\second}]", labelpad=40)
    ax2 = ax.twinx()
    ax2.set_ylabel("R: Rate [\si{\per\second}]", labelpad=20)
    ax.set_xlabel("ACK Delay [\si{\micro\second}]", labelpad=40)
    ax.set_xticks([])
    ax.set_yticks([])

    for axx in [ax,ax2]:
        axx.spines['top'].set_color('none')
        axx.spines['bottom'].set_color('none')
        axx.spines['left'].set_color('none')
        axx.spines['right'].set_color('none')
        axx.tick_params(labelcolor='w', top='off', bottom='off', left='off', right='off')

else:
    peaks = array(peaks)
    ax = fig.add_subplot(111)
    arq_header = 4+4+4+4
    offset = arq_header + 7+1+6+6+2+20+8+4+12 # => 82 byte offset
    peaks_bytes = peaks[:,2]*8
    tmp = (peaks_bytes).tolist()
    tmp = arange(0, 1500-16-8-20 + 0.1)
    peaks_bytes = array(tmp)
    #print peaks_bytes
    
    total_frame_size = peaks_bytes + offset
    
    normalized_frame_size = peaks_bytes/total_frame_size
    tb = normalized_frame_size * 125000000/1024/1024
    
    plot_wirespeed = ax.axhline(y=$THEOMAX, color='r', linestyle=':', label='\$\\mathrm{T_{wirespeed}}\$')
    plot_protowirespeed = ax.axhline(y=$THEOMAXRED, color='r', linestyle='--', label='\$\\mathrm{T_{protocol}}\$')
    plot_theo = ax.plot(peaks_bytes/8, tb, color='r', label='\$\\mathrm{T_{packet \/ size}}\$')
    plot_throughput = ax.errorbar(peaks[:,2], peaks[:,3], peaks[:,4], fmt='o', label='\$\\mathrm{T}\$')
    ax.set_ylabel(r"T: Throughput [\si[per-mode=symbol]{\mega\byte\per\second}]", labelpad=5)
    ax.set_xlabel(r"\parbox{15em}{Packet Size [64-bit commands] \\\\ Window Size [\# Packets]}", labelpad=10)
    labels = [r'\parbox{2em}{%s \\\\ %s}' % (p, w) for w, p in zip(WS, PS)]
    #labels = ['%s' % p for w, p in zip(WS, PS)]
    bax = ax.twinx()
    ticks = [0, 2*len(PS)/4, 3*len(PS)/4, 7*len(PS)/8, -1]
    ax.xaxis.set_ticks([PS[tick] for tick in ticks])
    ax.xaxis.set_ticklabels([labels[tick] for tick in ticks])
    
    plot_theopacketrate = bax.plot(peaks_bytes/8, (125000000)/total_frame_size, color='y', label='\$\\mathrm{R_{wirespeed}}\$')
    plot_packetrate     = bax.errorbar(peaks[:,2], peaks[:,9], peaks[:,10], color='y', fmt='D', label='\$\\mathrm{R_{packet}}\$')
    
    plot_interruptrate  = bax.errorbar(peaks[:,2], peaks[:,5], peaks[:,6], color='g', fmt='^', label='\$\\mathrm{R_{interrupt \/ (nic)}}\$')
    #plot_interruptrate2 = bax.errorbar(peaks[:,2], peaks[:,7], peaks[:,8], color='g', fmt='v', label='\$\\mathrm{interrupt \/ rate \/ (timer)}\$')
    
    bax.set_ylabel("R: rate [1/s]", labelpad=0)
    bax.set_ylim([0,4e5])
    bax.yaxis.set_ticks([50e3, 100e3, 150e3, 200e3, 250e3, 300e3, 350e3, 400e3])
    bax.yaxis.set_ticklabels(['50k', '100k', '150k', '', '250k', '300k', '350k', '400k'])
    lines, labels = ax.get_legend_handles_labels()
    lines2, labels2 = bax.get_legend_handles_labels()
    leg = bax.legend(lines+lines2, labels+labels2, loc=7)#, prop={'size':12})
    leg.get_frame().set_alpha(0.9)

if DEBUG:
    savefig("$PLOTNAME-DebugTrue.$PLOTFILETYPE", dpi=300)
else:
    savefig("$PLOTNAME.$PLOTFILETYPE", dpi=300)
EOF
