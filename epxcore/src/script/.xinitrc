#!/bin/sh

if [ -e "/var/run/epcv.pid" ] ; then
	APP=/opt/vgp02/bin/vgp02
	CONF=/opt/vgp02/conf/vgp02.properties
	BIN=/opt/vgp02/bin
else
	APP=/opt/eps02/bin/epgui
	CONF=/opt/eps02/conf/epgui.properties
	BIN=/opt/eps02/bin
fi

setxkbdmap -layout "it"

# Scommentare per eseguire il software di profiling
#$BIN/syspars.pl > $( date +"/tmp/syspars-20%y%m%d%H%M%S.csv" ) &


# spegne il cursore
unclutter -idle 0 &

# spegne lo screensaver
xset -dpms
xset s off

$APP -p $CONF

A=$?
echo $A > "/tmp/gui.retval"
sleep 1

if [ "$A" == "95" ] ; then sudo poweroff; sleep 1000000000; fi
#if [ "$A" == "95" ] ; then sudo halt -h ; fi

# se epgui muore lancia xterm per tappullare
xterm

