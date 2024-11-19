#! /bin/bash

i=1
while :
do
	i=$(( $i + 1 ))
	echo $i

	sleep 1
	
	[ $i -ge 10 ] && break
done
