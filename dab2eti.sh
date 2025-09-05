#!/bin/bash

#wrapper script to provide compatibility with dablin
#use in place of dab2eti, e.g.: `dablin -d ./dab2eti.sh -c 7C -g 61 -s 0x2206` 

#set these variables based on your needs
##################################################
SDR_DEVICE="0"; #device index or serial number
PPM=0; #ppm for the rtlsdr if known
TYPE=rtlsdr; #rtlsdr or wavefinder
##################################################

FREQ=$1;
GAIN="-g $2";

if [[ -z "$2" ]]; then
   GAIN="";
fi

./build/src/dab2eti -f "$FREQ" -d "$SDR_DEVICE" -t "$TYPE" -p $PPM $GAIN;
