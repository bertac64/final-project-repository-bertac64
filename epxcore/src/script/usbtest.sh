#!/bin/bash
#
#	Stress test per inizializzazione Ceusb/hub
#	Esege $NRETRY cicli di spegnimento/riaccensione della 
#	porta USB 2: se il device Ceusb non viene riconosciuto, 
#	si interrompe con error
#
#	(C)	2011 Igea SpA
#

NRETRY=100
USBhubVendor="0451"
USBhubProdID="2046"
USBCesysVendor="10f8"
USBCesysProdID0="c301"
USBCesysProdID1="c381"
USBport="usb2"

DEVICE=/proc/bus/usb/devices
LOGFILE=${1:-/tmp}/usbrecovery.err

echo "# usbrecovery.sh " `date` | tee $LOGFILE

RETRY=1
BAD=0
while :
do
	echo "$RETRY: Resetting $USBport" 

	# Ciclo spegnimento/riaccensione
	echo -n $USBport > /sys/bus/usb/drivers/usb/unbind
	sleep 0.5
	echo -n $USBport > /sys/bus/usb/drivers/usb/bind
	sleep 0.8

	# Controlla se c'e` il device Ceusb
	answer=$(awk '
			BEGIN			{found = 0}
			/Vendor='"$USBCesysVendor"' ProdID='"$USBCesysProdID0"'/ {found = 1}
			/Vendor='"$USBCesysVendor"' ProdID='"$USBCesysProdID1"'/ {found = 1}
			END 			{print found}
		' $DEVICE)

	# Logga il risultato
	# echo $answer
	if [ $answer == "0" ] ; then
		echo "$RETRY: Cesys driver not found!" | tee -a $LOGFILE
		grep -q $USBhubVendor $DEVICE || echo "Hub found"
		BAD=$(( $BAD + 1 ))
		
#	else
#		echo "$RETRY: Cesys driver is Ok" | tee -a $LOGFILE
	fi

	# se abbiamo superato i retries, interrompi
	[ $RETRY -eq $NRETRY ] && break

	# Il driver Cesys non c'e`: prova a resettare la porta USB2
#	answer=$(awk '
#		BEGIN	{found=0}
#		/Bus=/ 												{bus=$0}
#		/Vendor='"$USBhubVendor"' ProdID='"$USBhubProdID"'/ {found=1}
#		/^C:.*\#Ifs=/	{if (found) {
#							on = 0;
#							if ($1 ~ /\*/) on=1;
#					 		printf("on=%d %s\n", on, bus);
#					 		found = 0;
#					 	 }
#						}
#	'  $DEVICE)
#
#	echo $answer | grep -q '^on=1' && echo "Hub is on" >> $LOGFILE 
#
#
#	# Altrimenti, parsa la stringa per trovare i campi che ti servono
#	bus_no=$( echo $answer | awk '{print $3}' | awk -F= '{print $2}' )
#	bus_no=$(( $bus_no + 0 ))
#	dev_no=$( echo $answer | awk '{print $9}' )
#	bus_rank=$( echo $answer | awk '{print $6}' | awk -F= '{print $2}' )
#	bus_rank=$(( $bus_rank + 1 ))
#	#echo "Bus number: $bus_no"
#	#echo "Device number: $dev_no"
#	#echo "Bus rank: $bus_rank"
#
#	echo "Resetting hub $bus_no-$bus_rank" >> $LOGFILE 
#
#	echo -n "$bus_no-$bus_rank" > /sys/bus/usb/drivers/usb/unbind
#	sleep 5
#	echo -n "$bus_no-$bus_rank" > /sys/bus/usb/drivers/usb/bind
#	sleep 5

	# altro giro
	RETRY=$(( $RETRY + 1 ))
done

# Fine cicli
echo "$RETRY iterations: $BAD issues found" | tee -a $LOGFILE
exit 0
