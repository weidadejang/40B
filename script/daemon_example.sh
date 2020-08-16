#!/bin/bash
# ********************************************************
# @file: daemon_example.sh
# @create time: 2018-11-20 09:20:27
# @last modified: 2018-11-20 09:20:27
# @description:
# ********************************************************

# if [ $# != 1 ] ; then
#     echo "USAGE: $0 PROCESS_NAME"
#     echo " e.g.: $0 serial2http"
#     exit 1;
# fi

PRO_NAME=cmdproxy
LOGFILE=/tmp/messages
CFG=baozi.db
basepath=$(cd `dirname $0`; pwd)

echo $basepath
error_cnt=0
OLD_ACM0=""
OLD_ACM1=""
OLD_ACM2=""
OLD_ACM3=""
OLD_NET=""
OLD_Main=""
runtime=0
while true ; do
  #用ps获取$PRO_NAME进程数量
  #pid=`pidof $PRO_NAME`
  NUM=`ps | grep -w ${PRO_NAME} | grep -v grep |wc -l`
  #少于1，重启进程
  if [ ${NUM} -lt 1 ];then
    echo "${PRO_NAME} was killed"
    $basepath/$PRO_NAME $basepath/$CFG &
  #大于1，杀掉所有进程，重启
  elif [ ${NUM} -gt 1 ];then
    echo "more than 1 ${PRO_NAME},killall ${PRO_NAME}"
    killall -9 $PRO_NAME
    $basepath/$PRO_NAME $basepath/$CFG &
  fi
  #kill僵尸进程
  NUM_STAT=`ps | grep -w ${PRO_NAME} | grep T | grep -v grep | wc -l`
  if [ ${NUM_STAT} -gt 0 ];then
    killall -9 ${PRO_NAME}
    $basepath/$PRO_NAME $basepath/$CFG &
  fi

  sleep 3m

  ACM0=$(tail -n 200 $LOGFILE|egrep "baozi.*ttyACM0.*TX"|tail -n 1)
  ACM1=$(tail -n 200 $LOGFILE|egrep "baozi.*ttyACM1.*TX"|tail -n 1)
  ACM2=$(tail -n 200 $LOGFILE|egrep "baozi.*ttyACM2.*TX"|tail -n 1)
  ACM3=$(tail -n 200 $LOGFILE|egrep "baozi.*ttyACM3.*TX"|tail -n 1)
  Net=$(tail -n 1000 $LOGFILE|egrep "baozi.*heartThread.*TX"|tail -n 1)
  Main=$(tail -n 1000 $LOGFILE|egrep "baozi.*Main.*sync"|tail -n 1)

  if [ "${ACM0}" == "" -a "${ACM1}" == "" -a "${ACM2}" == "" -a "${ACM3}" == "" -a "${Net}" == "" ]; then
    error_cnt=$[error_cnt + 1]
  elif [ "${ACM0}" == "${OLD_ACM0}" -a "${ACM1}" == "${OLD_ACM1}" -a "${ACM2}" == "${OLD_ACM2}" -a "${ACM3}" == "${OLD_ACM3}" -a "${Net}" == "${OLD_NET}" ]; then
    error_cnt=$[error_cnt + 1]
  else
    error_cnt=0
  fi

  OLD_ACM0=$ACM0
  OLD_ACM1=$ACM1
  OLD_ACM2=$ACM2
  OLD_ACM3=$ACM3
  OLD_NET=$Net
  OLD_Main=$Main

  echo "error_cnt : $error_cnt"
  # 1个小时都无法正常
  if [ $error_cnt -gt 12 ]; then
    echo "system will reboot!!!"
    sleep 30s
    reboot
  elif [ $error_cnt -gt 3 ]; then
    echo "will restart ${PRO_NAME}!!!"
    killall -9 $PRO_NAME
  fi

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


