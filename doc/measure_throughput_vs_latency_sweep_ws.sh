#!/bin/bash -e

# set environment stuff
pushd . > /dev/null
cd ../
. bootstrap.sh.UHEI
popd > /dev/null

DO_MEASURE_SWEEP_WS=
DO_MEASURE_ISO_PSWS=
DO_MEASURE_SWEEP_PS=
DO_MEASURE_SWEEP_SWACKDELAY=1



#### HELPERS
DEBUGLOGFILE=`mktemp`
echo "#### DEBUGLOGFILE is $DEBUGLOGFILE"
SWEEPTMPFILE=`mktemp`
echo "#### SWEEPTMPFILE is $SWEEPTMPFILE"

function helper_cleanup() {
    rm $SWEEPTMPFILE
    mv $DEBUGLOGFILE .
}

function helper_calc_ack_values() {
    test -z "$1" && echo "window size undefined?" && exit 1
    test -z "$2" && echo "ack sweep divider (lower value) undefined" && exit 1
    test -z "$3" && echo "ack sweep multiplier (upper value) undefined" && exit 1
    test -z "$4" && echo "ack sweep step value undefined" && exit 1
    #test -z "$5" && echo "ack sweep additional values undefined" && exit 1
    local myws=$1
    local mydn=$2
    local myup=$3
    local mystep=$4
    local myaddvals=$5

    CHECK=`echo "$mydn >= $myup" | bc`
    if [ $mydn -eq $myup ]; then
        echo "ack sweep divider ! < ack sweep multiplier!"
        exit 1
    fi

    # microseconds per frame
    NETWORKBWPRODUCTBASE=12

    # setting some globals
    local mySWEEPMIN=`echo "$myws * $NETWORKBWPRODUCTBASE/$mydn/1" | bc`
    local mySWEEPMAX=`echo "$myws * $NETWORKBWPRODUCTBASE*$myup/1" | bc`
    local mySWEEPSTEP=`echo "($mySWEEPMAX - $mySWEEPMIN)/$mystep" | bc`

    SWEEP_ACK_VALUES="`seq -s\  $mySWEEPMIN $mySWEEPSTEP $mySWEEPMAX` $myaddvals"
    
    #echo "sweeping ack values: $SWEEP_ACK_VALUES"
}


helper_modifyandcompile() {
    test -z $1 && echo "missing ws" && exit 1
    test -z $2 && echo "missing ack delay" && exit 1
    test -z $3 && echo "missing packet size" && exit 1
    local myws=$1
    local myackdelay=$2
    local mypacketsize=$3

    local myackdelayclks=`echo "$myackdelay * 125" | bc`
    
    #echo "comping for ws=$myws ack=$myackdelay/$myackdelayclks ps=$mypacketsize"
    
    # do re-compile stuff here
    pushd . > /dev/null
    cd ../components/SCtrlTP/userspace/
    echo "#### WINDOW_SIZE is $myws, ACK_DELAY is ${myackdelay}us, PACKET_SIZE is ${mypacketsize} (`date`)" | tee $SWEEPTMPFILE
    sed "s/^#define MAX_WINSIZ SEDIT$/#define MAX_WINSIZ $myws/" packets.h.presed > packets.h
    sed "s/^#define TODEL_HW_ACK_DELAY SEDIT$/#define TODEL_HW_ACK_DELAY $myackdelayclks/" us_sctp_defs.h.presed > us_sctp_defs.h
    sed "s/size_t wordsize = WSSEDIT/size_t wordsize = ${mypacketsize}/" sctp_test_core.c.presed > sctp_test_core.c
    ./waf install >> $SWEEPTMPFILE 2>&1 || exit 1
    sleep 1
    popd > /dev/null
}


helper_measure() {
    expect start_core.expect >> $SWEEPTMPFILE
    if [ $? -eq 0 ]; then
        tail -n 4 < $SWEEPTMPFILE
    else
        echo "FAILED"
    fi
    cat $SWEEPTMPFILE >> $DEBUGLOGFILE
}




#### WS sweep measurement (max packet size)
function MEASURE_SWEEP_WS {
    for ws in 1 2 4 8 16 32 64 128 256 512 1024 2048; do
        # sweep ack from base/10 .. base*2 in 10 steps + additional values
        helper_calc_ack_values $ws 10 2 10 "10 15 20 30 50 100 200 400 800 1600 3200 6400 12800 16640 25600 33280 51200"
        for ACK_DELAY in $SWEEP_ACK_VALUES; do
            PACKET_SIZE=176
            helper_modifyandcompile $ws $ACK_DELAY $PACKET_SIZE
            helper_measure
        done
    done
}

