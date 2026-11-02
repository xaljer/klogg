const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const STARTUP_RE = /listening on (https?:\/\/(?:\[[^\]]+\]|[^:\s/]+):\d+)\s*$/;

function parseServerUrl(line) {
  const match = STARTUP_RE.exec(String(line).trim());
  if (!match) {
    return null;
  }
  try {
    const url = new URL(match[1]);
    if (!['http:', 'https:'].includes(url.protocol) || !url.port) {
      return null;
    }
    return url.origin;
  } catch {
    return null;
  }
}

function localServerUrl(serverUrl) {
  const url = new URL(serverUrl);
  const host = url.hostname;
  if (host === '0.0.0.0' || host === '[::]' || host === '::') {
    url.hostname = '127.0.0.1';
  }
  return url.origin;
}

function redactServerArgs(args) {
  return args.map((arg, index) => (args[index - 1] === '--token' ? '<redacted>' : arg));
}

function buildServerArgs({ webRoot, listenAddress = '127.0.0.1', port = 0, token = '' }) {
  const args = [
    '--port',
    String(port),
    '--listen',
    String(listenAddress),
  ];
  if (token) {
    args.push('--token', String(token));
  }
  args.push('--web-root', String(webRoot));
  return args;
}

function appendLog(logFile, chunk) {
  if (!logFile) {
    return;
  }
  try {
    fs.mkdirSync(path.dirname(logFile), { recursive: true });
    fs.appendFileSync(logFile, chunk);
  } catch {
    // Logging must never prevent the app from starting.
  }
}

function createServerManager({ serverPath, webRoot, logFile = '' }) {
  let child = null;
  let startupLines = [];
  let stderrText = '';
  let stopped = false;
  let exitHandler = null;

  function stop() {
    const current = child;
    if (!current) {
      return Promise.resolve();
    }
    child = null;
    stopped = true;

    return new Promise((resolve) => {
      let settled = false;
      let forceTimer = null;
      const finish = () => {
        if (settled) {
          return;
        }
        settled = true;
        if (forceTimer) {
          clearTimeout(forceTimer);
        }
        resolve();
      };

      current.once('exit', finish);
      current.once('error', finish);

      if (process.platform === 'win32') {
        const killer = spawn('taskkill', ['/PID', String(current.pid), '/T', '/F'], {
          windowsHide: true,
          stdio: 'ignore',
        });
        killer.once('error', () => {
          try {
            current.kill();
          } catch {
            // The process already exited.
          }
        });
      } else {
        try {
          current.kill('SIGTERM');
        } catch {
          finish();
        }
      }

      forceTimer = setTimeout(finish, 3000);
      forceTimer.unref?.();
    });
  }

  async function start(options = {}) {
    if (child) {
      throw new Error('server is already running');
    }
    stopped = false;
    startupLines = [];
    stderrText = '';

    const args = buildServerArgs({ ...options, webRoot });
    const processChild = spawn(serverPath, args, {
      windowsHide: true,
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    child = processChild;

    if (logFile) {
      appendLog(
        logFile,
        `\n[${new Date().toISOString()}] ${serverPath} ${redactServerArgs(args).join(' ')}\n`,
      );
    }

    return new Promise((resolve, reject) => {
      let settled = false;
      let timer = null;

      const fail = (error) => {
        if (settled) {
          return;
        }
        settled = true;
        if (timer) {
          clearTimeout(timer);
        }
        reject(error);
      };

      const lineBuffer = { value: '' };
      processChild.stdout.setEncoding('utf8');
      processChild.stdout.on('data', (chunk) => {
        if (logFile) {
          appendLog(logFile, chunk);
        }
        lineBuffer.value += chunk;
        const lines = lineBuffer.value.split(/\r?\n/);
        lineBuffer.value = lines.pop() ?? '';
        for (const line of lines) {
          startupLines.push(line);
          const url = parseServerUrl(line);
          if (url && !settled) {
            settled = true;
            if (timer) {
              clearTimeout(timer);
            }
            resolve(url);
          }
        }
      });

      processChild.stderr.setEncoding('utf8');
      processChild.stderr.on('data', (chunk) => {
        stderrText += chunk;
        if (stderrText.length > 32000) {
          stderrText = stderrText.slice(-32000);
        }
        if (logFile) {
          appendLog(logFile, chunk);
        }
      });

      processChild.once('error', (error) => {
        child = null;
        fail(error);
      });

      processChild.once('exit', (code, signal) => {
        if (child === processChild) {
          child = null;
        }
        const detail = stderrText.trim() || startupLines.join('\n').trim();
        if (!settled) {
          fail(
            new Error(
              `klogg_server exited before listening (code=${code ?? 'null'}, signal=${
                signal ?? 'null'
              })${detail ? `: ${detail}` : ''}`,
            ),
          );
        }
        if (stopped) {
          stopped = false;
        }
        if (exitHandler) {
          exitHandler(code, signal, detail);
        }
      });

      timer = setTimeout(() => {
        fail(new Error(`timed out waiting for klogg_server at ${serverPath}`));
      }, 15000);
    });
  }

  return {
    start,
    stop,
    get pid() {
      return child?.pid ?? 0;
    },
    onExit(handler) {
      exitHandler = handler;
    },
  };
}

module.exports = {
  buildServerArgs,
  createServerManager,
  localServerUrl,
  parseServerUrl,
  redactServerArgs,
};
