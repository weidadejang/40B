#!/bin/bash

export KEYPAD_DEV="/dev/input/keypad"
/root/cmdproxy /root/baozi.db &
/root/daemon_example.sh &