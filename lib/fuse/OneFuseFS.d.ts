import type { FilerModels } from '@refinio/one.leute.replicant/lib/filer/Filer.js';
export interface OneFuseFSConfig {
    mountPath: string;
    models: FilerModels;
    debug?: boolean;
}
export declare class OneFuseFS {
    private fuse;
    private filer;
    private readonly mountPath;
    private readonly models;
    private readonly debug;
    constructor(config: OneFuseFSConfig);
    mount(): Promise<void>;
    unmount(): Promise<void>;
    shutdown(): Promise<void>;
}
