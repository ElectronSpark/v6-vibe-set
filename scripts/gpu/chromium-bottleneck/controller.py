#!/usr/bin/env python3
import os,sys,json,time,socket,subprocess,signal,select,hashlib,shutil,threading,re,importlib.util,argparse,shlex
from pathlib import Path
from datetime import datetime,timezone
from http.server import HTTPServer,BaseHTTPRequestHandler
from PIL import Image
parser=argparse.ArgumentParser(description='Prepared local-media A/B; no VM without --run-vm')
parser.add_argument('--run-vm',action='store_true')
configuration=parser.add_mutually_exclusive_group()
configuration.add_argument('--diagnostic-config',type=Path,help='Explicit staged diagnostic image/runtime manifest; validates concrete inputs even without --run-vm')
configuration.add_argument('--kernel-variant',type=Path,help='Absolute sealed kernel-only variant receipt; preserves the frozen browser/rootfs policy')
parser.add_argument('--diagnostic-smoke',action='store_true',help='One unchanged 15-second traced on-1 for emission validation only; requires --diagnostic-config')
args=parser.parse_args()
if args.diagnostic_smoke and args.kernel_variant is not None:parser.error('--diagnostic-smoke cannot be combined with --kernel-variant')
if args.diagnostic_smoke and args.diagnostic_config is None:parser.error('--diagnostic-smoke requires --diagnostic-config')
CAPTURE_PURPOSE='emission-smoke' if args.diagnostic_smoke else 'comparison'
if not args.run_vm and args.diagnostic_config is None and args.kernel_variant is None:
 print(json.dumps({'mode':'NO-BOOT preparation','order':['off-1','on-1','on-2','off-2'],'measurement_ms':15000,'phase_watchdog_s':35,'post_playback_child_wait_s':90,'exit_watch_budget_s':95,'ready_budget_s':720,'requires':'explicit root VM authorization, final fixture SHA and zero-QEMU preflight'}));sys.exit(0)
ROOT=Path('/home/es/xv6-os')
HELPERS=Path(__file__).resolve().parent
import capture_config
import capture_cleanup
import kernel_variant
PROTECTED_BASE=capture_config.PROTECTED_BASE
RETAINED=ROOT/'build-x86_64/gui-progress-audit/chromium-bottleneck-trace-20260908T020910Z'
KERNEL_VARIANT=kernel_variant.load_receipt(args.kernel_variant) if args.kernel_variant else None
if KERNEL_VARIANT:kernel_variant.check_environment(KERNEL_VARIANT,os.environ)
TOKEN=('chromium-kernel-'+KERNEL_VARIANT['identity']['variant']+'-capture-' if KERNEL_VARIANT else 'chromium-diagnostic-capture-' if args.diagnostic_config else 'chromium-ack-capture-')+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
DIAGNOSTIC=capture_config.load_diagnostic(args.diagnostic_config,TOKEN,capture_purpose=CAPTURE_PURPOSE) if args.diagnostic_config else None
SELECTED_TRIALS=capture_config.owner.capture_trials(CAPTURE_PURPOSE)
CAPTURE_POLICY=DIAGNOSTIC['policy'] if DIAGNOSTIC else None
CAPTURE_IDENTITY=capture_config.owner.capture_identity(CAPTURE_POLICY)
if not args.run_vm:
 print(json.dumps({'mode':'NO-BOOT validated kernel variant preparation' if KERNEL_VARIANT else 'NO-BOOT validated diagnostic preparation','capture_identity':CAPTURE_IDENTITY,'controls':capture_config.owner.capture_controls(CAPTURE_PURPOSE),'preflight':KERNEL_VARIANT['preflight'] if KERNEL_VARIANT else DIAGNOSTIC['receipt']}));sys.exit(0)
OUT=ROOT/'build-x86_64/gui-progress-audit'/TOKEN
capture_cleanup.create_receipt(OUT)
KERNEL_DEFAULT=ROOT/'build-x86_64/gui-progress-audit/virgl-wait-progress-candidate-build-20260907T225130Z/xv6.bin'
KERNEL=KERNEL_VARIANT['kernel'] if KERNEL_VARIANT else Path(os.environ.get('GL_AUDIT_KERNEL',str(KERNEL_DEFAULT)))
KERNEL_EXPECTED_SHA=KERNEL_VARIANT['identity']['kernel_sha256'] if KERNEL_VARIANT else os.environ.get('GL_AUDIT_KERNEL_SHA256','')
BASE=DIAGNOSTIC['image'] if DIAGNOSTIC else PROTECTED_BASE
FIXTURE=ROOT/'scripts/gpu/fixtures/chromium-video-drops.html'
MEDIA=ROOT/'build-x86_64/gui-progress-audit/chromium-video-drop-inputs-20260908T004059Z/perf-1280x800-60fps.mp4'
MEDIA_SHA='e79cc0e2fbd02b9928fbf5d3c579566c6db7bb07da5ac923ebca7422599289df'
FIXTURE_SHA=os.environ.get('MEDIA_AUDIT_FIXTURE_SHA256','')
SERVER_SOURCE=HELPERS/'http-server.py'
OWNER_SOURCE=HELPERS/'owner-helper.py'
QMP=Path('/tmp/'+TOKEN+'.qmp')
SERIAL=Path('/tmp/'+TOKEN+'.serial')
serial_sock=None;serial_thread=None;serial_stop=threading.Event()
PID=OUT/'qemu.pid'; OVERLAY=OUT/'session.qcow2'
TITLE='QEMU (xv6-'+TOKEN+'-0)'
ENV=os.environ.copy()
ENV.update(QEMU_SDL_APT_BIN=str(HELPERS/'serial-qemu-wrapper.py'),QEMU_OPENGL_SERIAL_PATH=str(SERIAL),QEMU_OPENGL_WRAPPED_ARGS=str(OUT/'serial-wrapper-arguments.json'),KERNEL=str(KERNEL),FSIMG=str(BASE),BUILD_DIR=str(ROOT/'build-x86_64'),AUTO_BUILD='0',DISPLAY_MODE='sdl',QEMU_GPU='virtio-vga-gl-primary',QEMU_CPUS='6',QEMU_MEMORY='8G',QEMU_INPUT='virtio',QEMU_SDL_VARIANT='apt',QEMU_SDL_APT_MODULES='corrected',QEMU_ALLOW_WSL_SDL_GL='1',QEMU_RUN_TOKEN=TOKEN,QEMU_PIDFILE=str(PID),QEMU_OVERLAY=str(OVERLAY),QEMU_NATURAL_ZERO_TIMEOUT='0',QEMU_DISPLAY_REPORT='1',QEMU_EXTRA=f'-qmp unix:{QMP},server=on,wait=off',QEMU_WSL_SDL_FIT_WATCH_SECONDS='0',QEMU_AUDIO_WAV_PATH=str(OUT/'audio.wav'),QEMU_APPEND='root=/dev/disk0 netsurf=0 webkit=0 '+os.environ.get('GL_AUDIT_APPEND_EXTRA',''))
# Keep all interactions tied to this VM. QEMU/launcher are always reaped in finally.
launcher=None; log=None; owned_pid=None; owned_start=None; qsock=None; qfile=None; seq=0; host_cpu_map=[]
transcript=(OUT/'actions.jsonl').open('a',buffering=1)
def emit(value):
 value={'utc':datetime.now(timezone.utc).isoformat(),'host_monotonic_ns':time.monotonic_ns(),**value}
 transcript.write(json.dumps(value)+'\n')
 notice={k:v for k,v in value.items() if k not in ('provenance','output_tail','browser_report','host_details','fit')}
 if 'browser_report' in value:
  payload=value['browser_report'].get('payload',{})
  notice['browser_summary']={k:payload[k] for k in ('trial','phase','legacy','diagnostic_valid','invalid') if k in payload}
 print(json.dumps(notice),flush=True)
