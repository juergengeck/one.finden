declare module 'fuse-native' {
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

    export class Fuse {
        static ENOENT: number;
        
        constructor(mountPath: string, operations: FuseOperations, options?: FuseOptions);
        mount(cb: (err?: Error) => void): void;
        unmount(cb: (err?: Error) => void): void;
    }

    export default Fuse;
} 