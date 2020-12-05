1. 将可执行程序，脚步，数据库放在一个目录

cmdproxy
script/daemon_example.sh

例如：/home/root/#ls
cmdproxy
baozi.db #数据库可以自动生成
daemon_example.sh

2. /etc/init.d/qt.sh 为开机启动脚本，内容如下：
# cat /etc/init.d/qt.sh
#!/bin/sh

export KEYPAD_DEV="/dev/input/keypad"
/home/root/daemon_example.sh &

3.daemon_example.sh  文件需要改的地方有如下部分：

PRO_NAME=cmdproxy                 --可以改为需要的名字，不需要路径部分
LOGFILE=/var/log/messages					--log文件路径，与/etc/syslog-startup.conf 中LOGFILE=/var/log/messages 保持一致即可
CFG=baozi.db                      --数据库名，不需要路径部分，会自动生成

