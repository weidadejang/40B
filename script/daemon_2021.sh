#!/bin/bash

if [ $# != 1 ] ; then
    echo "USAGE: $0 PROCESS_NAME"
    echo " e.g.: $0 cmdproxy"
    exit 1;
fi

PRO_NAME=cmdproxy
LOGFILE=/tmp/messages
CFG=baozi.db
basepath=$(cd `dirname $0`; pwd)

echo $basepath

runtime=0
while true ; do
  #用ps获取$PRO_NAME进程数量
  #pid=`pidof $PRO_NAME`
  NUM=`ps | grep -w ${PRO_NAME} | grep -v grep |wc -l`
  #少于1，重启进程
  if [ ${NUM} -lt 1 ];then
    echo "${PRO_NAME} was killed"
    $basepath/$PRO_NAME $basepath/$CFG daemon
  #大于1，杀掉所有进程，重启
  elif [ ${NUM} -gt 1 ];then
    echo "more than 1 ${PRO_NAME},killall ${PRO_NAME}"
    killall -9 $PRO_NAME
    $basepath/$PRO_NAME $basepath/$CFG daemon
  fi
  #kill僵尸进程
  NUM_STAT=`ps | grep -w ${PRO_NAME} | grep T | grep -v grep | wc -l`
  if [ ${NUM_STAT} -gt 0 ];then
    killall -9 ${PRO_NAME}
    $basepath/$PRO_NAME $basepath/$CFG daemon
  fi

  sleep 3m

  runtime=$[runtime + 1]
  echo "runtime ${runtime}"
  if [ $runtime -gt 480 ]; then
    killall -2 $PRO_NAME
    echo "--------system will reboot----------"
    sleep 60s
    reboot
  fi
done

exit 0


