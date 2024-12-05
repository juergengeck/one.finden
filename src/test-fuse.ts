import Fuse from 'fuse-native';
import { join } from 'path';
import { homedir } from 'os';
import { mkdirSync, writeFileSync } from 'fs';

// Synchronous logging to a file
const logFile = join(homedir(), 'fuse-test.log');
function log(msg: string) {
    const timestamp = new Date().toISOString();
    const entry = `${timestamp} ${msg}\n`;
    console.log(entry.trim());
    writeFileSync(logFile, entry, { flag: 'a' });
}

log('Starting application...');

// Set up error handlers first
process.on('uncaughtException', (err) => {
    log(`Uncaught exception: ${err}`);
    if (err instanceof Error) {
        log(`Stack trace: ${err.stack}`);
    }
    process.exit(1);
});

process.on('unhandledRejection', (err) => {
    log(`Unhandled rejection: ${err}`);
    process.exit(1);
});

// Ensure mount directory exists
const mountPath = join(homedir(), 'ONE');
try {
    log('Creating mount directory...');
    mkdirSync(mountPath, { recursive: true });
    log('Mount directory created successfully');
} catch (err) {
    log(`Failed to create mount directory: ${err}`);
    process.exit(1);
}

// Basic FUSE operations - minimal version
const ops = {
    readdir(path: string, cb: (code: number, entries?: string[]) => void) {
        log(`readdir called: ${path}`);
        cb(0, ['.', '..']);
    },
    getattr(path: string, cb: (code: number, stats?: any) => void) {
        log(`getattr called: ${path}`);
        if (path === '/') {
            cb(0, {
                mtime: new Date(),
                atime: new Date(),
                ctime: new Date(),
                size: 100,
                mode: 16877,
                uid: process.getuid?.() ?? 0,
                gid: process.getgid?.() ?? 0
            });
        } else {
            cb(Fuse.ENOENT);
        }
    }
};

try {
    log('Creating FUSE instance...');
    const fuse = new Fuse(mountPath, ops, {
        debug: true,
        force: true,
        mkdir: true
    });

    log('FUSE instance created, attempting to mount...');
    fuse.mount(err => {
        if (err) {
            log(`Failed to mount filesystem: ${err}`);
            process.exit(1);
        }
        log('Filesystem mounted successfully');
    });

    // Handle cleanup
    const cleanup = () => {
        log('Cleanup called, unmounting...');
        fuse.unmount(err => {
            if (err) {
                log(`Failed to unmount: ${err}`);
                process.exit(1);
            }
            log('Unmounted successfully');
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
    log('Setting up heartbeat...');
    setInterval(() => {
        log('Heartbeat');
    }, 5000);

} catch (err) {
    log(`Fatal error: ${err}`);
    if (err instanceof Error) {
        log(`Stack trace: ${err.stack}`);
    }
    process.exit(1);
} 