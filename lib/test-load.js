import { join } from 'path';
import { homedir } from 'os';
import { writeFileSync, mkdirSync } from 'fs';
import Fuse from 'fuse-native';
const logFile = join(homedir(), 'fuse-test.log');
function log(msg) {
    const timestamp = new Date().toISOString();
    const entry = `${timestamp} ${msg}\n`;
    console.log(entry.trim());
    writeFileSync(logFile, entry, { flag: 'a' });
}
log('Starting...');
try {
    const mountPath = join(homedir(), 'ONE');
    log('Mount path: ' + mountPath);
    // Ensure mount directory exists
    try {
        mkdirSync(mountPath, { recursive: true });
        log('Mount directory created successfully');
    }
    catch (err) {
        log('Failed to create mount directory: ' + err);
        process.exit(1);
    }
    // Basic FUSE operations
    const ops = {
        readdir(path, cb) {
            log('readdir called: ' + path);
            cb(0, ['.', '..']);
        },
        getattr(path, cb) {
            log('getattr called: ' + path);
            if (path === '/') {
                cb(0, {
                    mtime: new Date(),
                    atime: new Date(),
                    ctime: new Date(),
                    size: 100,
                    mode: 16877, // directory
                    uid: process.getuid?.() ?? 0,
                    gid: process.getgid?.() ?? 0
                });
            }
            else {
                cb(Fuse.ENOENT);
            }
        }
    };
    log('Creating FUSE instance...');
    const fuseFS = new Fuse(mountPath, ops, {
        debug: true,
        force: true,
        mkdir: true
    });
    log('Mounting FUSE filesystem...');
    fuseFS.mount((err) => {
        if (err) {
            log('Mount error: ' + err);
            process.exit(1);
        }
        log('Mount successful!');
    });
    // Handle cleanup
    const cleanup = () => {
        log('Cleanup called');
        fuseFS.unmount((err) => {
            if (err) {
                log('Unmount error: ' + err);
                process.exit(1);
            }
            log('Unmount successful');
            process.exit(0);
        });
    };
    process.on('SIGINT', () => {
        log('Received SIGINT');
        cleanup();
    });
    process.on('SIGTERM', () => {
        log('Received SIGTERM');
        cleanup();
    });
    // Keep process alive
    setInterval(() => {
        log('Heartbeat');
    }, 5000);
}
catch (err) {
    log('Fatal error: ' + err);
    if (err instanceof Error) {
        log('Error stack: ' + err.stack);
    }
    process.exit(1);
}
//# sourceMappingURL=test-load.js.map