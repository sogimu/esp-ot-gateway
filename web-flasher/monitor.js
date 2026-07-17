// esp-ot-gateway Serial Monitor
const connectBtn = document.getElementById('connect-btn');
const disconnectBtn = document.getElementById('disconnect-btn');
const monitorStatus = document.getElementById('monitor-status');
const serialOutput = document.getElementById('serial-output');

// ANSI to HTML color mapping (ESP-IDF log levels)
const ANSI_COLORS = {
    '0': 'color:inherit;font-weight:normal;font-style:normal',
    '1': 'font-weight:bold',
    '30': 'color:#000',
    '31': 'color:#f44',
    '32': 'color:#4f4',
    '33': 'color:#ff4',
    '34': 'color:#44f',
    '35': 'color:#f4f',
    '36': 'color:#4ff',
    '37': 'color:#fff',
};

function ansiToHtml(text) {
    let result = '';
    let i = 0;
    const len = text.length;
    while (i < len) {
        if (text[i] === '\x1b' && text[i+1] === '[') {
            const end = text.indexOf('m', i);
            if (end !== -1) {
                const code = text.slice(i+2, end);
                i = end + 1;
                if (code === '0' || code === '') {
                    result += '</span>';
                } else {
                    const style = ANSI_COLORS[code] || '';
                    result += `<span style="${style}">`;
                }
                continue;
            }
        }
        // Escape HTML
        if (text[i] === '<') result += '&lt;';
        else if (text[i] === '>') result += '&gt;';
        else if (text[i] === '&') result += '&amp;';
        else result += text[i];
        i++;
    }
    result += '</span>'.repeat((result.match(/<span/g)||[]).length - (result.match(/<\/span>/g)||[]).length);
    return result;
}

let port = null;
let reader = null;
let readLoop = null;

async function connect() {
    try {
        port = await navigator.serial.requestPort();
        await port.open({ baudRate: 115200 });

        connectBtn.style.display = 'none';
        disconnectBtn.style.display = 'inline-block';
        monitorStatus.textContent = 'Подключено 115200';

        serialOutput.textContent = '';
        readLoop = true;
        reader = port.readable.getReader();

        const decoder = new TextDecoder();
        let buffer = '';

        while (readLoop) {
            const { value, done } = await reader.read();
            if (done) break;
            buffer += decoder.decode(value, { stream: true });

            // Split into lines and display
            const lines = buffer.split('\n');
            buffer = lines.pop(); // incomplete line

            for (const line of lines) {
                serialOutput.innerHTML += ansiToHtml(line) + '\n';
            }
            serialOutput.scrollTop = serialOutput.scrollHeight;
        }
    } catch (err) {
        if (err.name !== 'AbortError') {
            monitorStatus.textContent = 'Ошибка: ' + err.message;
        }
        reset();
    }
}

async function disconnect() {
    readLoop = false;
    if (reader) {
        await reader.cancel().catch(() => {});
        reader = null;
    }
    if (port) {
        await port.close().catch(() => {});
        port = null;
    }
    reset();
}

function reset() {
    connectBtn.style.display = 'inline-block';
    disconnectBtn.style.display = 'none';
    monitorStatus.textContent = 'Отключено';
}

connectBtn.addEventListener('click', connect);
disconnectBtn.addEventListener('click', disconnect);