def inventory():
 found=[]
 for p in Path('/proc').iterdir():
  if p.name.isdigit():
   try:
    exe=(p/'exe').resolve(strict=True).name
    if exe.startswith('qemu-system-') or exe=='qemu-kvm':found.append(int(p.name))
   except (FileNotFoundError,PermissionError,ProcessLookupError):pass
 return found
def run(args,timeout=15,env=None):
 return subprocess.run(args,cwd=ROOT,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
def validate():
 if owned_pid is None:return False
 r=run(['bash',str(ROOT/'scripts/launch/cleanup-owned-qemu.sh'),'--check-only',str(owned_pid),str(owned_start),TOKEN])
 return r.returncode==0
def serial_completion_ready(prefix,response,marker):
 token=marker.encode()
 echo_end=prefix.find(b'\n')
 if echo_end<0:return False
 clean=re.sub(rb'\x1b\[[0-9;?]*[A-Za-z]',b'',response)
 if clean.count(token)!=1:return False
 after=clean.split(token,1)[1]
 return re.search(rb'root:[^\r\n]*[#\$] ',after) is not None
def pump_serial():
 with (OUT/'serial.log').open('ab',buffering=0) as f:
  while not serial_stop.is_set():
   try:
    data=serial_sock.recv(65536)
    if not data:break
    offset=f.tell();f.write(data)
    with (OUT/'serial-chunks.jsonl').open('a') as chunks:
     chunks.write(json.dumps({'host_monotonic_ns':time.monotonic_ns(),'offset':offset,'bytes':len(data)})+'\n')
   except socket.timeout:continue
   except OSError:break
def qmp(name,args=None):
 global seq
 seq+=1
 qsock.sendall((json.dumps({'execute':name,'arguments':args or {},'id':seq})+'\n').encode())
 while True:
  line=qfile.readline()
  if not line:raise RuntimeError('QMP EOF')
  obj=json.loads(line)
  if obj.get('id')==seq:
   if 'error' in obj:raise RuntimeError(json.dumps(obj['error']))
   return obj.get('return')
def key(keys):
 qmp('send-key',{'keys':[{'type':'qcode','data':k} for k in keys.split('-')],'hold-time':60})
 time.sleep(.09)
def type_text(text):
 punctuation={' ':('spc',False),'/':('slash',False),'.':('dot',False),'-':('minus',False),'_':('minus',True),':':('semicolon',True),';':('semicolon',False),'=':('equal',False),'+':('equal',True),'\'':('apostrophe',False),'"':('apostrophe',True),'|':('backslash',True),'>':('dot',True),'<':('comma',True),'(':('9',True),')':('0',True),'!':('1',True),'?':('slash',True),'&':('7',True),'$':('4',True),'\\':('backslash',False),',':('comma',False),'[':('bracket_left',False),']':('bracket_right',False),'*':('8',True),'%':('5',True),'#':('3',True),'@':('2',True),'`':('grave_accent',False)}
 for c in text:
  if c=='\n':key('ret');continue
  if c.isascii() and c.isalnum():code=c.lower();shift=c.isupper()
  else:code,shift=punctuation[c]
  key(('shift-' if shift else '')+code)
def mouse(x,y,button=None):
 qmp('input-send-event',{'events':[{'type':'abs','data':{'axis':'x','value':round(x*32767/1279)}},{'type':'abs','data':{'axis':'y','value':round(y*32767/799)}}]})
 if button:
  qmp('input-send-event',{'events':[{'type':'btn','data':{'down':True,'button':button}}]});time.sleep(.12)
  qmp('input-send-event',{'events':[{'type':'btn','data':{'down':False,'button':button}}]})
def snapshot(label,host=True):
 if not label.replace('-','').replace('_','').isalnum():raise ValueError('unsafe label')
 data={'snapshot':label,'status':qmp('query-status')}
 ppm=OUT/(label+'-guest.ppm');png=OUT/(label+'-guest.png')
 try:
  qmp('screendump',{'filename':str(ppm)})
  with Image.open(ppm) as im:
   data['guest_size']=im.size;im.save(png)
  ppm.unlink();data['guest']=str(png)
 except Exception as e:data['guest_error']=str(e)
 if host:
  path=OUT/(label+'-host.png');env=ENV.copy();env['QEMU_WINDOW_TITLE']=TITLE
  r=run(['bash',str(ROOT/'scripts/gpu/capture-host-screen.sh'),str(path)],timeout=20,env=env)
  (OUT/(label+'-host-capture.txt')).write_text(r.stdout)
  data['host_capture_rc']=r.returncode;data['host_details']=r.stdout.strip()
  if path.exists():data['host']=str(path)
 emit(data)
 return data


class SerialDeadline(RuntimeError):
 def __init__(self,label,receipt):
  super().__init__('serial completion/prompt deadline: '+label)
  self.receipt=receipt


def serial_command(text,label,timeout=35):
 marker='MEDIA_DONE_'+str(seq)+'_'+str(int(time.monotonic()))
 command=" m=MEDIA_DONE_; m=$m'"+marker[len('MEDIA_DONE_'):] +"'; "+text+'; echo; echo '+chr(34)+'$m'+chr(34)+'\n'
 assert marker not in command
 if len(command)>250:raise ValueError('serial command too long: '+str(len(command)))
 offset=(OUT/'serial.log').stat().st_size
 owner_record_cursor=len(http.records) if http else 0
 started=time.monotonic()
 emit({'serial_action':{'label':label,'text':text,'timeout_seconds':timeout}})
 serial_sock.sendall(command.encode())
 deadline=min(time.monotonic()+timeout,ready_deadline) if ready_deadline else time.monotonic()+timeout
 captured=b''
 while time.monotonic()<deadline:
  with (OUT/'serial.log').open('rb') as stream:
   stream.seek(offset);prefix=stream.read(4096);echo_end=prefix.find(b'\n')
   if echo_end>=0:stream.seek(max(offset+echo_end+1,(OUT/'serial.log').stat().st_size-16384))
   else:stream.seek(offset)
   captured=stream.read(16384)
  if serial_completion_ready(prefix,captured,marker):break
  if launcher.poll() is not None:raise RuntimeError('VM ended during serial')
  time.sleep(.1)
 else:
  end=(OUT/'serial.log').stat().st_size
  with (OUT/'serial.log').open('rb') as stream:
   stream.seek(offset);window=stream.read(min(end-offset,1024*1024))
  path=OUT/(label+'-deadline.serial.txt');path.write_bytes(window)
  receipt={'label':label,'marker':marker,'offset':offset,'end_offset':end,
           'elapsed_seconds':time.monotonic()-started,'requested_timeout_seconds':timeout,
           'captured_bytes':len(window),'available_bytes':end-offset,'truncated':end-offset>len(window),
           'capture_sha256':hashlib.sha256(window).hexdigest(),'path':str(path),
           'completion_and_fresh_prompt':False,'serial_silence_is_not_exit_evidence':True}
  (OUT/(label+'-deadline.json')).write_text(json.dumps(receipt,indent=2)+'\n')
  emit({'phase':'serial-deadline','receipt':receipt})
  raise SerialDeadline(label,receipt)
 time.sleep(.2)
 with (OUT/'serial.log').open('rb') as stream:stream.seek(offset);captured=stream.read(1024*1024)
 (OUT/(label+'.txt')).write_bytes(captured)
 emit({'serial_completed':marker,'label':label,'bytes':len(captured),'output_tail':captured[-8192:].decode(errors='replace')})
 owner_receipts=[]
 if http is not None:
  with http.lock:
   owner_receipts=[record['payload']['owner'] for record in http.records[owner_record_cursor:]
                   if record.get('payload',{}).get('phase')=='owner-result']
 if owner_receipts:
  (OUT/(label+'-owner-receipts.json')).write_text(json.dumps(owner_receipts,indent=2)+'\n')
 return captured.decode(errors='replace')+'\n'+'\n'.join('MEDIA_OWNER_JSON '+json.dumps(row) for row in owner_receipts)

def owner_rows(text):
 rows=[]
 for part in text.split('MEDIA_OWNER_JSON ')[1:]:
  try:row,_=json.JSONDecoder().raw_decode(part);rows.append(row)
  except ValueError:continue
 return rows

def exit_watch_receipts(trial,owner,prefix):
 with http.lock:
  rows=[record for record in http.records
        if record.get('payload',{}).get('phase')=='owner-result'
        and record['payload'].get('owner',{}).get('phase')=='exit-watch'
        and record['payload']['owner'].get('trial')==trial
        and record['payload']['owner'].get('pid')==owner['pid']
        and record['payload']['owner'].get('start_ticks')==owner['start_ticks']
        and record['payload']['owner'].get('capture_identity')==CAPTURE_IDENTITY]
 (OUT/(prefix+'-exit-watch.json')).write_text(json.dumps(rows,indent=2)+'\n')
 return rows


def exit_diagnosis(rows,direct_values=None):
 terminals=[record['payload']['owner'] for record in rows
            if record['payload']['owner'].get('event')=='terminal']
 disappeared=any(row.get('process_disappearance_observed') is True
                 and row.get('process',{}).get('identity')=='absent' for row in terminals)
 if direct_values==['0']:classification='direct-child-exit-zero'
 elif direct_values:classification='direct-child-exit-invalid-or-nonzero'
 elif disappeared:classification='process-disappeared-child-completion-unconfirmed'
 else:classification='child-completion-unconfirmed'
 return {'classification':classification,'process_disappearance_observed':disappeared,
         'direct_child_exit_values':direct_values,'exit_code_inferred_from_process_state':False,
         'hang_proven':False,'transport_failure_proven':False,
         'watcher_terminal_records':len(terminals)}


def recover_serial_prompt(label):
 # Only interrupt after the child-wait deadline; require a new prompt before
 # another command. The interrupt itself proves no browser exit status.
 offset=(OUT/'serial.log').stat().st_size
 emit({'phase':'post-exit-deadline-serial-interrupt','label':label})
 serial_sock.sendall(b'\x03')
 deadline=min(time.monotonic()+5,ready_deadline)
 captured=b'';fresh=False
 while time.monotonic()<deadline:
  with (OUT/'serial.log').open('rb') as stream:
   stream.seek(max(offset,(OUT/'serial.log').stat().st_size-16384));captured=stream.read(16384)
  clean=re.sub(rb'\x1b\[[0-9;?]*[A-Za-z]',b'',captured)
  if re.search(rb'root:[^\r\n]*[#\$] ',clean):fresh=True;break
  if launcher.poll() is not None:break
  time.sleep(.1)
 (OUT/(label+'-interrupt.serial.txt')).write_bytes(captured)
 emit({'phase':'post-exit-deadline-prompt','label':label,'fresh_prompt':fresh,
       'capture_bytes':len(captured)})
 return fresh


def close_browser(owner,trial,prefix,origin,traced):
 identity=str(owner['pid'])+' '+owner['start_ticks']+' '+trial
 serial_command('/usr/bin/python3 /tmp/mo.py exit-watch '+identity+
                ' >/tmp/'+trial+'.exit-watch 2>&1 & wpid=$!',prefix+'-exit-watch-launch')
 def watcher_ready(record):
  row=record.get('payload',{}).get('owner',{})
  return (record.get('payload',{}).get('phase')=='owner-result' and
          row.get('phase')=='exit-watch' and row.get('event')=='ready' and
          row.get('trial')==trial and row.get('pid')==owner['pid'] and
          row.get('start_ticks')==owner['start_ticks'] and
          row.get('capture_identity')==CAPTURE_IDENTITY)
 ready=http.wait_record(watcher_ready,min(10,max(0,ready_deadline-time.monotonic())))
 exit_watch_receipts(trial,owner,prefix)
 if ready['payload']['owner'].get('process',{}).get('identity')!='owned':
  raise RuntimeError('exit watcher exact initial identity failed '+trial)
 emit({'phase':'post-playback-close','trial':trial,'child_wait_seconds':90,
       'exit_watch_ready':ready,'measurement_already_complete':True})
 key('ctrl-shift-w')
 values=None
 try:
  exited=serial_command('wait $cpid; r=$?; echo MEDIA_CHILD_EXIT=$r',prefix+'-direct-exit',timeout=90)
  values=re.findall(r'MEDIA_CHILD_EXIT=(\d+)',exited)
 except SerialDeadline as error:
  diagnosis={'serial_deadline':error.receipt,'recovery':{}}
  try:diagnosis['deadline_vm_snapshot']=snapshot(prefix+'-exit-deadline')
  except Exception as caught:diagnosis['deadline_snapshot_error']=str(caught)
  try:fresh=recover_serial_prompt(prefix+'-exit-deadline')
  except Exception as caught:
   fresh=False;diagnosis['recovery']['prompt_error']=str(caught)
  diagnosis['recovery']['fresh_prompt']=fresh
  if fresh and ready_deadline-time.monotonic()>12:
   try:
    state=serial_command('/usr/bin/python3 /tmp/mo.py exit-state '+identity,
                         prefix+'-exit-deadline-state',timeout=12)
    diagnosis['recovery']['owner_state']=owner_rows(state)
    watched=serial_command('wait $wpid; r=$?; echo MEDIA_WATCH_EXIT=$r',
                           prefix+'-exit-watch-reap',timeout=10)
    diagnosis['recovery']['watcher_exit_values']=re.findall(r'MEDIA_WATCH_EXIT=(\d+)',watched)
   except Exception as caught:diagnosis['recovery']['error']=str(caught)
  rows=exit_watch_receipts(trial,owner,prefix)
  diagnosis.update(exit_diagnosis(rows))
  # Export only after observed disappearance and a recovered, freshly checked
  # prompt. Missing direct-child exit status still makes this capture fail.
  if (fresh and diagnosis['process_disappearance_observed'] and
      'error' not in diagnosis['recovery'] and ready_deadline-time.monotonic()>85):
   try:
    log_result=serial_command('/usr/bin/python3 /tmp/mo.py log-end '+trial+' '+origin,
                              prefix+'-exit-recovery-log-upload')
    diagnosis['recovery']['log_export']=owner_rows(log_result)
    if traced:
     trace_result=serial_command('/usr/bin/python3 /tmp/mo.py trace-end '+trial+' '+origin,
                                 prefix+'-exit-recovery-trace-upload',timeout=60)
     diagnosis['recovery']['trace_export']=owner_rows(trace_result)
   except Exception as caught:diagnosis['recovery']['export_error']=str(caught)
  (OUT/(prefix+'-exit-diagnosis.json')).write_text(json.dumps(diagnosis,indent=2)+'\n')
  emit({'phase':'post-playback-exit-diagnosis','trial':trial,**exit_diagnosis(rows)})
  raise
 try:watched=serial_command('wait $wpid; r=$?; echo MEDIA_WATCH_EXIT=$r',prefix+'-exit-watch-reap',timeout=10)
 except Exception as error:
  rows=exit_watch_receipts(trial,owner,prefix)
  diagnosis={**exit_diagnosis(rows,values),'watcher_reap_error':str(error)}
  (OUT/(prefix+'-exit-diagnosis.json')).write_text(json.dumps(diagnosis,indent=2)+'\n')
  raise
 watch_values=re.findall(r'MEDIA_WATCH_EXIT=(\d+)',watched)
 rows=exit_watch_receipts(trial,owner,prefix)
 diagnosis={**exit_diagnosis(rows,values),'watcher_exit_values':watch_values}
 (OUT/(prefix+'-exit-diagnosis.json')).write_text(json.dumps(diagnosis,indent=2)+'\n')
 if watch_values!=['0']:raise RuntimeError('exit watcher did not finish successfully '+trial+' '+repr(watch_values))
 if values!=['0']:raise RuntimeError('direct browser exit value missing, duplicated or nonzero '+trial+' '+repr(values))
 return diagnosis


def event(trial,phases,timeout):
 def matches(row):
  payload=row.get('payload',{})
  return payload.get('trial')==trial and payload.get('phase') in phases
 while time.monotonic()<ready_deadline:
  try:return http.wait_record(matches,min(timeout,max(0,ready_deadline-time.monotonic())))
  except TimeoutError:raise RuntimeError('fixture phase deadline '+trial+' '+repr(phases))
 raise RuntimeError('720-second ready budget exhausted')

def assert_time_left(seconds):
 if ready_deadline-time.monotonic()<seconds:raise RuntimeError('insufficient budget for next bounded phase')

def host_task_snapshot(trial,phase):
 row={'trial':trial,'phase':phase,'host_monotonic_start_ns':time.monotonic_ns(),'host_utc':datetime.now(timezone.utc).isoformat(),'qemu_pid':owned_pid,'qemu_start_ticks':owned_start,'qmp_cpus_fast':host_cpu_map,'tasks':[]}
 def bounded(path,cap):
  try:
   with path.open('rb') as stream:data=stream.read(cap+1)
   if len(data)>cap:return {'error':'exceeds cap','cap':cap}
   return data.decode(errors='replace')
  except OSError as error:return {'error':str(error)}
 process=Path('/proc')/str(owned_pid)
 identity=bounded(process/'stat',4096)
 if not isinstance(identity,str) or identity.rsplit(') ',1)[1].split()[19]!=owned_start:raise RuntimeError('host QEMU identity changed before task snapshot')
 tasks=[entry for entry in (process/'task').iterdir() if entry.name.isdigit()]
 if len(tasks)>256:raise RuntimeError('host QEMU task count exceeds cap')
 for task in sorted(tasks,key=lambda entry:int(entry.name)):
  row['tasks'].append({'tid':int(task.name),'comm':bounded(task/'comm',256),'stat':bounded(task/'stat',4096),'schedstat':bounded(task/'schedstat',4096)})
 row['host_monotonic_end_ns']=time.monotonic_ns()
 path=OUT/(trial+'-'+phase+'-host-qemu-tasks.json')
 path.write_text(json.dumps(row,indent=2)+'\n')
 emit({'phase':'host-task-snapshot','trial':trial,'boundary':phase,'tasks':len(row['tasks']),'path':str(path),'duration_ns':row['host_monotonic_end_ns']-row['host_monotonic_start_ns']})

base_stat=BASE.stat()
protected_stat=PROTECTED_BASE.stat()
http=None;http_thread=None;audit_error=False;ready_deadline=None
try:
 if not KERNEL_VARIANT and (KERNEL_EXPECTED_SHA!='59253eb271555b6e1e3e035280002d79a7fc82f3e236b288ca981a0e495ed104' or hashlib.sha256(KERNEL.read_bytes()).hexdigest()!=KERNEL_EXPECTED_SHA):
  raise RuntimeError('explicit kernel SHA missing or mismatched')
 if not FIXTURE_SHA or hashlib.sha256(FIXTURE.read_bytes()).hexdigest()!=FIXTURE_SHA:
  raise RuntimeError('final fixture SHA missing or mismatched')
 if MEDIA.stat().st_size!=15740901 or hashlib.sha256(MEDIA.read_bytes()).hexdigest()!=MEDIA_SHA:
  raise RuntimeError('extracted media identity mismatch')
 if os.environ.get('GL_AUDIT_APPEND_EXTRA','')!='virtio_gpu_submit_trace=1':raise RuntimeError('only submit aggregate trace gate is permitted')
 if inventory():raise RuntimeError('QEMU preflight is not zero')
 module_spec=importlib.util.spec_from_file_location('xv6_media_ab_http',SERVER_SOURCE)
 module=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(module)
 http=module.MediaServer(('127.0.0.1',0),FIXTURE,MEDIA,OUT,OWNER_SOURCE.read_bytes(),DIAGNOSTIC['policy_bytes'] if DIAGNOSTIC else b'',SELECTED_TRIALS)
 http_thread=threading.Thread(target=http.serve_forever,daemon=True)
 http_thread.start()
 origin='http://10.0.2.2:'+str(http.server_port)
 for file,name in [(Path(__file__),'controller.py'),(SERVER_SOURCE,'http-server.py'),(OWNER_SOURCE,'owner-helper.py'),(HELPERS/'capture_config.py','capture_config.py'),(HELPERS/'capture_cleanup.py','capture_cleanup.py'),(HELPERS/'kernel_variant.py','kernel_variant.py'),(FIXTURE,'video-drops-fixture.html'),(HELPERS/'serial-qemu-wrapper.py','serial-qemu-wrapper.py')]:
  shutil.copyfile(file,OUT/name)
 if DIAGNOSTIC:
  for name,data in [('diagnostic-config.json',DIAGNOSTIC['config_raw']),('diagnostic-runtime-manifest.json',DIAGNOSTIC['manifest_raw']),('diagnostic-source-pin.json',DIAGNOSTIC['pin_raw']),('capture-policy.json',DIAGNOSTIC['policy_bytes'])]:
   (OUT/name).write_bytes(data)
  (OUT/'diagnostic-host-preflight.json').write_text(json.dumps(DIAGNOSTIC['receipt'],indent=2)+'\n')
 if KERNEL_VARIANT:
  (OUT/'kernel-variant.json').write_bytes(KERNEL_VARIANT['receipt_raw'])
  (OUT/'kernel-build-receipt.json').write_bytes(KERNEL_VARIANT['build_receipt_raw'])
  (OUT/'kernel-variant-preflight.json').write_text(json.dumps(KERNEL_VARIANT['preflight'],indent=2)+'\n')
 (OUT/'source-state.txt').write_text(run(['git','status','--short'],timeout=20).stdout+'\n'+run(['git','rev-parse','HEAD']).stdout+'\n'+run(['git','submodule','status','--recursive'],timeout=20).stdout)
 provenance={'token':TOKEN,'kernel':str(KERNEL),'kernel_sha256':KERNEL_EXPECTED_SHA,'fixture':str(FIXTURE),'fixture_sha256':FIXTURE_SHA,'media':str(MEDIA),'media_bytes':MEDIA.stat().st_size,'media_sha256':MEDIA_SHA,'rootfs':str(BASE),'rootfs_size':base_stat.st_size,'rootfs_mtime_ns':base_stat.st_mtime_ns,'launch_env':{k:v for k,v in ENV.items() if k.startswith('QEMU_') or k in ['AUTO_BUILD','DISPLAY_MODE']},'order':list(module.TRIALS),'measurement_ms':15000,'transport':'same-origin HTTP Range; unchanged frozen clip/fixture','browser_trace':'media,cc,viz,benchmark,mojom,mojom.flow,graphics.pipeline,disabled-by-default-mojom; OFF/ON/ON/OFF; all WAYLAND_DEBUG disabled; 32 MiB ring; JSON <=64 MiB collected only after graceful browser exit','helper_hashes':{name:hashlib.sha256((OUT/name).read_bytes()).hexdigest() for name in ['controller.py','http-server.py','owner-helper.py']}}
 provenance['capture_identity']=CAPTURE_IDENTITY
 provenance['capture_purpose']=CAPTURE_PURPOSE
 provenance['order']=SELECTED_TRIALS
 provenance['browser_trace']=capture_config.owner.TRACE_CATEGORIES+'; '+capture_config.owner.capture_controls(CAPTURE_PURPOSE)+'; all WAYLAND_DEBUG disabled; 32 MiB ring; JSON <=64 MiB collected only after graceful browser exit'
 provenance['helper_hashes']['capture_config.py']=hashlib.sha256((OUT/'capture_config.py').read_bytes()).hexdigest()
 provenance['helper_hashes']['capture_cleanup.py']=hashlib.sha256((OUT/'capture_cleanup.py').read_bytes()).hexdigest()
 provenance['helper_hashes']['kernel_variant.py']=hashlib.sha256((OUT/'kernel_variant.py').read_bytes()).hexdigest()
 if KERNEL_VARIANT:provenance['kernel_variant']=KERNEL_VARIANT['identity']
 if DIAGNOSTIC:
  provenance['diagnostic_preflight']=DIAGNOSTIC['receipt']
  provenance['capture_policy_sha256']=DIAGNOSTIC['policy_sha256']
  provenance['browser_trace']+= '; diagnostic ON category: '+capture_config.owner.DIAGNOSTIC_CATEGORY
 (OUT/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
 emit({'phase':'preflight','out':str(OUT),'origin':origin,'qemu_count':0,'provenance':provenance})
 for name in ['xv6-fb-pacing-snapshot','xv6-fb-pacing-snapshot.c','xv6-fb-pacing-proof.json','xv6-fb-pacing-snapshot-validation.json','xv6-fb-pacing-layout.json']:
  shutil.copyfile(RETAINED/name,OUT/name)
 exact_preflight=run(['bash',str(ROOT/'scripts/launch/qemu-exact-inventory.sh'),'--require-zero'])
 (OUT/'exact-preflight.txt').write_text(exact_preflight.stdout)
 if exact_preflight.returncode:raise RuntimeError('exact QEMU preflight failed')
 if DIAGNOSTIC:capture_config.check_prelaunch_stamps(DIAGNOSTIC)
 if KERNEL_VARIANT:kernel_variant.check_prelaunch(KERNEL_VARIANT)
 log=(OUT/'run.log').open('w')
 launcher=subprocess.Popen(['bash',str(ROOT/'scripts/launch/launch-gui.sh')],cwd=ROOT,env=ENV,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT)
 deadline=time.monotonic()+40
 while time.monotonic()<deadline:
  if launcher.poll() is not None:raise RuntimeError('launcher exited '+str(launcher.returncode))
  if PID.exists() and QMP.exists() and SERIAL.exists():
   owned_pid=int(PID.read_text());owned_start=Path(f'/proc/{owned_pid}/stat').read_text().rsplit(') ',1)[1].split()[19]
   if validate():break
  time.sleep(.1)
 else:raise RuntimeError('owned launch startup deadline')
 serial_sock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);serial_sock.settimeout(.2);serial_sock.connect(str(SERIAL))
 serial_thread=threading.Thread(target=pump_serial,daemon=True);serial_thread.start()
 qsock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);qsock.settimeout(10);qsock.connect(str(QMP));qfile=qsock.makefile('rb')
 greeting=json.loads(qfile.readline());qmp('qmp_capabilities')
 (OUT/'qemu-cmdline.json').write_text(json.dumps(Path(f'/proc/{owned_pid}/cmdline').read_bytes().decode().split('\0'),indent=2))
 host_cpu_map=qmp('query-cpus-fast')
 (OUT/'qmp-cpus-fast.json').write_text(json.dumps(host_cpu_map,indent=2)+'\n')
 emit({'phase':'owned-running','pid':owned_pid,'start_ticks':owned_start,'qmp':greeting,'title':TITLE})
 prompt_deadline=time.monotonic()+60
 while time.monotonic()<prompt_deadline:
  try:
   with (OUT/'serial.log').open('rb') as stream:
    stream.seek(max(0,(OUT/'serial.log').stat().st_size-16384));tail=stream.read(16384)
  except FileNotFoundError:tail=b''
  clean=re.sub(rb'\x1b\[[0-9;?]*[A-Za-z]',b'',tail)
  if re.search(rb'root:[^\r\n]*[#\$] ',clean):break
  if launcher.poll() is not None:raise RuntimeError('VM exited before shell prompt')
  time.sleep(.1)
 else:raise RuntimeError('initial shell prompt deadline')
 emit({'phase':'initial-shell-prompt'})
 fit_env=ENV.copy();fit_env['XV6_QEMU_WINDOW_FIT_MODE']='clamp';fit_env['XV6_QEMU_WINDOW_FIT_WATCH_SECONDS']='0'
 fit=run(['bash',str(ROOT/'scripts/gpu/fit-owned-qemu-window.sh'),TITLE],timeout=20,env=fit_env)
 (OUT/'initial-fit.txt').write_text(fit.stdout);emit({'fit_rc':fit.returncode,'fit':fit.stdout.strip()})

 ready_deadline=time.monotonic()+720
 emit({'phase':'serial-ready-preflight','ready_deadline_host_monotonic_ns':int(ready_deadline*1e9),'workflow':capture_config.owner.capture_controls(CAPTURE_PURPOSE)+'; all Wayland tracing OFF; submit aggregate gate ON','capture_purpose':CAPTURE_PURPOSE,'selected_order':[trial for trial,_ in SELECTED_TRIALS]})
 text="/usr/bin/python3 -c 'import urllib.request;open(\"/tmp/mo.py\",\"wb\").write(urllib.request.urlopen(\""+origin+"/owner.py\",timeout=10).read())'"
 serial_command(text,'02-stage-owner')
 serial_command('printf %s '+shlex.quote(origin)+' >/tmp/media-audit-origin','02-stage-origin')
 serial_command('unset '+capture_config.owner.POLICY_ENV,'02-clear-helper-policy')
 if DIAGNOSTIC:
  policy_result=serial_command('/usr/bin/python3 /tmp/mo.py policy '+DIAGNOSTIC['policy_sha256'],'02-stage-diagnostic-policy')
  if not any(row.get('phase')=='capture-policy' and row.get('sha256')==DIAGNOSTIC['policy_sha256'] and row.get('capture_identity')==CAPTURE_IDENTITY for row in owner_rows(policy_result)):raise RuntimeError('guest diagnostic policy not verified')
  serial_command('export '+capture_config.owner.POLICY_ENV+'='+DIAGNOSTIC['policy_sha256'],'02-activate-diagnostic-policy')
 result=serial_command('/usr/bin/python3 /tmp/mo.py session','03-session-identity')
 if not any(row.get('phase')=='session' and all(row.get('checks',{}).values()) for row in owner_rows(result)):raise RuntimeError('KDE readiness identity unavailable')
 binary_result=serial_command('/usr/bin/python3 /tmp/mo.py binary','03-browser-binary-identity',timeout=180 if DIAGNOSTIC else 35)
 binary_receipt=next((row for row in owner_rows(binary_result) if row.get('phase')=='browser-binary' and row.get('matched') is True and row.get('capture_identity')==CAPTURE_IDENTITY),None)
 if not binary_receipt:raise RuntimeError('selected browser hash and capture identity not verified')
 if DIAGNOSTIC:
  expected_runtime=CAPTURE_POLICY['runtime_files']
  expected_digest=hashlib.sha256(capture_config.owner.policy_bytes(expected_runtime)).hexdigest()
  if not binary_receipt.get('all_runtime_files_matched') or binary_receipt.get('runtime_files_count')!=len(expected_runtime) or binary_receipt.get('runtime_bytes')!=sum(row['bytes'] for row in expected_runtime) or binary_receipt.get('runtime_files_sha256')!=expected_digest:raise RuntimeError('guest runtime asset verification incomplete')
 fb_stage=serial_command('/usr/bin/python3 /tmp/mo.py stage-fb '+origin,'03-stage-fb-helper')
 if not any(row.get('phase')=='stage-fb' for row in owner_rows(fb_stage)):raise RuntimeError('FB helper staging not verified')
 snapshot('01-kde-ready-desktop')
 emit({'phase':'kde-ready','remaining_seconds':ready_deadline-time.monotonic()})
 serial_command('while IFS= read -r e; do [ -n "$e" ] && export "$e"; done </tmp/media-ab-session.env; export WAYLAND_CHROMIUM_MULTIPROCESS=1; unset WAYLAND_DEBUG','04-session-env')
 serial_command('cat /proc/cmdline','05-kernel-append')
 all_trials=[];reference_geometry=None;reference_argv=None;reference_environment=None
 for index,(trial,traced) in enumerate(SELECTED_TRIALS,1):
  assert_time_left(155)
  prefix=f't{index}-{trial}'
  cursor_result=serial_command('/usr/bin/python3 /tmp/mo.py cursor '+trial,prefix+'-log-start')
  if not any(row.get('phase')=='cursor' and row.get('profile_absent') for row in owner_rows(cursor_result)):raise RuntimeError('fresh-profile/log boundary not established')
  url=origin+'/?trial='+trial
  policy='unset WAYLAND_DEBUG'
  serial_command(policy+'; /usr/bin/python3 /tmp/mo.py launch '+trial+' '+shlex.quote(origin)+' >/tmp/'+trial+'.outer 2>&1 & cpid=$!; echo MEDIA_CHILD_PID=$cpid',prefix+'-launch')
  ready=event(trial,('ready','error'),35)
  if ready['payload']['phase']=='error':raise RuntimeError('fixture error before start '+json.dumps(ready))
  inspected=serial_command('/usr/bin/python3 /tmp/mo.py owner $cpid '+trial+' off',prefix+'-owner')
  owner=next((row for row in owner_rows(inspected) if row.get('phase')=='owner'),None)
  if not owner or not all(owner.get('checks',{}).values()):raise RuntimeError('browser owner/environment mismatch '+trial)
  if owner.get('capture_identity')!=CAPTURE_IDENTITY:raise RuntimeError('browser capture identity changed '+trial)
  normalized_argv=['--user-data-dir=<PROFILE>' if arg.startswith('--user-data-dir=') else origin+'/?trial=<TRIAL>' if arg.startswith(origin+'/?trial=') else arg for arg in owner['argv'] if not arg.startswith(('--enable-tracing=', '--trace-startup-', '--default-trace-buffer-size-limit-in-kb='))]
  trace_flags=[arg for arg in owner['argv'] if arg.startswith(('--enable-tracing=', '--trace-startup-', '--default-trace-buffer-size-limit-in-kb='))]
  expected_flags=capture_config.owner.trace_flags(trial,CAPTURE_POLICY)
  if trace_flags!=expected_flags:raise RuntimeError('actual Chrome tracing flags mismatch')
  normalized_environment={key:value for key,value in owner['environment'].items() if key!='WAYLAND_DEBUG'}
  if reference_argv is None:reference_argv=normalized_argv;reference_environment=normalized_environment
  launch_receipts=[record['payload']['owner'] for record in http.records
                   if record.get('payload',{}).get('phase')=='owner-result'
                   and record['payload'].get('owner',{}).get('phase')=='launch'
                   and record['payload']['owner'].get('trial')==trial]
  if len(launch_receipts)!=1:raise RuntimeError('unique pre-exec launch receipt missing')
  launch_receipt=launch_receipts[0]
  if launch_receipt.get('capture_identity')!=CAPTURE_IDENTITY:raise RuntimeError('launch capture identity changed '+trial)
  if launch_receipt['pid']!=owner['pid'] or launch_receipt['start_ticks']!=owner['start_ticks'] or launch_receipt['url']!=url:
   raise RuntimeError('pre-exec launch identity or requested URL mismatch')
  capped_url=(traced and len(owner['argv'])==32 and len(reference_argv)+len(trace_flags)==33
              and normalized_argv==reference_argv[:-1] and reference_argv[-1]==origin+'/?trial=<TRIAL>'
              and ready['payload'].get('trial')==trial and ready['payload'].get('phase')=='ready')
  if (normalized_argv!=reference_argv and not capped_url) or normalized_environment!=reference_environment:
   raise RuntimeError('non-trace browser launch policy changed between arms')
  owner['argument_observation']={'procfs_maxarg':32,'observed_count':len(owner['argv']),
      'final_url_beyond_procfs_cap':capped_url,'requested_launch':launch_receipt,
      'proof':'exact exposed argument prefix, expected diagnostic flags, same-PID launch URL and unique fixture ready receipt' if capped_url else 'complete exposed argv matched'}
  if CAPTURE_PURPOSE=='emission-smoke':
   url_exposed=url in owner['argv']
   if not url_exposed and len(owner['argv'])!=32:raise RuntimeError('smoke URL absent without exact procfs argument cap')
   owner['argument_observation']={'procfs_maxarg':32,'observed_count':len(owner['argv']),
       'requested_url_exposed':url_exposed,'full_procfs_argv_proven':len(owner['argv'])<32,
       'requested_launch':launch_receipt,'cross_arm_argv_parity':'not measured in emission-smoke',
       'proof':'actual trace flags, fixed browser/profile/environment, same-PID/start requested URL and unique fixture ready receipt; capped argv has no complete-argument claim'}
  # Page HTTP readiness can precede Wayland mapping; require an actual visual review.
  time.sleep(4)
  mouse(500,250,'left');time.sleep(.5)
  snapshot(prefix+'-before')
  approval=OUT/(prefix+'-mapped-visible.ok')
  emit({'phase':'mapped-review','trial':trial,'image':str(OUT/(prefix+'-before-host.png')),'marker':str(approval),'instruction':'Create this regular marker file only after inspecting a mapped, visible browser page.'})
  review_deadline=min(time.monotonic()+90,ready_deadline)
  next_preview=time.monotonic()+2;second_preview=False
  while time.monotonic()<review_deadline:
   if approval.is_file() and not approval.is_symlink():break
   if launcher.poll() is not None:raise RuntimeError('VM exited during mapped review')
   if not second_preview and time.monotonic()>=next_preview:
    snapshot(prefix+'-before-review2');second_preview=True
    emit({'phase':'mapped-review','trial':trial,'image':str(OUT/(prefix+'-before-review2-host.png')),'marker':str(approval),'instruction':'Second preview; measurement is still gated.'})
   time.sleep(.1)
  else:raise RuntimeError('mapped-visible review marker deadline '+trial)
  emit({'phase':'mapped-visible-approved','trial':trial,'marker':str(approval)})
  mouse(500,250,'left');time.sleep(.25)
  telemetry=serial_command('/usr/bin/python3 /tmp/mo.py telemetry '+trial+' before '+origin,prefix+'-before-telemetry',timeout=45)
  if not any(row.get('phase')=='telemetry' for row in owner_rows(telemetry)):raise RuntimeError('before telemetry missing')
  host_task_snapshot(trial,'before')
  http.set_control(trial,start=True)
  started=event(trial,('started','error'),35)
  if started['payload']['phase']=='error':raise RuntimeError('fixture failed starting '+trial)
  emit({'phase':'measurement-started','trial':trial,'browser_report':started})
  # No serial, QMP calls, captures, validation subprocesses or logging polls in this interval.
  terminal=event(trial,('complete','error'),35)
  host_task_snapshot(trial,'after')
  emit({'phase':'measurement-finished','trial':trial,'browser_report':terminal})
  payload=terminal['payload']
  if payload['phase']=='error':raise RuntimeError('fixture measurement error '+trial)
  observation={'trial':trial,'browser_trace_enabled':traced,'wayland_trace_enabled':False,
               'owner':owner,'ready':ready,'started':started,'complete':terminal,
               'direct_child_exit':None,'measurement_usable':payload.get('diagnostic_valid') is True,
               'legacy_status':payload.get('legacy',{}).get('status'),'export_status':'not yet attempted',
               'capture_identity':CAPTURE_IDENTITY}
  if KERNEL_VARIANT:observation['kernel_variant']=KERNEL_VARIANT['identity']
  observation_path=OUT/(prefix+'-completed-observation.json')
  observation_path.write_text(json.dumps(observation,indent=2)+'\n')
  geometry={key:payload.get(key) for key in ['inner_width','inner_height','dpr','fullscreen','video_rect']}
  if reference_geometry is None:reference_geometry=geometry
  if geometry!=reference_geometry:raise RuntimeError('trial window geometry changed')
  telemetry=serial_command('/usr/bin/python3 /tmp/mo.py telemetry '+trial+' after '+origin,prefix+'-after-telemetry',timeout=45)
  if not any(row.get('phase')=='telemetry' for row in owner_rows(telemetry)):raise RuntimeError('after telemetry missing')
  snapshot(prefix+'-complete')
  if payload.get('fullscreen') or payload.get('visibility')!='visible' or not payload.get('has_focus'):
   raise RuntimeError('browser not focused/windowed for graphical close')
  observation['exit_diagnosis']=close_browser(owner,trial,prefix,origin,traced)
  observation['direct_child_exit']=0
  observation_path.write_text(json.dumps(observation,indent=2)+'\n')
  log_result=serial_command('/usr/bin/python3 /tmp/mo.py log-end '+trial+' '+origin,prefix+'-log-upload')
  if not any(row.get('phase')=='log-end' for row in owner_rows(log_result)):raise RuntimeError('trial log upload incomplete')
  trace_export='disabled'
  if traced:
   trace_result=serial_command('/usr/bin/python3 /tmp/mo.py trace-end '+trial+' '+origin,prefix+'-trace-upload',timeout=60)
   trace_export='accepted' if any(row.get('phase')=='trace-end' for row in owner_rows(trace_result)) else 'rejected'
   if trace_export=='rejected':emit({'phase':'trace-export-rejected','trial':trial,'receipts':owner_rows(trace_result)})
  result={'trial':trial,'browser_trace_enabled':traced,'wayland_trace_enabled':False,'owner':owner,'ready':ready,'started':started,'complete':terminal,'direct_child_exit':0,'measurement_usable':payload.get('diagnostic_valid') is True,'legacy_status':payload.get('legacy',{}).get('status'),'classification':'usable' if payload.get('diagnostic_valid') is True else 'invalid-diagnostics','trace_export':trace_export}
  result['capture_identity']=CAPTURE_IDENTITY
  if KERNEL_VARIANT:result['kernel_variant']=KERNEL_VARIANT['identity']
  all_trials.append(result)
  (OUT/'trial-summary.json').write_text(json.dumps(all_trials,indent=2)+'\n')
  emit({'phase':'trial-finished','trial':trial,'legacy':payload.get('legacy'),'diagnostic_valid':payload.get('diagnostic_valid'),'measurement_usable':result['measurement_usable'],'classification':result['classification'],'remaining_seconds':ready_deadline-time.monotonic()})
 serial_command('fbstat >/tmp/fbtrace; while IFS= read -r l; do case "$l" in virtio_*|drm_event*) echo "$l";; esac; done </tmp/fbtrace','99-final-counters')
 summary=capture_config.capture_summary(all_trials,CAPTURE_PURPOSE)
 summary['capture_identity']=CAPTURE_IDENTITY
 if KERNEL_VARIANT:summary['kernel_variant']=KERNEL_VARIANT['identity']
 (OUT/('emission-smoke-status.json' if args.diagnostic_smoke else 'comparison-status.json')).write_text(json.dumps(summary,indent=2)+'\n')
 emit({'phase':'all-trials-finished',**summary})
