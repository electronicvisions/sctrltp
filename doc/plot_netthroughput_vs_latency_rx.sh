source `dirname "${BASH_SOURCE[0]}"`/plot_parser2.sh

# MB/s (119635891 b/s)
THEOMAX=114.09
THEOMAXRED=`echo "scale=2; 125000000 / 1538 * (1500-20-8-16) / 1024 / 1024" | bc -l`
THEOMAXREDTUD=`echo "scale=2; 125000000 / 1538 * (1500-20-8-16) / 1024 / 1024 * 11.824/17.952" | bc -l`

# frames/s
THEOINTMAX=81274

USPERFRAME=12
PACKETSIZE=176

#cat $TMPDATA |head

python <<EOF
from pylab import *
data = loadtxt("$TMPDATA")
exclude_ws = []#[3,5,6,7,9,10]
wsizes = [ws for ws in list(sorted(set(data[:,0]))) if not ws in exclude_ws]
fig = figure()
ax = fig.add_subplot(111)
rows = int(sqrt(len(wsizes)))
cols = int(round(1.0*len(wsizes)/rows))

leg = None
for i, ws in enumerate(wsizes):
    ldata = data[(data[:,0] == ws) & (data[:,1] == $PACKETSIZE)]
    #ldata = data[ldata[:,1] == $PACKETSIZE] # max packet size
    sax = fig.add_subplot(rows, cols, i+1)
    
    plot_product        = sax.axvline(x=12.0*ws, color='r', label='\$D = C / T\$')
    plot_protowirespeed = sax.axhline(y=$THEOMAXRED, color='r', linestyle='--', label='\$\\mathrm{T_{protocol}}\$')
    plot_prototudspeed  = sax.axhline(y=$THEOMAXREDTUD, color='r', linestyle='-.', label='\$\\mathrm{T_{TUD\ UDP\ core}}\$')
    plot_wirespeed      = sax.axhline(y=$THEOMAX, color='r', linestyle=':', label='\$\\mathrm{T_{wirespeed}}\$')
    plot_eplot          = sax.errorbar(ldata[:,4], ldata[:,5], yerr=ldata[:,6], fmt='.', label='\$\\mathrm{T}\$')
    
    sax.set_xscale('log', basex=10)
    sax.set_xlim([5, 55000])
    sax.set_ylim([0, 120])
    #sax.set_title('window %d' % ws)
    #bax = sax.twinx()
    #intrplot = bax.errorbar(ldata[:,1], ldata[:,5], ldata[:,6], fmt='g.')
    #bax.axhline(y=$THEOINTMAX, color='g', linestyle='dashdot')
    #bax.set_ylim([0,4e5])
    #bax.set_ylim([1e4,1e6])
    #bax.set_yscale('log', basex=10)
    #if i % cols == cols - 1:
    #    bax.yaxis.set_ticks([50e3, 100e3, 150e3, 200e3, 250e3, 300e3, 350e3, 400e3])
    #    bax.yaxis.set_ticklabels(['50k', '100k', '150k', '200k', '250k', '300k', '350k', '400k'])
    #    #bax.yaxis.set_ticks([ 1e4, 1e5, 1e6])
    #    #bax.yaxis.set_ticklabels(['10k', '100k', '1M'])
    #else:
    #    bax.yaxis.set_ticks([])
    if i % cols != 0 and (rows*cols != 1):
        sax.yaxis.set_ticks([])
    else:
        sax.yaxis.set_ticks([0, 20, 40, 60, 80, 100, 120])
        #sax.yaxis.set_ticklabels(labels)
    if ((i / cols) != rows - 1) and (rows*cols != 1):
        sax.set_xticks([])
    if i == 1 or (rows * cols == 1):
        #legend(loc=1, numpoints=1)
        lines, labels = sax.get_legend_handles_labels()
        leg = sax.legend(lines, labels)
        leg.get_frame().set_alpha(0.9)

#leg.set_zorder(2000)

#ax.set_title("Host to FPGA (TX)\n\n")
ax.set_ylabel("T: throughput [MiB/s]", labelpad=10)
ax.set_xlabel("ACK Delay [\$\mu s\$]", labelpad=10)

if (rows * cols) != 1:
    ax.set_xticks([])
    ax.set_yticks([])
    ax.spines['top'].set_color('none')
    ax.spines['bottom'].set_color('none')
    ax.spines['left'].set_color('none')
    ax.spines['right'].set_color('none')
    ax.tick_params(labelcolor='w', top='off', bottom='off', left='off', right='off')


#bax = ax.twinx()
#bax.set_ylabel("rate [1/s]", fontsize=16, labelpad=30)
#bax.yaxis.set_ticks([])

savefig("$PLOTNAME.$PLOTFILETYPE", dpi=300)
EOF
