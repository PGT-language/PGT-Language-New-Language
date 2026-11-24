const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');
const { spawnSync } = require('child_process');

let client;

function serverOptions() {
  const config = vscode.workspace.getConfiguration('pgt');
  const command = config.get('compilerPath') || 'pgt';
  const env = Object.assign({}, process.env);
  const std = config.get('stdPath');
  if (std) {
    env.PGT_STD = std;
  }
  return { command, args: ['lsp'], options: { env } };
}

function checkCompiler(command) {
  const result = spawnSync(command, ['--help'], { encoding: 'utf8', timeout: 5000 });
  if (result.error) {
    return 'cannot run "' + command + '": ' + result.error.message;
  }
  const output = (result.stdout || '') + (result.stderr || '');
  if (!/\blsp\b/.test(output)) {
    return '"' + command + '" does not support "pgt lsp" (an old build?)';
  }
  return null;
}

async function start(context) {
  const options = serverOptions();
  const problem = checkCompiler(options.command);
  if (problem) {
    const action = await vscode.window.showErrorMessage(
      'PGT: ' + problem + '. Set "pgt.compilerPath" to a current pgt compiler.',
      'Open Settings'
    );
    if (action === 'Open Settings') {
      vscode.commands.executeCommand('workbench.action.openSettings', 'pgt.compilerPath');
    }
    return;
  }
  client = new LanguageClient(
    'pgt',
    'PGT Language Server',
    options,
    {
      documentSelector: [{ scheme: 'file', language: 'pgt' }],
      synchronize: {
        fileEvents: vscode.workspace.createFileSystemWatcher('**/{*.pgt,pgt.mod}')
      }
    }
  );
  try {
    await client.start();
  } catch (err) {
    vscode.window.showErrorMessage(
      'PGT: cannot start the language server. Check the "pgt.compilerPath" setting. ' + err.message
    );
  }
}

async function restart(context) {
  if (client) {
    await client.stop().catch(() => {});
    client = undefined;
  }
  await start(context);
}

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand('pgt.restartServer', () => restart(context)),
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (e.affectsConfiguration('pgt.compilerPath') || e.affectsConfiguration('pgt.stdPath')) {
        restart(context);
      }
    })
  );
  return start(context);
}

function deactivate() {
  return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
