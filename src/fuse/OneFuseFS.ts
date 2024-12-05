import type { Stats } from 'fuse-native';
import Fuse from 'fuse-native';
import { join } from 'path';
import { homedir } from 'os';
import type { FilerConfig } from '@refinio/one.leute.replicant/lib/filer/FilerConfig.js';
import { Filer } from '@refinio/one.leute.replicant/lib/filer/Filer.js';
import type { FilerModels } from '@refinio/one.leute.replicant/lib/filer/Filer.js';

export interface OneFuseFSConfig {
    mountPath: string;
    models: FilerModels;
    debug?: boolean;
}

export class OneFuseFS {
    private fuse: Fuse | null = null;
    private filer: Filer | null = null;
    private readonly mountPath: string;
    private readonly models: FilerModels;
    private readonly debug: boolean;

    constructor(config: OneFuseFSConfig) {
        this.mountPath = config.mountPath;
        this.models = config.models;
        this.debug = config.debug ?? false;

        if (this.debug) {
            console.log('OneFuseFS initialized with config:', {
                mountPath: this.mountPath,
                debug: this.debug
            });
        }
    }

    async mount(): Promise<void> {
        if (this.debug) {
            console.log('OneFuseFS.mount() called');
        }

        // Create filer config
        const filerConfig = {
            logCalls: this.debug
        };

        if (this.debug) {
            console.log('Creating Filer with config:', filerConfig);
        }

        try {
            // Initialize filer
            this.filer = new Filer(this.models, filerConfig);
            if (this.debug) {
                console.log('Filer created successfully');
            }

            await this.filer.init();
            if (this.debug) {
                console.log('Filer initialized successfully');
            }

            // Get FUSE operations from filer
            const ops = (this.filer as any).fuseFrontend.ops;
            if (this.debug) {
                console.log('Got FUSE operations from filer');
                console.log('Available operations:', Object.keys(ops));
            }

            // Create and mount FUSE filesystem
            this.fuse = new Fuse(this.mountPath, ops, {
                debug: this.debug,
                force: true,
                mkdir: true
            });

            if (this.debug) {
                console.log('FUSE instance created');
            }

            return new Promise((resolve, reject) => {
                if (this.debug) {
                    console.log('Mounting FUSE filesystem...');
                }

                this.fuse!.mount(err => {
                    if (err) {
                        console.error('Failed to mount FUSE filesystem:', err);
                        reject(err);
                        return;
                    }
                    console.log(`Mounted ONE filesystem at ${this.mountPath}`);
                    resolve();
                });
            });
        } catch (err) {
            console.error('Error in OneFuseFS.mount():', err);
            if (err instanceof Error) {
                console.error('Stack trace:', err.stack);
            }
            throw err;
        }
    }

    async unmount(): Promise<void> {
        if (!this.fuse) {
            if (this.debug) {
                console.log('No FUSE instance to unmount');
            }
            return;
        }

        if (this.debug) {
            console.log('Unmounting FUSE filesystem...');
        }

        return new Promise((resolve, reject) => {
            this.fuse!.unmount(err => {
                if (err) {
                    console.error('Failed to unmount FUSE filesystem:', err);
                    reject(err);
                    return;
                }
                console.log(`Unmounted ONE filesystem from ${this.mountPath}`);
                resolve();
            });
        });
    }

    async shutdown(): Promise<void> {
        if (this.debug) {
            console.log('OneFuseFS.shutdown() called');
        }

        try {
            await this.unmount();
            if (this.filer) {
                if (this.debug) {
                    console.log('Shutting down Filer...');
                }
                await this.filer.shutdown();
                if (this.debug) {
                    console.log('Filer shutdown complete');
                }
            }
        } catch (err) {
            console.error('Error in OneFuseFS.shutdown():', err);
            if (err instanceof Error) {
                console.error('Stack trace:', err.stack);
            }
            throw err;
        }
    }
} 