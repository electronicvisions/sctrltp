
PLOTNAME=$(basename $0 | sed -n 's/plot_\(.*\).sh/\1/p')
PLOTFILETYPE=pgf

if [ -n $1 ]; then
    echo "setting $PLOTNAME"
    PLOTNAME=`basename $1 .dat`
fi
DATAFILE=$PLOTNAME.dat

TMP1=`mktemp`
TMP2=`mktemp`
TMPDATA=`mktemp`
sed -n 's,.*rx rate =\s*\(.*\)MB/s,\1,p' < $DATAFILE > $TMP1
sed -n 's,.*tx rate =\s*\(.*\)MB/s,\1,p' < $DATAFILE > $TMP2
paste $TMP1 $TMP2 > $TMPDATA
rm $TMP1
rm $TMP2


# MB/s (119635891 b/s)
THEOMAX=114.09
THEOMAXRED=`echo "scale=2; 125000000 / 1538 * (1500-20-8-16) / 1024 / 1024" | bc -l`
# -16 weil CRC und interframe gap nicht mac-level sind (was man als duty cycle in sim gesehen hatte)
THEOMAXREDTUD=`echo "scale=2; 125000000 / (1538-16) * (1500-20-8-16) / 1024 / 1024 * 11.824/17.952" | bc -l`

# frames/s
THEOINTMAX=81274

USPERFRAME=12
PACKETSIZE=176

python <<EOF
from pylab import *
data = loadtxt("$TMPDATA")
exclude_ws = []#[3,5,6,7,9,10]
wsizes = [512]
fig = figure(figsize=(8,5)) # 8,10 is default
ax = fig.add_subplot(111)
rows = int(sqrt(len(wsizes)))
cols = int(round(1.0*len(wsizes)/rows))

leg = None
for i, ws in enumerate(wsizes):
    ldata = data[30:]
    xpoints = array(range(0, len(ldata)))
    sax = fig.add_subplot(rows, cols, i+1)
    
    plot_protowirespeed = sax.axhline(y=$THEOMAXRED, color='r', linestyle='--', label='\$\\mathrm{T_{protocol}}\$')
    plot_wirespeed      = sax.axhline(y=$THEOMAX, color='r', linestyle=':', label='\$\\mathrm{T_{wirespeed}}\$')
    plot_prototudspeed  = sax.axhline(y=$THEOMAXREDTUD, color='r', linestyle='-.', label='\$\\mathrm{T_{TUD\ UDP\ core}}\$')
    
    plot_tx             = sax.plot(xpoints/10.0, ldata[:,1]/1.024**2, label='\$\\mathrm{TX}\$')
    plot_rx             = sax.plot(xpoints/10.0, ldata[:,0]/(1.024**2), label='\$\\mathrm{RX}\$')
    
    sax.set_ylim([70, 115])
#    sax.set_xlim([0, 16])

#    sax.yaxis.set_ticks([70, 80, 90, 100, 110, 120])
    if i == 1 or (rows * cols == 1):
        lines, labels = sax.get_legend_handles_labels()
        leg = sax.legend(lines, labels, loc=5)
        leg.get_frame().set_alpha(0.9)

#ax.set_title("Host to FPGA (TX)\n\n")
ax.set_ylabel(r"T: Throughput [\si[per-mode=symbol]{\mebi\byte\per\second}]", labelpad=10)
ax.set_xlabel(r"Time [\si{\second}]", labelpad=10)

if (rows * cols) != 1:
    ax.set_xticks([])
    ax.set_yticks([])
    ax.spines['top'].set_color('none')
    ax.spines['bottom'].set_color('none')
    ax.spines['left'].set_color('none')
    ax.spines['right'].set_color('none')
    ax.tick_params(labelcolor='w', top='off', bottom='off', left='off', right='off')


savefig("$PLOTNAME.$PLOTFILETYPE", dpi=300)
EOF
