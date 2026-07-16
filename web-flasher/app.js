// esp-ot-gateway Web Flasher — app.js
import 'https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module';

const REPO = 'sogimu/esp-ot-gateway';
const versionSelect = document.getElementById('version-select');
const buttonContainer = document.getElementById('button-container');
const progressBar = document.getElementById('progress-bar');
const progressText = document.getElementById('progress-text');
const progressSection = document.getElementById('flash-progress');
const statusMsg = document.getElementById('flash-status');
const versionLoading = document.getElementById('version-loading');

let installButton = null;
let manifestBlobUrl = null;

// Resolve GitHub API asset URL to S3 redirect target (S3 has CORS)
async function resolveAssetUrl(assetId) {
    const apiUrl = `https://api.github.com/repos/${REPO}/releases/assets/${assetId}`;
    const resp = await fetch(apiUrl, {
        headers: { 'Accept': 'application/octet-stream' }
    });
    if (!resp.ok) throw new Error(`Asset ${assetId}: ${resp.status}`);
    return resp.url; // final S3 URL after redirects — has CORS
}

async function buildParts(release) {
    const files = ['bootloader.bin', 'partition-table.bin', 'esp-ot-gateway.bin'];
    const offsets = [4096, 32768, 65536];
    const parts = [];
    for (let i = 0; i < files.length; i++) {
        const asset = release.assets.find(a => a.name.endsWith(files[i]));
        if (!asset) throw new Error(`Asset not found: ${files[i]}`);
        const url = await resolveAssetUrl(asset.id);
        parts.push({ path: url, offset: offsets[i] });
    }
    return parts;
}

// Build manifest, create ESP Web Tools button, inject into page
async function updateManifest(tag) {
    // Clean up previous
    if (manifestBlobUrl) URL.revokeObjectURL(manifestBlobUrl);
    if (installButton) {
        installButton.remove();
        installButton = null;
    }

    const release = await fetch(
        `https://api.github.com/repos/${REPO}/releases/tags/${tag}`
    ).then(r => {
        if (!r.ok) throw new Error(`GitHub API: ${r.status}`);
        return r.json();
    });

    const parts = await buildParts(release);

    const manifest = {
        name: 'ESP OpenTherm Gateway',
        version: tag,
        home_url: 'https://github.com/sogimu/esp-ot-gateway',
        builds: [{
            chipFamily: 'ESP32',
            flashMode: 'dio',
            flashSize: '2MB',
            flashFreq: '80m',
            parts
        }]
    };

    const blob = new Blob([JSON.stringify(manifest)], { type: 'application/json' });
    manifestBlobUrl = URL.createObjectURL(blob);

    // Create button with manifest set BEFORE it initializes
    installButton = document.createElement('esp-web-install-button');
    installButton.setAttribute('manifest', manifestBlobUrl);
    installButton.innerHTML = `
        <button slot="activate" class="flash-btn">Подключить и прошить</button>
        <span slot="unsupported">
            Ваш браузер не поддерживает WebSerial.<br>
            Нужен <strong>Google Chrome</strong>, Edge или Opera.<br>
            <small>Chromium на Linux — включите флаг:<br>
            <code>chrome://flags/#enable-experimental-web-platform-features</code> → Enabled</small>
        </span>
        <span slot="not-allowed">Разрешите доступ к последовательному порту в диалоге браузера.</span>
    `;

    // Events
    installButton.addEventListener('flash-progress', (e) => {
        progressSection.style.display = 'block';
        progressBar.value = e.detail.percentage;
        progressText.textContent = e.detail.message || 'Flashing...';
    });

    installButton.addEventListener('flash-complete', () => {
        progressText.textContent = 'Flashing complete!';
        statusMsg.textContent = 'Device rebooting. Connect to ot-gateway-setup-XXXXXX WiFi for setup.';
    });

    installButton.addEventListener('flash-error', (e) => {
        progressText.textContent = 'Flash error';
        statusMsg.textContent = 'Error: ' + (e.detail.message || 'unknown');
    });

    buttonContainer.innerHTML = '';
    buttonContainer.appendChild(installButton);
    statusMsg.textContent = `Ready: ${tag}`;
}

// Load available versions from GitHub Releases
async function loadVersions() {
    try {
        const releases = await fetch(
            `https://api.github.com/repos/${REPO}/releases?per_page=20`
        ).then(r => {
            if (!r.ok) throw new Error(`GitHub API: ${r.status}`);
            return r.json();
        });

        versionSelect.innerHTML = '';

        if (releases.length === 0) {
            versionSelect.innerHTML = '<option>No releases found</option>';
            versionLoading.textContent = 'No releases found';
            return;
        }

        releases.forEach(release => {
            const option = document.createElement('option');
            option.value = release.tag_name;
            const date = release.published_at.slice(0, 10);
            const prerelease = release.prerelease ? ' [pre]' : '';
            option.textContent = `${release.tag_name} — ${date}${prerelease}`;
            versionSelect.appendChild(option);
        });

        versionSelect.disabled = false;
        versionLoading.textContent = '';

        // Auto-select latest non-prerelease
        const latestStable = releases.find(r => !r.prerelease);
        versionSelect.value = latestStable ? latestStable.tag_name : releases[0].tag_name;

        await updateManifest(versionSelect.value);
        statusMsg.textContent = `${releases.length} versions available`;

    } catch (err) {
        versionLoading.textContent = 'Error loading';
        statusMsg.textContent = err.message;
        console.error(err);
    }
}

// Version selection
versionSelect.addEventListener('change', () => {
    updateManifest(versionSelect.value).catch(err => {
        statusMsg.textContent = err.message;
    });
});

loadVersions();
