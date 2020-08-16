#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# ********************************************************
# @file: server.py
# @author: Lin, Chao <chaochaox.lin@intel.com>
# @create time: 2019-03-21 09:09:09
# @last modified: 2019-03-21 09:09:09
# @description:
# ********************************************************

from socketserver import BaseRequestHandler, TCPServer
import pdb

#pdb.set_trace()

piccmd = 'FA FA 57 7A 00 00 00 05 00 E0 E0 11 00 00 01 06 C7 8A A9 00 30 31 32 33 34 35 36 37 38 39 3A E1 E1 E0 E0 11 00 00 02 07 8E 15 53 01 30 31 32 33 34 35 36 37 38 39 3A E1 E1 E0 E0 11 00 00 03 08 55 A0 FC 01 30 31 32 33 34 35 36 37 38 39 3A E1 E1 E0 E0 11 00 00 04 09 1C 2B A6 02 30 31 32 33 34 35 36 37 38 39 3A E1 E1 E0 E0 11 00 00 05 01 E3 B5 4F 03 30 31 32 33 34 35 36 37 38 39 3A E1 E1 FB FB'

class EchoHandler(BaseRequestHandler):
    def handle(self):
        print('Got connection from', self.client_address)
        msg = self.request.recv(8192)
        if not msg:
           return 
        msg = bytearray(msg)
        print(msg)
        cmd = bytearray.fromhex(piccmd)
        self.request.send(cmd)

if __name__ == '__main__':
    serv = TCPServer(('', 20002), EchoHandler)
    serv.serve_forever()
