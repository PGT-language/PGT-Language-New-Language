const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');

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

async function start(context) {
  client = new LanguageClient(
    'pgt',
    'PGT Language Server',
    serverOptions(),
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
