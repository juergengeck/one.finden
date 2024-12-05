import { Command } from 'commander';
import { existsSync } from 'fs';
import { join } from 'path';
import { homedir } from 'os';
import { execSync } from 'child_process';
import Replicant from '@refinio/one.leute.replicant/lib/Replicant.js';
import { OneFuseFS } from '../fuse/OneFuseFS.js';
function checkMacFUSE() {
    console.log('Checking macFUSE installation...');
    // Check if macFUSE is installed
    if (!existsSync('/usr/local/lib/libfuse.dylib')) {
        console.error('macFUSE is not installed. Please install it first:');
        console.error('brew install macfuse');
        process.exit(1);
    }
    console.log('✓ macFUSE library found');
    // Check if macFUSE kernel extension is loaded
    try {
        const kextOutput = execSync('kextstat | grep -i fuse', { encoding: 'utf8' });
        if (!kextOutput.includes('fuse')) {
            console.error('\nERROR: macFUSE kernel extension is not loaded.');
            console.error('\nPlease follow these steps:');
            console.error('1. Open System Settings');
            console.error('2. Go to Privacy & Security');
            console.error('3. Scroll down to Security section');
            console.error('4. Look for a message about "System software from developer "Benjamin Fleischer" was blocked"');
            console.error('5. Click "Allow" next to this message');
            console.error('6. Restart your computer');
            console.error('\nAfter completing these steps, try running this command again.');
            process.exit(1);
        }
        console.log('✓ macFUSE kernel extension is loaded');
    }
    catch (err) {
        console.error('\nERROR: Could not verify macFUSE kernel extension status.');
        console.error('Please ensure macFUSE is properly installed and allowed in System Settings.');
        process.exit(1);
    }
}
export const startCommand = new Command('start')
    .description('Start the ONE Finder integration')
    .option('-c, --config <path>', 'Path to config file')
    .option('-d, --directory <path>', 'Directory to mount', join(homedir(), 'ONE'))
    .option('--secret <secret>', 'Secret for ONE instance', 'dummy')
    .option('--debug', 'Enable debug logging')
    .action(async (options) => {
    console.log('Starting ONE Finder integration...');
    console.log('Options:', { ...options, secret: '***' });
    // Check macFUSE installation and permissions
    checkMacFUSE();
    try {
        // Initialize replicant
        console.log('Initializing replicant...');
        const replicant = new Replicant({});
        console.log('Starting replicant...');
        await replicant.start(options.secret);
        console.log('Replicant started successfully');
        // Get models from replicant
        console.log('Getting models from replicant...');
        const models = {
            channelManager: replicant.channelManager,
            connections: replicant.connections,
            leuteModel: replicant.leuteModel,
            notifications: replicant.notifications,
            topicModel: replicant.topicModel,
            iomManager: replicant.iomManager,
            journalModel: replicant.journalModel,
            questionnaireModel: replicant.questionnaires,
            consentModel: replicant.consentFile
        };
        console.log('Got models from replicant');
        // Initialize FUSE filesystem
        console.log('Initializing FUSE filesystem...');
        const fuseFS = new OneFuseFS({
            mountPath: options.directory,
            models,
            debug: options.debug
        });
        // Mount filesystem
        console.log('Mounting filesystem...');
        await fuseFS.mount();
        console.log('Filesystem mounted successfully');
        // Handle shutdown
        const cleanup = async () => {
            console.log('\nShutting down...');
            try {
                await fuseFS.shutdown();
                console.log('FUSE filesystem shutdown complete');
                await replicant.stop();
                console.log('Replicant shutdown complete');
                process.exit(0);
            }
            catch (err) {
                console.error('Error during shutdown:', err);
                process.exit(1);
            }
        };
        process.on('SIGINT', cleanup);
        process.on('SIGTERM', cleanup);
        process.on('uncaughtException', (err) => {
            console.error('Uncaught exception:', err);
            cleanup().catch(console.error);
        });
        // Keep process running
        console.log('ONE Finder integration is running');
        console.log(`Mounted at: ${options.directory}`);
        console.log('Press Ctrl+C to stop');
        process.stdin.resume();
    }
    catch (err) {
        console.error('Failed to start ONE Finder integration:', err);
        if (err instanceof Error) {
            console.error('Stack trace:', err.stack);
        }
        process.exit(1);
    }
});
//# sourceMappingURL=start.js.map