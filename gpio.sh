#!/bin/bash


#echo 121 > /sys/class/gpio/export
#echo out > /sys/class/gpio/gpio121/direction
#echo 1 > /sys/class/gpio/gpio121/value
#echo 0 > /sys/class/gpio/gpio121/value

echo 137 > /sys/class/gpio/export
echo 138 > /sys/class/gpio/export
echo 139 > /sys/class/gpio/export
echo 140 > /sys/class/gpio/export
echo 141 > /sys/class/gpio/export
echo 142 > /sys/class/gpio/export
echo 143 > /sys/class/gpio/export
echo 144 > /sys/class/gpio/export
echo 145 > /sys/class/gpio/export

sleep 1

echo in > /sys/class/gpio/gpio137/direction
echo in > /sys/class/gpio/gpio138/direction
echo in > /sys/class/gpio/gpio139/direction
echo in > /sys/class/gpio/gpio140/direction
echo in > /sys/class/gpio/gpio141/direction
echo in > /sys/class/gpio/gpio142/direction
echo in > /sys/class/gpio/gpio143/direction
echo in > /sys/class/gpio/gpio144/direction
echo in > /sys/class/gpio/gpio145/direction


for((i=1;i<100;i++))
do

cat /sys/class/gpio/gpio137/value
cat /sys/class/gpio/gpio138/value
cat /sys/class/gpio/gpio139/value
cat /sys/class/gpio/gpio140/value
cat /sys/class/gpio/gpio141/value
cat /sys/class/gpio/gpio142/value
cat /sys/class/gpio/gpio143/value
cat /sys/class/gpio/gpio144/value
cat /sys/class/gpio/gpio145/value

echo "---------------------------->"

sleep 10
done
