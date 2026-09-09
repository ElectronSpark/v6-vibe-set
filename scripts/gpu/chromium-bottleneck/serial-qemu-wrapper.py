#!/usr/bin/env python3
import os,sys,json
args=sys.argv[1:]
positions=[i for i,arg in enumerate(args) if arg=='-serial']
if positions:
    if len(positions)!=1 or args[positions[0]+1]!='mon:stdio':raise SystemExit('unexpected serial arguments')
    before=args.copy()
    args[positions[0]+1]='unix:'+os.environ['QEMU_OPENGL_SERIAL_PATH']+',server=on,wait=off'
    with open(os.environ['QEMU_OPENGL_WRAPPED_ARGS'],'w') as f:json.dump({'original':before,'serial_socket_override':args},f,indent=2)
os.execv('/usr/bin/qemu-system-x86_64',['/usr/bin/qemu-system-x86_64',*args])
