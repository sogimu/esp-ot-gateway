// esp-ot-gateway Serial Monitor
const connectBtn = document.getElementById('connect-btn');
const disconnectBtn = document.getElementById('disconnect-btn');
const resetBtn = document.getElementById('reset-btn');
const monitorStatus = document.getElementById('monitor-status');
const serialOutput = document.getElementById('serial-output');

let port = null;
let reader = null;
let readLoop = null;

async function connect() {
    try {
        port = await navigator.serial.requestPort();
        await port.open({ baudRate: 115200 });

        connectBtn.style.display = 'none';
        disconnectBtn.style.display = 'inline-block';
        resetBtn.style.display = 'inline-block';
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
                serialOutput.textContent += line + '\n';
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

async function resetDevice() {
    if (!port) return;
    try {
        // Pull RTS low → EN low → chip in reset
        await port.setSignals({ requestToSend: false });
        await new Promise(r => setTimeout(r, 100));
        // RTS high → EN high → chip boots
        await port.setSignals({ requestToSend: true });
        monitorStatus.textContent = 'Сброс…';
        setTimeout(() => { monitorStatus.textContent = 'Подключено 115200'; }, 500);
    } catch (err) {
        monitorStatus.textContent = 'Ошибка сброса: ' + err.message;
    }
}

function reset() {
    connectBtn.style.display = 'inline-block';
    disconnectBtn.style.display = 'none';
    resetBtn.style.display = 'none';
    monitorStatus.textContent = 'Отключено';
}

connectBtn.addEventListener('click', connect);
disconnectBtn.addEventListener('click', disconnect);
resetBtn.addEventListener('click', resetDevice);