except BaseException as e:
 audit_error=True
 emit({'error':str(e),'error_type':type(e).__name__})
finally:
 cleanup_errors=[]
 def cleanup_step(label,operation):
  try:return operation()
  except BaseException as error:
   cleanup_errors.append({'step':label,'type':type(error).__name__,'error':str(error)})
   return None
 def cleanup_exact_owned_qemu():
  global owned_pid,owned_start
  # Startup may have published its owned PID before the QMP/serial sockets.
  # Recover only from this fresh receipt, then require the normal exact token,
  # executable, start-tick and process-group validation before any signal.
  if owned_pid is None and PID.is_file() and not PID.is_symlink():
   candidate=int(PID.read_text())
   candidate_stat=Path(f'/proc/{candidate}/stat')
   if candidate_stat.exists():
    owned_pid=candidate;owned_start=candidate_stat.read_text().rsplit(') ',1)[1].split()[19]
  if owned_pid is None or not validate():return
  response=run(['bash',str(ROOT/'scripts/launch/cleanup-owned-qemu.sh'),str(owned_pid),str(owned_start),TOKEN],timeout=12)
  def save_owned_cleanup():
   with (OUT/'owned-qemu-cleanup.txt').open('a') as stream:stream.write(response.stdout)
  cleanup_step('owned_qemu_cleanup_transcript',save_owned_cleanup)
  if response.returncode:raise RuntimeError('owned QEMU cleanup returned '+str(response.returncode))
 launcher_cleanup=None
 if launcher is not None:
  launcher_cleanup=cleanup_step('launcher_cleanup',lambda:capture_cleanup.stop_owned_launcher(
    launcher,lambda:qmp('quit') if qsock else None,cleanup_exact_owned_qemu))
  if launcher_cleanup:cleanup_errors.extend(launcher_cleanup['errors'])
 if log:cleanup_step('launcher_log_close',log.close)
 if qfile:cleanup_step('qmp_file_close',qfile.close)
 if qsock:cleanup_step('qmp_socket_close',qsock.close)
 serial_stop.set()
 if serial_sock:cleanup_step('serial_socket_close',serial_sock.close)
 if serial_thread:
  cleanup_step('serial_thread_join',lambda:serial_thread.join(timeout=2))
  if serial_thread.is_alive():cleanup_errors.append({'step':'serial_thread_join','error':'thread remains live'})
 cleanup_step('serial_path_remove',lambda:SERIAL.unlink(missing_ok=True))
 def copy_debugcon():
  if Path('/tmp/xv6-debugcon.log').is_file():shutil.copyfile('/tmp/xv6-debugcon.log',OUT/'debugcon.log')
 cleanup_step('debugcon_copy',copy_debugcon)
 if http is not None:
  cleanup_step('http_shutdown',lambda:capture_cleanup.stop_server_loop(http,http_thread))
  cleanup_step('http_close',http.server_close)
 cleanup_step('qmp_path_remove',lambda:QMP.unlink(missing_ok=True))
 # These final checks run even if signaling, waits, socket teardown or copies
 # failed. Unknown/nonzero inventory is always a failed cleanup verdict.
 exact=cleanup_step('exact_final_inventory',lambda:run(['bash',str(ROOT/'scripts/launch/qemu-exact-inventory.sh'),'--require-zero']))
 if exact:
  cleanup_step('exact_final_inventory_receipt',lambda:(OUT/'exact-final-inventory.txt').write_text(exact.stdout))
 remaining=cleanup_step('final_proc_inventory',inventory)
 base_after=cleanup_step('base_final_stat',BASE.stat)
 protected_after=cleanup_step('protected_final_stat',PROTECTED_BASE.stat)
 result={'launcher_rc':None if launcher is None else launcher.returncode,
         'launcher_cleanup':launcher_cleanup,'qemu_remaining':remaining,
         'exact_inventory_rc':None if exact is None else exact.returncode,
         'overlay_removed':not os.path.lexists(OVERLAY),
         'base_size_unchanged':base_after is not None and base_after.st_size==base_stat.st_size,
         'base_mtime_unchanged':base_after is not None and base_after.st_mtime_ns==base_stat.st_mtime_ns,
         'base_stamp_unchanged':base_after is not None and capture_config.owner.file_stamp(base_after)==capture_config.owner.file_stamp(base_stat),
         'protected_baseline_stamp_unchanged':protected_after is not None and capture_config.owner.file_stamp(protected_after)==capture_config.owner.file_stamp(protected_stat),
         'out':str(OUT),'cleanup_errors':cleanup_errors}
 if KERNEL_VARIANT:
  result['kernel_variant']=KERNEL_VARIANT['identity']
  result['kernel_variant_inputs_unchanged']=cleanup_step('kernel_variant_final_stamps',lambda:kernel_variant.check_prelaunch(KERNEL_VARIANT)) is True
 result['cleanup_gates_passed']=(remaining==[] and exact is not None and exact.returncode==0 and
     (launcher is None or launcher_cleanup is not None and launcher_cleanup['reaped'] and launcher_cleanup['clean_exit']) and
     all(result[key] for key in ('overlay_removed','base_stamp_unchanged','protected_baseline_stamp_unchanged')) and
     result.get('kernel_variant_inputs_unchanged',True))
 def save_screenshots():
  manifest=[{'file':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'size':p.stat().st_size} for p in OUT.glob('*.png')]
  (OUT/'screenshots.json').write_text(json.dumps(manifest,indent=2)+'\n')
 cleanup_step('screenshot_manifest',save_screenshots)
 cleanup_step('finished_log',lambda:emit({'phase':'finished',**result}))
 cleanup_step('transcript_close',transcript.close)
 cleanup_step('cleanup_receipt',lambda:(OUT/'cleanup.json').write_text(json.dumps(result,indent=2)+'\n'))
 if cleanup_errors:
  try:print(json.dumps({'phase':'cleanup-errors','out':str(OUT),'errors':cleanup_errors}),file=sys.stderr,flush=True)
  except BaseException:pass
 if remaining is None or remaining or exact is None or exact.returncode or (launcher is not None and (not launcher_cleanup or not launcher_cleanup['reaped'])):sys.exit(2)
 if audit_error or cleanup_errors or not result['cleanup_gates_passed']:sys.exit(1)
