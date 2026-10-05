// esp-ot-gateway Web Flasher — app.js
// Versions list from GitHub Release API, binaries from same-origin Pages.
import 'https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module';

const REPO = 'sogimu/esp-ot-gateway';
const versionSelect = document.getElementById('version-select');
const buttonContainer = document.getElementById('button-container');
const progressBar = document.getElementById('progress-bar');
const progressText = document.getElementById('progress-text');
const progressSection = document.getElementById('flash-progress');
const statusMsg = document.getElementById('flash-status');
const versionLoading = document.getElementById('version-loading');
const releaseNotes = document.getElementById('release-notes');
const releaseBody = document.getElementById('release-body');

let installButton = null;
let sortedReleases = [];

function isTestTag(tag) {
    return /-rc\d+$/i.test(tag);
}

// Источник правды о канале — суффикс тега; флаг GitHub API учитываем тоже
// (на случай вручную снятого флага у старого релиза).
function isTest(release) {
    return !!release.prerelease || isTestTag(release.tag_name);
}

function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, c => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[c]));
}

function semverParts(tag) {
    const m = /^v?(\d+)\.(\d+)\.(\d+)(?:-(.+))?$/.exec(tag);
    if (!m) return null;
    return { major: +m[1], minor: +m[2], patch: +m[3], pre: m[4] || '' };
}

// Убывание: v0.10.0 перед v0.9.0, стабильная перед rc той же версии.
function compareSemverDesc(a, b) {
    const pa = semverParts(a), pb = semverParts(b);
    if (!pa && !pb) return 0;
    if (!pa) return 1;
    if (!pb) return -1;
    if (pa.major !== pb.major) return pb.major - pa.major;
    if (pa.minor !== pb.minor) return pb.minor - pa.minor;
    if (pa.patch !== pb.patch) return pb.patch - pa.patch;
    if (!pa.pre && pb.pre) return -1;
    if (pa.pre && !pb.pre) return 1;
    return pb.pre.localeCompare(pa.pre, undefined, { numeric: true });
}

function sortReleases(list) {
    return list.slice().sort((a, b) => {
        const at = isTest(a), bt = isTest(b);
        if (at !== bt) return at ? 1 : -1;
        return compareSemverDesc(a.tag_name, b.tag_name);
    });
}

function releaseOption(release) {
    const date = release.published_at ? release.published_at.slice(0, 10) : '';
    const label = isTestTag(release.tag_name) ? ' (тестовая)' : '';
    return `<option value="${escapeHtml(release.tag_name)}">${escapeHtml(release.tag_name)} — ${escapeHtml(date)}${label}</option>`;
}

async function manifestExists(tag) {
    try {
        const r = await fetch(`firmware/${tag}/manifest.json`, { method: 'HEAD' });
        return r.ok;
    } catch {
        return false;
    }
}

function createButton(tag) {
    if (installButton) {
        installButton.remove();
        installButton = null;
    }

    const manifestUrl = `firmware/${tag}/manifest.json`;

    installButton = document.createElement('esp-web-install-button');
    installButton.setAttribute('manifest', manifestUrl);
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

    buttonContainer.innerHTML = '';
    buttonContainer.appendChild(installButton);

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
}

async function updateManifest(tag) {
    createButton(tag);
    releaseNotes.href = `https://github.com/sogimu/esp-ot-gateway/releases/tag/${tag}`;
    statusMsg.textContent = `Ready: ${tag}`;

    // Fetch and show release notes
    try {
        const release = await fetch(
            `https://api.github.com/repos/${REPO}/releases/tags/${tag}`
        ).then(r => r.ok ? r.json() : null);
        if (release && release.body) {
            releaseBody.textContent = release.body;
        } else {
            releaseBody.textContent = '';
        }
    } catch {
        releaseBody.textContent = '';
    }
}

// Выбирает версию только если её манифест есть на Pages. При авто-выборе
// откатывается к следующей стабильной с манифестом; при явном — показывает
// ошибку и не включает прошивку.
async function selectVersion(tag, { auto = false } = {}) {
    if (await manifestExists(tag)) {
        versionSelect.value = tag;
        await updateManifest(tag);
        return true;
    }

    if (auto) {
        const start = sortedReleases.findIndex(r => r.tag_name === tag);
        for (let i = start + 1; i < sortedReleases.length; i++) {
            const candidate = sortedReleases[i];
            if (isTest(candidate)) continue;
            if (await manifestExists(candidate.tag_name)) {
                versionSelect.value = candidate.tag_name;
                await updateManifest(candidate.tag_name);
                statusMsg.textContent = `${tag} не опубликована на Pages, выбрана ${candidate.tag_name}`;
                return true;
            }
        }
    }

    if (installButton) {
        installButton.remove();
        installButton = null;
    }
    buttonContainer.innerHTML = '';
    statusMsg.textContent = `Версия ${tag}: manifest.json не найден на Pages (HTTP 404)`;
    return false;
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

        sortedReleases = sortReleases(releases);
        const stable = sortedReleases.filter(r => !isTest(r));
        const test = sortedReleases.filter(r => isTest(r));

        let html = stable.map(releaseOption).join('');
        if (test.length) {
            html += `<optgroup label="Тестовые (на свой риск)">${test.map(releaseOption).join('')}</optgroup>`;
        }
        versionSelect.innerHTML = html;

        versionSelect.disabled = false;
        versionLoading.textContent = '';

        // Deep-link ?tag=<tag>: преселект, включая тестовую версию.
        const wanted = new URLSearchParams(location.search).get('tag');
        const wantedExists = !!wanted && sortedReleases.some(r => r.tag_name === wanted);
        const defaultStable = stable[0];

        if (!wantedExists && !defaultStable) {
            versionSelect.insertAdjacentHTML('afterbegin', '<option value="" disabled>— выберите версию —</option>');
            versionSelect.value = '';
            statusMsg.textContent = 'Нет стабильных версий — выберите тестовую вручную';
            return;
        }

        const initialTag = wantedExists ? wanted : defaultStable.tag_name;
        await selectVersion(initialTag, { auto: !wantedExists });
    } catch (err) {
        versionLoading.textContent = 'Error loading';
        statusMsg.textContent = err.message;
        console.error(err);
    }
}

versionSelect.addEventListener('change', () => {
    selectVersion(versionSelect.value).catch(err => {
        statusMsg.textContent = err.message;
    });
});

loadVersions();
