#!/bin/bash
#
#	Test di avvio del transceiver USB in fibra ottica
#	Se il transceiver non e' correttamente inizializzato,
#	lo script disabilita e riabilita il dispositivo
#	fino a un massimo di $NRETRY volte.
#	Esce con 0 se il dispositivo e` correttamente inizializzato
#	altrimenti esce con 1
#
#	(C)	2011 Igea SpA
#

NRETRY=3
USBhubVendor="0451"
USBhubProdID="2046"
USBCesysVendor="10f8"
USBCesysProdID="c381"

DEVICE=/proc/bus/usb/devices
LOGFILE=${1:-/tmp}/usbrecovery.err

echo "# usbrecovery.sh " `date` > $LOGFILE

while :
do
	answer=$(awk '
			BEGIN			{found = 0}
			/Vendor='"$USBCesysVendor"' ProdID='"$USBCesysProdID"'/ {found = 1}
			END 			{print found}
		' $DEVICE)

	# Se la risposta e` 1, esci con tutto bene
	# echo $answer
	if [ $answer == "1" ] ; then
		echo "Cesys driver is Ok ($NRETRY to go)" >> $LOGFILE
		exit 0
	fi

	# se abbiamo superato i retries, interrompi
	[ $NRETRY -eq 0 ] && break

	# Il driver Cesys non c'e`: prova a resettare l'hub
	answer=$(awk '
		BEGIN	{found=0}
		/Bus=/ 												{bus=$0}
		/Vendor='"$USBhubVendor"' ProdID='"$USBhubProdID"'/ {found=1}
		/^C:.*\#Ifs=/	{if (found) {
							on = 0;
							if ($1 ~ /\*/) on=1;
					 		printf("on=%d %s\n", on, bus);
					 		found = 0;
					 	 }
						}
	'  $DEVICE)

	echo $answer | grep -q '^on=1' && echo "Hub is on" >> $LOGFILE 


	# Altrimenti, parsa la stringa per trovare i campi che ti servono
	bus_no=$( echo $answer | awk '{print $3}' | awk -F= '{print $2}' )
	bus_no=$(( $bus_no + 0 ))
	dev_no=$( echo $answer | awk '{print $9}' )
	bus_rank=$( echo $answer | awk '{print $6}' | awk -F= '{print $2}' )
	bus_rank=$(( $bus_rank + 1 ))
	#echo "Bus number: $bus_no"
	#echo "Device number: $dev_no"
	#echo "Bus rank: $bus_rank"

	echo "Resetting hub $bus_no-$bus_rank" >> $LOGFILE 

	echo -n "$bus_no-$bus_rank" > /sys/bus/usb/drivers/usb/unbind
	sleep 10
	echo -n "$bus_no-$bus_rank" > /sys/bus/usb/drivers/usb/bind
	sleep 5

	# altro giro
	NRETRY=$(( $NRETRY - 1 ))
done

# se sei arrivato qui, la reinizializzazione non e` riuscita
echo "Cesys driver init failed" >> $LOGFILE
exit 1
