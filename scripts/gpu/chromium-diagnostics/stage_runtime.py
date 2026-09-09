#!/usr/bin/env python3
"""Stage an explicit diagnostic runtime into a new private raw ext image.

No mount, fsck, launcher change or VM operation. Existing output paths are
refused. Runtime files are mapped from host_browser_path.parent and verified
before and after debugfs writes; only the fixed guest runtime subtree changes.
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import selectors
import signal
import stat
import subprocess
import tempfile
import time

ROOT = '/opt/host-gui/wayland-chromium/chrome-linux64'
BROWSER = ROOT + '/chrome'
MAX_IMAGE = 64 * 1024**3
MAX_FILE = 2 * 1024**3
MAX_TOTAL = 4 * 1024**3
MAX_ENTRIES = 4096
SAFE_NAME = re.compile(r'[A-Za-z0-9_+.-]+\Z')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(info):
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns,
            info.st_ctime_ns, info.st_mode, info.st_nlink)


def host_path(value, missing=False):
    path = pathlib.Path(value)
    require(path.is_absolute() and '..' not in path.parts, 'host paths must be absolute without traversal')
    for component in [*reversed(path.parents), path]:
        try:
            info = component.lstat()
        except FileNotFoundError:
            require(missing and component == path, 'missing host path: '+str(component))
            continue
        require(not stat.S_ISLNK(info.st_mode), 'symlink host path: '+str(component))
        if component != path:
            require(stat.S_ISDIR(info.st_mode), 'non-directory host parent')
        elif missing:
            raise ValueError('output already exists: '+str(path))
    return path


def open_regular(path, maximum):
    path = host_path(str(path))
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        info = os.fstat(fd)
        require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, 'regular single-link source required: '+str(path))
        require(info.st_size <= maximum, 'source exceeds size cap: '+str(path))
        require(identity(info) == identity(path.lstat()), 'source changed while opening')
        return fd, info
    except BaseException:
        os.close(fd)
        raise


def unchanged(path, fd, before):
    require(identity(os.fstat(fd)) == identity(before) == identity(host_path(str(path)).lstat()),
            'source changed: '+str(path))


def digest(fd, maximum, deadline):
    os.lseek(fd, 0, os.SEEK_SET)
    value, size = hashlib.sha256(), 0
    while True:
        require(time.monotonic() < deadline, 'staging deadline exceeded')
        block = os.read(fd, 1024*1024)
        if not block:
            break
        size += len(block)
        require(size <= maximum, 'hash input exceeds size cap')
        value.update(block)
    return value.hexdigest(), size


def run(argv, deadline, pass_fds=(), timeout=120, output_limit=1024*1024):
    """Own, bound and synchronously reap each child, including timeouts."""
    remaining = min(timeout, deadline-time.monotonic())
    require(remaining > 0, 'staging deadline exceeded')
    process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               pass_fds=pass_fds, start_new_session=True)
    buffers = [bytearray(),bytearray()]
    try:
        command_end = time.monotonic()+remaining
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout,selectors.EVENT_READ,0)
            selector.register(process.stderr,selectors.EVENT_READ,1)
            while selector.get_map():
                remaining = command_end-time.monotonic()
                require(remaining > 0, 'command deadline exceeded')
                for key,_ in selector.select(remaining):
                    block = os.read(key.fileobj.fileno(),16384)
                    if not block:
                        selector.unregister(key.fileobj)
                    else:
                        buffers[key.data].extend(block)
                        require(len(buffers[key.data]) <= output_limit, 'command output cap exceeded')
        process.wait(timeout=max(0.001,command_end-time.monotonic()))
        out, err = (value.decode('utf-8','strict') for value in buffers)
        require(process.returncode == 0, 'command failed: '+err[:2048])
        return out, err
    except BaseException:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        raise
    finally:
        process.stdout.close()
        process.stderr.close()


class ExtImage:
    def __init__(self, fd, deadline):
        self.fd, self.deadline = fd, deadline

    def commands(self, commands, write=False, extra_fds=()):
        require(0 < len(commands) <= 64, 'debugfs command batch cap exceeded')
        require(all('\n' not in command and len(command) <= 4096 for command in commands) and
                sum(map(len,commands)) <= 65536, 'debugfs command text cap or multiline command')
        with tempfile.TemporaryFile() as script:
            script.write(('\n'.join(commands)+'\n').encode())
            script.flush()
            script.seek(0)
            argv = ['debugfs'] + (['-w'] if write else [])
            argv += ['-f', '/proc/self/fd/'+str(script.fileno()), '/proc/self/fd/'+str(self.fd)]
            out, err = run(argv, self.deadline, (self.fd, script.fileno(), *extra_fds))
        require(not any(line and not line.startswith('debugfs ') for line in err.splitlines()),
                'debugfs reported an error: '+err[:2048])
        return '\n'.join(line for line in out.splitlines() if not line.startswith('debugfs: '))

    def info(self, path):
        output = self.commands(['stat '+path])
        match = re.search(r'Inode: (\d+)\s+Type: (\w+)\s+Mode:\s+([0-7]+)', output)
        links = re.search(r'Links:\s+(\d+)', output)
        size = re.search(r'\bSize:\s+(\d+)', output)
        require(match is not None and links is not None and size is not None, 'unrecognized debugfs stat')
        return dict(inode=int(match[1]), type=match[2], mode=int(match[3],8), links=int(links[1]), bytes=int(size[1]))

    def entries(self, path):
        output = self.commands(['ls -p '+path])
        entries = []
        for line in output.splitlines():
            if not line:
                continue
            fields = line.split('/')
            require(len(fields) == 8 and fields[0] == fields[-1] == '', 'unrecognized debugfs directory entry')
            if fields[5] in ('.','..'):
                continue
            require(SAFE_NAME.fullmatch(fields[5]) and len(fields[5]) <= 255, 'unsafe guest entry name')
            entries.append((fields[5], int(fields[2],8)))
        require(len(entries) <= MAX_ENTRIES and len({row[0] for row in entries}) == len(entries), 'directory cap or duplicate entry')
        return entries

    def tree(self):
        result, pending = {}, [ROOT]
        while pending:
            path = pending.pop()
            require(len(path.split('/')) <= 40 and len(result) < MAX_ENTRIES, 'runtime tree cap exceeded')
            row = self.info(path)
            require(row['type'] in ('directory','regular'), 'guest symlinks/special files refused')
            require(row['type'] == 'directory' or row['links'] == 1, 'guest hardlinks refused')
            result[path] = row
            if row['type'] == 'directory':
                for name, mode in self.entries(path):
                    require(stat.S_ISDIR(mode) or stat.S_ISREG(mode), 'guest symlinks/special files refused')
                    require(len(result)+len(pending) < MAX_ENTRIES, 'runtime tree cap exceeded')
                    pending.append(path+'/'+name)
        return result


def read_input(path):
    fd, before = open_regular(path, 1024*1024)
    try:
        raw = os.read(fd,1024*1024+1)
        require(len(raw) <= 1024*1024, 'input manifest cap exceeded')
        value = json.loads(raw)
        unchanged(path, fd, before)
    finally:
        os.close(fd)
    require(isinstance(value,dict), 'runtime manifest must be an object')
    pins = json.loads(pathlib.Path(__file__).with_name('upstream-sources.json').read_text())
    require(value.get('source_commit') == pins['chromium_commit'] and value.get('patch_sha256') == pins['patch_sha256'], 'source/patch pin mismatch')
    require(type(value.get('schema_version')) is int and value['schema_version'] == 1, 'unsupported input schema')
    browser = host_path(value['host_browser_path'])
    require(browser.name == 'chrome', 'host browser must be named chrome')
    files = value.get('runtime_files')
    require(isinstance(files,list) and 1 <= len(files) <= 1024, 'runtime file count cap')
    seen, total = set(), 0
    for row in files:
        require(isinstance(row,dict) and set(row) == {'guest_path','sha256','bytes'}, 'runtime files require guest_path/sha256/bytes only')
        guest = row['guest_path']
        require(isinstance(guest,str) and guest.startswith(ROOT+'/') and guest not in seen, 'duplicate or outside runtime guest path')
        suffix = guest[len(ROOT)+1:]
        require(len(suffix.split('/')) <= 32 and all(part not in ('','.','..') and SAFE_NAME.fullmatch(part) and len(part) <= 255 for part in suffix.split('/')), 'unsafe guest path')
        require(isinstance(row['sha256'],str) and re.fullmatch('[0-9a-f]{64}',row['sha256']), 'invalid file digest')
        require(type(row['bytes']) is int and 0 <= row['bytes'] <= MAX_FILE, 'file size cap')
        seen.add(guest)
        total += row['bytes']
    require(total <= MAX_TOTAL and BROWSER in seen, 'runtime aggregate cap or missing browser')
    browser_row = next(row for row in files if row['guest_path'] == BROWSER)
    require((value.get('browser_sha256'),value.get('browser_bytes')) == (browser_row['sha256'],browser_row['bytes']), 'browser identity differs from runtime record')
    require(isinstance(value.get('build_provenance',{}),dict), 'build_provenance must be an object')
    return value, browser, before, {'path':str(path),'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw)}


def stage(base_path, input_path, image_path, manifest_path, timeout=1200, *, base_sha256):
    require(0 < timeout <= 3600, 'staging timeout must be at most 3600 seconds')
    require(isinstance(base_sha256,str) and re.fullmatch('[0-9a-f]{64}',base_sha256), 'expected frozen base SHA-256 required')
    deadline = time.monotonic()+timeout
    base_path, input_path = host_path(str(base_path)), host_path(str(input_path))
    image_path, manifest_path = host_path(str(image_path),True), host_path(str(manifest_path),True)
    require(len({base_path,input_path,image_path,manifest_path}) == 4, 'distinct input/output paths required')
    data, browser, input_info, input_identity = read_input(input_path)
    sources, directories = {}, {}
    for row in data['runtime_files']:
        source = browser.parent / row['guest_path'][len(ROOT)+1:]
        fd, before = open_regular(source,MAX_FILE)
        try:
            require(digest(fd,MAX_FILE,deadline) == (row['sha256'],row['bytes']), 'runtime source identity mismatch')
            unchanged(source,fd,before)
        finally:
            os.close(fd)
        sources[row['guest_path']] = (source,before)
        parent = source.parent
        while True:
            info = host_path(str(parent)).lstat()
            require(stat.S_ISDIR(info.st_mode), 'runtime parent is not directory')
            guest_dir = ROOT + ('/'+str(parent.relative_to(browser.parent)) if parent != browser.parent else '')
            directories[guest_dir] = (parent,identity(info),stat.S_IMODE(info.st_mode))
            if parent == browser.parent:
                break
            parent = parent.parent
    require(sources[BROWSER][1].st_mode & 0o111, 'browser source is not executable')
    require(not set(sources).intersection(directories), 'file/directory runtime path collision')
    base_fd, base_info = open_regular(base_path,MAX_IMAGE)
    owned = []
    try:
        base_sha, base_bytes = digest(base_fd,MAX_IMAGE,deadline)
        unchanged(base_path,base_fd,base_info)
        require(base_sha == base_sha256, 'frozen base SHA-256 mismatch')
        image_fd = os.open(image_path,os.O_RDWR|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
        owned.append((image_path,image_fd,os.fstat(image_fd)))
        manifest_fd = os.open(manifest_path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
        owned.append((manifest_path,manifest_fd,os.fstat(manifest_fd)))
        run(['cp','--reflink=auto','--sparse=always','--','/proc/self/fd/'+str(base_fd),'/proc/self/fd/'+str(image_fd)],
            deadline,(base_fd,image_fd),timeout=600)
        unchanged(base_path,base_fd,base_info)
        require(os.fstat(image_fd).st_nlink == 1 and
                (os.fstat(image_fd).st_dev,os.fstat(image_fd).st_ino) != (base_info.st_dev,base_info.st_ino), 'clone is not a distinct private file')
        require(digest(image_fd,MAX_IMAGE,deadline) == (base_sha,base_bytes), 'private clone differs from frozen source')
        image = ExtImage(image_fd,deadline)
        filesystem_stats = image.commands(['stats'])
        parent = ''
        for component in ROOT.strip('/').split('/'):
            parent += '/'+component
            require(image.info(parent)['type'] == 'directory', 'guest runtime parent is not a directory')
        old = image.tree()
        commands = [('rmdir ' if old[path]['type'] == 'directory' else 'rm ')+path
                    for path in sorted(old,key=lambda path:(path.count('/'),path),reverse=True)]
        for index in range(0,len(commands),64):
            image.commands(commands[index:index+64],True)
        commands = []
        for path,(_,_,mode) in sorted(directories.items(),key=lambda item:item[0].count('/')):
            commands.extend(['mkdir '+path,'sif '+path+' mode 0'+format(stat.S_IFDIR|mode,'o')])
        for index in range(0,len(commands),64):
            image.commands(commands[index:index+64],True)
        for guest,(source,before) in sources.items():
            source_fd, current = open_regular(source,MAX_FILE)
            try:
                require(identity(current) == identity(before), 'runtime source changed before staging')
                image.commands(['write /proc/self/fd/'+str(source_fd)+' '+guest,
                                'sif '+guest+' mode 0'+format(stat.S_IFREG|stat.S_IMODE(before.st_mode),'o')],True,(source_fd,))
                unchanged(source,source_fd,before)
            finally:
                os.close(source_fd)
        after = image.tree()
        require(set(after) == set(sources)|set(directories), 'staged runtime membership differs from explicit list')
        for guest,(source,before) in sources.items():
            require(after[guest]['type'] == 'regular' and after[guest]['mode'] == stat.S_IMODE(before.st_mode), 'staged file type/mode mismatch')
            row = next(row for row in data['runtime_files'] if row['guest_path'] == guest)
            require(after[guest]['bytes'] == row['bytes'], 'staged file size mismatch')
            with tempfile.TemporaryFile() as dumped:
                image.commands(['dump '+guest+' /proc/self/fd/'+str(dumped.fileno())],extra_fds=(dumped.fileno(),))
                require(digest(dumped.fileno(),MAX_FILE,deadline) == (row['sha256'],row['bytes']), 'staged guest content hash mismatch')
            require(identity(host_path(str(source)).lstat()) == identity(before), 'runtime source changed after staging')
        for guest,(source,before,mode) in directories.items():
            require(after[guest]['type'] == 'directory' and after[guest]['mode'] == mode, 'staged directory mode mismatch')
            require(identity(host_path(str(source)).lstat()) == before, 'runtime source directory changed')
        unchanged(base_path,base_fd,base_info)
        os.fsync(image_fd)
        image_info = os.fstat(image_fd)
        rootfs_sha, rootfs_bytes = digest(image_fd,MAX_IMAGE,deadline)
        require(identity(os.fstat(image_fd)) == identity(image_info), 'private image changed during final hash')
        unchanged(base_path,base_fd,base_info)
        require(identity(host_path(str(input_path)).lstat()) == identity(input_info), 'runtime input manifest changed')
        for source,before in sources.values():
            require(identity(host_path(str(source)).lstat()) == identity(before), 'runtime source changed before completion')
        for source,before,_ in directories.values():
            require(identity(host_path(str(source)).lstat()) == before, 'runtime source directory changed before completion')
        for path,fd,created in owned:
            current = host_path(str(path)).lstat()
            require((current.st_dev,current.st_ino,current.st_nlink) ==
                    (created.st_dev,created.st_ino,1) and current.st_ino == os.fstat(fd).st_ino,
                    'owned output path changed')
        output = dict(schema_version=1,kind='chromium-bottleneck-diagnostic-runtime',status='staged',
            source_commit=data['source_commit'],patch_sha256=data['patch_sha256'],browser_path=BROWSER,
            browser_sha256=data['browser_sha256'],browser_bytes=data['browser_bytes'],host_browser_path=str(browser),
            rootfs_sha256=rootfs_sha,runtime_files=data['runtime_files'],build_provenance=data.get('build_provenance',{}),
            staging_provenance=dict(base_image_path=str(base_path),base_image_sha256=base_sha,base_image_bytes=base_bytes,
                expected_base_image_sha256=base_sha256,private_image_path=str(image_path),private_image_bytes=rootfs_bytes,base_filesystem_stats=filesystem_stats,
                runtime_input=data,runtime_input_identity=input_identity,removed_guest_entries=len(old),file_modes={guest:oct(after[guest]['mode']) for guest in sources},
                directory_modes={guest:oct(after[guest]['mode']) for guest in directories},method='private cp/reflink; bounded debugfs; exact guest dump hashes'))
        encoded = (json.dumps(output,indent=2,sort_keys=True)+'\n').encode()
        require(len(encoded) <= 1024*1024, 'output manifest cap exceeded')
        with os.fdopen(os.dup(manifest_fd),'wb') as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        return output
    except BaseException:
        for path,fd,created in reversed(owned):
            try:
                now = path.lstat()
                if (now.st_dev,now.st_ino) == (created.st_dev,created.st_ino):
                    path.unlink()
            except FileNotFoundError:
                pass
        raise
    finally:
        for _,fd,_ in reversed(owned):
            os.close(fd)
        os.close(base_fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-image',required=True)
    parser.add_argument('--base-sha256',required=True,help='explicit expected image digest; retain whether historical or freshly pinned before staging')
    parser.add_argument('--runtime-manifest',required=True)
    parser.add_argument('--output-image',required=True)
    parser.add_argument('--output-manifest',required=True)
    parser.add_argument('--timeout-seconds',type=int,default=1200)
    args = parser.parse_args()
    result = stage(args.base_image,args.runtime_manifest,args.output_image,args.output_manifest,args.timeout_seconds,base_sha256=args.base_sha256)
    manifest_sha = hashlib.sha256((json.dumps(result,indent=2,sort_keys=True)+'\n').encode()).hexdigest()
    print(json.dumps({'output_image':args.output_image,'output_manifest':args.output_manifest,
                      'rootfs_sha256':result['rootfs_sha256'],'runtime_manifest_sha256':manifest_sha,
                      'runtime_files':len(result['runtime_files'])}))


if __name__ == '__main__':
    main()
