#!/usr/bin/env node
console.log('Starting application...');
import { program } from 'commander';
import { startCommand } from './commands/start.js';
console.log('Imports completed');
async function main() {
    console.log('Main function started');
    process.on('uncaughtException', (err, origin) => {
        console.log(`Uncaught ${err.name}, Origin: ${origin}`);
        console.error(err.message);
        console.error(err.stack);
    });
    console.log('Setting up commander...');
    await program
        .name('one.finden')
        .version('0.1.0')
        .description('macOS Finder integration for ONE')
        .addCommand(startCommand, { isDefault: true })
        .parseAsync();
}
console.log('Calling main function...');
main().catch(err => {
    console.error('Fatal error:', err);
    process.exit(1);
});
//# sourceMappingURL=index.js.map