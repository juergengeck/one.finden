declare module 'node-fuse-bindings' {
    export interface Stats {
        mtime: Date;
        atime: Date;
        ctime: Date;
        size: number;
        mode: number;
        uid: number;
        gid: number;
    }

    export interface FuseOptions {
        debug?: boolean;
        force?: boolean;
        mkdir?: boolean;
        displayFolder?: boolean;
    }

    export interface FuseOperations {
        readdir(path: string, cb: (err: number, files?: string[]) => void): void;
        getattr(path: string, cb: (err: number, stats?: Stats) => void): void;
        [key: string]: any;
    }

    export const ENOENT: number;

    export function mount(mountPath: string, operations: FuseOperations, options: FuseOptions, callback: (err?: Error) => void): void;
    export function unmount(mountPath: string, callback: (err?: Error) => void): void;

    const fuse: {
        ENOENT: number;
        mount: typeof mount;
        unmount: typeof unmount;
    };
    export default fuse;
} 