#### ISO (bits-in-flight) MEASUREMENT
function MEASURE_ISO_PSWS() {
    # PS*WS = 10080 = const
    PS=(  5 6 7 8 9 10 12 14 15 16 18 20 21 24 28 30 32 35 36 40 42 45 48 56 60 63 70 72 80 84 90 96 105 112 120 126 140 144 160 168 )
    WS=( 2016 1680 1440 1260 1120 1008 840 720 672 630 560 504 480 420 360 336 315 288 280 252 240 224 210 180 168 160 144 140 126 120 112 105 96 90 84 80 72 70 63 60 )

    # measure iso-bits-in-flight (ws vs ps)
    for idx in $(seq 0 1 `expr ${#WS[*]} - 1`); do
        ws=${WS[$idx]}
        ps=${PS[$idx]}
        # sweep ack from base/10 .. base in 10 steps
        helper_calc_ack_values $ws 10 1 10 ""
        for ACK_DELAY in $SWEEP_ACK_VALUES; do
            helper_modifyandcompile $ws $ACK_DELAY $ps
            helper_measure
        done
    done
}


# sweep packet size! (UUUH, ugly!)
function MEASURE_SWEEP_PS() {
    for ws in 2048 1024 512 256 128 64 32 16; do
        for PACKET_SIZE in 1 2 3 4 5 8 10 16 21 31 32 44 63 64 88 127 128 160 176; do
            # sweep ack from base/10 .. base*2 in 10 steps + additional values
            helper_calc_ack_values $ws 10 2 10 "10 15 20 30 50 100 200 400 800 1600 3200 6400 12800 16640 25600 33280 51200"
            for ACK_DELAY in $SWEEP_ACK_VALUES; do
                helper_modifyandcompile $ws $ACK_DELAY $PACKET_SIZE
                helper_measure
            done
        done
    done
}


helper_modifyandcompile2() {
    test -z $1 && echo "missing ws"                 && exit 1
    test -z $2 && echo "missing packet size"        && exit 1
    test -z $3 && echo "missing hw ack delay"       && exit 1
    test -z $4 && echo "missing hw master timeout"  && exit 1
    test -z $5 && echo "missing sw ack delay"       && exit 1
    test -z $6 && echo "missing sw master timeout"  && exit 1

    local myws=$1
    local mypacketsize=$2
    local myhwackdelay=$3
    local myhwmastertimeout=$4
    local myswackdelay=$5
    local myswmastertimeout=$6

    local myhwackdelayclks=`echo "$myhwackdelay * 125" | bc`
    local myhwmastertimeoutclks=`echo "$myhwmastertimeout * 125" | bc`

    # do re-compile stuff here
    pushd . > /dev/null
    cd ../components/SCtrlTP/userspace/
    echo "#### WINDOW_SIZE is $myws, PACKET_SIZE is ${mypacketsize}, HW_ACK_DELAY is ${myhwackdelay}us, HW_MASTER_TIMEOUT is ${myhwmastertimeout}us, SW_ACK_DELAY is ${myswackdelay}us, SW_MASTER_TIMEOUT is ${myswmastertimeout}us (`date`)" | tee $SWEEPTMPFILE

    sed "s/^#define MAX_WINSIZ SEDIT$/#define MAX_WINSIZ $myws/" packets.h.presed > packets.h

    # old-skool
    sed "s/^#define TODEL_HW_ACK_DELAY SEDIT$/#define TODEL_HW_ACK_DELAY $myhwackdelayclks/" us_sctp_defs.h.presed > us_sctp_defs.h.tmp
    sed "s/size_t wordsize = WSSEDIT/size_t wordsize = ${mypacketsize}/" sctp_test_core.c.presed2 > sctp_test_core.c

    # even more ugliness here
    sed "s,^//SEDADDMACRODEFSHERE,#define TODEL_HW_MASTER_TIMEOUT ${myhwmastertimeoutclks}\n#define TODEL_SW_MASTER_TIMEOUT ${myswmastertimeout}\n#define TODEL_SW_ACK_DELAY ${myswackdelay}\n," us_sctp_defs.h.tmp > us_sctp_defs.h
    rm us_sctp_defs.h.tmp

    ./waf install >> $SWEEPTMPFILE 2>&1 || exit 1

    sleep 1
    popd > /dev/null
}

#### ISO (bits-in-flight) MEASUREMENT
function MEASURE_SWEEP_SWACKDELAY() {
    ws=512
    ps=176

    # sw default ack delay was/is 100us
    local default_hw_ack_delay=100
    # hw default master timeout was/is 1000us
    local default_sw_master_timeout=1000

    # sweep ack from base/10 .. base in 10 steps
    helper_calc_ack_values $ws 10 1 20 "10 15 20 30 50 100 200 400 800 1600 3200 6400 12800 16640 25600 33280 51200"
    for ACK_DELAY in $SWEEP_ACK_VALUES; do
        local HW_MASTER_TIMEOUT=`echo "2 * (${ACK_DELAY} + 1)" | bc`
        helper_modifyandcompile2 $ws $ps $default_hw_ack_delay $HW_MASTER_TIMEOUT $ACK_DELAY $default_sw_master_timeout
        helper_measure
    done
}


if [ -n "$DO_MEASURE_SWEEP_WS" ]; then
    MEASURE_SWEEP_WS
fi
if [ -n "$DO_MEASURE_ISO_PSWS" ]; then
    MEASURE_ISO_PSWS
fi
if [ -n "$DO_MEASURE_SWEEP_PS" ]; then
    MEASURE_SWEEP_PS
fi

if [ -n "$DO_MEASURE_SWEEP_SWACKDELAY" ]; then
    MEASURE_SWEEP_SWACKDELAY
fi
