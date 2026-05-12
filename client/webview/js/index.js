/**
 * PIVOT NOTE: We removed the Wails imports.
 * Saucer automatically injects 'window.saucer' into the page.
 */
const saucer = window.saucer;

// DOM Elements
const overlay = document.getElementById('login-overlay');
const loginBtn = document.getElementById('login-btn');
const cancelBtn = document.getElementById('cancel-login');
const submitBtn = document.getElementById('submit-login');
const jidInput = document.getElementById('jid');
const passwordInput = document.getElementById('password');
const errorMsg = document.getElementById('login-error');
const logBox = document.getElementById('log-box');
const controlButtons = document.querySelectorAll('.control-grid button');

// Status elements
const statusBattery = document.getElementById('status-battery');
const statusSignal = document.getElementById('status-signal');
const statusSpeed = document.getElementById('status-speed');

function setControlsEnabled(enabled) {
    controlButtons.forEach(btn => {
        btn.disabled = !enabled;
    });
}

function addLog(time, message) {
    logBox.innerHTML += `<br/>[${time}] ${message}`;
    logBox.scrollTop = logBox.scrollHeight;
}

// make accessible from saucer
window.addLog = addLog;

const navbar = document.querySelector('.navbar');

navbar.addEventListener('mousedown', (e) => {

    console.log("mousedown")

    // Prevent dragging if the user clicks a button or a link inside the navbar
    if (e.target.tagName === 'BUTTON' || e.target.tagName === 'A' || e.target.closest('button')) {
        return;
    }

    // Call the Saucer native drag function
    // Note: Use the exact method name provided by your specific Saucer version (usually start_drag)
    if (window.saucer && window.saucer.startDrag) {
        console.log("start drag")
        window.saucer.startDrag();
    }
});

let devtoolsShown = false;

let debugBtn = document.getElementById('debug-btn');
debugBtn.addEventListener('click', () => {
    devtoolsShown = !devtoolsShown;
    window.saucer.exposed.toggleDevTools(devtoolsShown);
})

let isMaximized = false;

const minBtn = document.getElementById('min-btn');
const maxBtn = document.getElementById('max-btn');
const closeBtn = document.getElementById('close-btn');

minBtn.addEventListener('click', () => {
    window.saucer.minimize(true);
});

maxBtn.addEventListener('click', () => {
    isMaximized = !isMaximized;
    window.saucer.maximize(isMaximized);

    // Optional: Toggle symbol between '▢' and '❐'
    maxBtn.textContent = isMaximized ? '❐' : '▢';
});

closeBtn.addEventListener('click', () => {
    window.saucer.close();
});

// UI Handlers
loginBtn.addEventListener('click', () => {
    overlay.classList.add('show');
    jidInput.focus();
});

cancelBtn.addEventListener('click', () => {
    overlay.classList.remove('show');
    errorMsg.classList.remove('show');
});

/**
 * BINDINGS: Replaced Wails Go calls with saucer.exposed
 */
submitBtn.addEventListener('click', async () => {
    const jid = jidInput.value;
    const password = passwordInput.value;

    if (!jid || !password) {
        errorMsg.textContent = 'Please enter JID and password';
        errorMsg.classList.add('show');
        return;
    }

    submitBtn.disabled = true;
    submitBtn.textContent = 'Connecting...';

    try {
        // Call C++ function app.expose("Login", ...)
        await saucer.exposed.Login(jid, password);

        overlay.classList.remove('show');
        loginBtn.textContent = 'Connected';
        loginBtn.disabled = true;
        addLog(new Date().toLocaleTimeString(), 'Logging in as ' + jid);
        setControlsEnabled(true);
    } catch (err) {
        errorMsg.textContent = err;
        errorMsg.classList.add('show');
        submitBtn.disabled = false;
        submitBtn.textContent = 'Login';
    }
});

controlButtons.forEach(btn => {
    btn.addEventListener('click', async () => {
        const command = btn.dataset.command;
        if (!command) return;

        try {
            // Call C++ function app.expose("SendCommand", ...)
            await saucer.exposed.SendCommand(command);
        } catch (err) {
            addLog(new Date().toLocaleTimeString(), 'Error: ' + err);
        }
    });
});

/**
 * Status Update Functions
 * These are mapped to the status elements defined at the top of the file.
 */

// Update all status fields at once
function updateStatus(battery, signal, speed) {
    if (battery !== undefined) window.updateBattery(battery);
    if (signal !== undefined) window.updateSignal(signal);
    if (speed !== undefined) window.updateSpeed(speed);
}

// Update Battery: expects a number (e.g., 85)
function updateBattery(level) {
    statusBattery.textContent = `Battery: ${level}%`;

    // Optional: Add a visual warning if low
    if (level < 20) {
        statusBattery.style.color = '#ff4d4d';
    } else {
        statusBattery.style.color = '';
    }
}

// Update Signal: expects a string or level (e.g., "Excellent" or "4/5")
function updateSignal(strength) {
    statusSignal.textContent = `Signal: ${strength}`;
}

// Update Speed: expects a value (e.g., "1.2 m/s")
function updateSpeed(value) {
    statusSpeed.textContent = `Speed: ${value}`;
}

// Expose these to the window so Saucer/C++ can call them
window.updateStatus = updateStatus;
window.updateBattery = updateBattery;
window.updateSignal = updateSignal;
window.updateSpeed = updateSpeed;

function setCameraIframe(url) {
    const container = document.getElementById("camera-feed");
    if (!container) {
        console.error("Camera container not found: " + containerId);
        return;
    }

    // Look for an existing iframe or create a new one
    let iframe = container.querySelector('iframe');
    if (!iframe) {
        iframe = document.createElement('iframe');
        iframe.style.width = "100%";
        iframe.style.height = "100%";
        iframe.style.border = "none";
        iframe.setAttribute('allow', 'autoplay; encrypted-media');
        container.appendChild(iframe);
    }

    iframe.src = url;
}

window.setCameraIframe = setCameraIframe;

// Theme toggle (Standard JS, no changes needed)
const toggleButton = document.getElementById('theme-toggle');
const body = document.body;
const themeKey = 'theme';
const darkModeClass = 'dark-mode';

function setTheme(theme) {
    if (theme === 'dark') {
        body.classList.add(darkModeClass);
        localStorage.setItem(themeKey, 'dark');
    } else {
        body.classList.remove(darkModeClass);
        localStorage.setItem(themeKey, 'light');
    }
}

toggleButton.addEventListener('click', () => {
    body.classList.contains(darkModeClass) ? setTheme('light') : setTheme('dark');
});

const savedTheme = localStorage.getItem(themeKey);
if (savedTheme === 'dark' || (!savedTheme && window.matchMedia('(prefers-color-scheme: dark)').matches)) {
    setTheme('dark');
}