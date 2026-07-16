// esp-ot-gateway Web Flasher — app.js
// Fetches available firmware versions from GitHub Releases,
// builds manifest dynamically, and flashes via ESP Web Tools.

import 'https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module';

const REPO = 'sogimu/esp-ot-gateway';
const installButton = document.getElementById('install-button');
const versionSelect = document.getElementById('version-select');
const flashBtn = document.querySelector('.flash-btn');
const progressBar = document.getElementById('progress-bar');
const progressText = document.getElementById('progress-text');
const progressSection = document.getElementById('flash-progress');
const statusMsg = document.getElementById('flash-status');
const versionLoading = document.getElementById('version-loading');

// Fetch available firmware versions from GitHub Releases
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
        flashBtn.disabled = false;

        // Auto-select latest non-prerelease
        const latestStable = releases.find(r => !r.prerelease);
        versionSelect.value = latestStable ? latestStable.tag_name : releases[0].tag_name;

        await updateManifest(versionSelect.value);
        statusMsg.textContent = `${releases.length} versions available`;

    } catch (err) {
        versionLoading.textContent = 'Error loading';
        statusMsg.textContent = err.message;
        console.error('Failed to load releases:', err);
    }
}

// Find asset URL by filename
function findAssetUrl(release, filename) {
    const asset = release.assets.find(a => a.name.endsWith(filename));
    if (!asset) throw new Error(`Asset not found: ${filename}`);
    return asset.browser_download_url;
}

// Build manifest for selected version
async function updateManifest(tag) {
    try {
        const release = await fetch(
            `https://api.github.com/repos/${REPO}/releases/tags/${tag}`
        ).then(r => {
            if (!r.ok) throw new Error(`GitHub API: ${r.status}`);
            return r.json();
        });

        installButton.manifest = {
            name: 'ESP OpenTherm Gateway',
            version: tag,
            home_url: 'https://github.com/sogimu/esp-ot-gateway',
            builds: [{
                chipFamily: 'ESP32',
                flashMode: 'dio',
                flashSize: '2MB',
                flashFreq: '80m',
                parts: [
                    { path: findAssetUrl(release, 'bootloader.bin'),      offset: 4096  },
                    { path: findAssetUrl(release, 'partition-table.bin'), offset: 32768 },
                    { path: findAssetUrl(release, 'esp-ot-gateway.bin'),  offset: 65536 }
                ]
            }]
        };

        statusMsg.textContent = `Ready: ${tag}`;

    } catch (err) {
        statusMsg.textContent = err.message;
        console.error('Failed to build manifest:', err);
    }
}

// Version selection
versionSelect.addEventListener('change', async () => {
    flashBtn.disabled = true;
    await updateManifest(versionSelect.value);
    flashBtn.disabled = false;
});

// Flash events
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

// Start
loadVersions();
