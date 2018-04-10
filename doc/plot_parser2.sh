PLOTNAME=$(basename $0 | sed -n 's/plot_\(.*\).sh/\1/p')
PLOTFILETYPE=png

if [ -n $1 ]; then
    echo "setting $PLOTNAME"
    PLOTNAME=`basename $1 .dat`
fi
DATAFILE=$PLOTNAME.dat

if [ -z $PLOTNAME ]; then
    echo "#### PLOTNAME is empty!"
    exit 1
else
    echo "#### Plotting to $PLOTNAME.$PLOTFILETYPE"
fi

if [ -r $DATAFILE ]; then
    echo "#### Plotting data file $DATAFILE"
else
    echo "#### DATAFILE ($DATAFILE) not readable???"
    exit 1
fi

TMP1=`mktemp`
TMP2=`mktemp`
TMP3=`mktemp`
TMP4=`mktemp`
TMP5=`mktemp`
TMPDATA=`mktemp`
           #### WINDOW_SIZE is 512        , PACKET_SIZE is 176us, HW_ACK_DELAY is 100us, HW_MASTER_TIMEOUT is 1230us, SW_ACK_DELAY is 614us, SW_MASTER_TIMEOUT is 1000us (Mon Feb 17 14:27:55 CET 2014)
sed -n 's/^#### WINDOW_SIZE is \([0-9]\+\), PACKET_SIZE is \([0-9]\+\), HW_ACK_DELAY is \([0-9]\+\)us, HW_MASTER_TIMEOUT is \([0-9]\+\)us, SW_ACK_DELAY is \([0-9]\+\)us, SW_MASTER_TIMEOUT is \([0-9]\+\)us .*/\1 \2 \3 \4 \5/p' < $DATAFILE > $TMP1
sed -n "s,^\(sent\|recv\) avg = \([0-9.]*\) +/- \([0-9.]*\)MB/s.*,\2 \3,p" < $DATAFILE > $TMP2
sed -n "s,^interrupt rate = \([0-9.]*\) +/- \([0-9.]*\)intr/s.*,\1 \2,p" < $DATAFILE > $TMP3
sed -n "s,^interrupt2 rate = \([0-9.]*\) +/- \([0-9.]*\)intr/s.*,\1 \2,p" < $DATAFILE > $TMP4
sed -n "s,^packet rate = \([0-9.]*\) +/- \([0-9.]*\)pckts/s.*,\1 \2,p" < $DATAFILE > $TMP5

LEN1=`cat $TMP1 | wc -l`
LEN2=`cat $TMP2 | wc -l`
LEN3=`cat $TMP3 | wc -l`
LEN4=`cat $TMP4 | wc -l`
LEN5=`cat $TMP5 | wc -l`

if  [[ $LEN1 -ne $LEN2 || $LEN2 -ne $LEN3 || $LEN3 -ne $LEN4 || $LEN4 -ne $LEN5 ]]; then
    echo "sizes did not match!"
    wc -l $TMP1
    wc -l $TMP2
    wc -l $TMP3
    wc -l $TMP4
    wc -l $TMP5
    exit 1
fi

paste $TMP1 $TMP2 $TMP3 $TMP4 $TMP5 > $TMPDATA
#echo $TMP1
#head -n 1 $TMP1
#echo $TMP2
#head -n 1 $TMP2
#echo $TMP3
#head -n 1 $TMP3
#paste -d'\t\t\t'  $TMP1 $TMP2 | head -n 2
#paste -d'\t\t\t'  $TMP2 $TMP3 | head -n 2

#echo $TMP1 $TMP2 $TMP3
#wc -l $TMP1
#wc -l $TMP1
#wc -l $TMP3

rm $TMP1
rm $TMP2
rm $TMP3
rm $TMP4
rm $TMP5
#exit 1
