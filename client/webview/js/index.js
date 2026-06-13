/**
 * Frontend control script for the PICSES Helelani Rover UI.
 *
 * This file manages the browser-side behavior of the Saucer webview interface:
 * - Login dialog handling
 * - Native window controls
 * - Theme switching
 * - Rover connectivity status UI
 * - Telemetry display updates
 * - Camera iframe updates
 * - Dynamic rover command buttons
 * - Calls into C++ through `window.saucer.exposed`
 *
 * C++ also calls several functions exposed on `window`, including:
 * - `addLog(...)`
 * - `clearControlButtons()`
 * - `addControlButton(...)`
 * - `markRoverWaiting()`
 * - `markRoverReachable()`
 * - `markRoverUnreachable()`
 * - `updateBattery(...)`
 * - `updateSignal(...)`
 * - `updateSpeed(...)`
 * - `setCameraIframe(...)`
 */
const saucer = window.saucer;

// DOM Elements
const overlay = document.getElementById("login-overlay");
const loginBtn = document.getElementById("login-btn");
const cancelBtn = document.getElementById("cancel-login");
const submitBtn = document.getElementById("submit-login");
const jidInput = document.getElementById("jid");
const passwordInput = document.getElementById("password");
const errorMsg = document.getElementById("login-error");
const logBox = document.getElementById("log-box");
const controlButtons = document.querySelectorAll(".control-grid button");

// Telemetry elements
const telemetryList = document.getElementById("telemetry-list");
const telemetryRows = new Map();
const telemetryState = new Map();

/**
 * Enables or disables all rover control buttons.
 *
 * This is used while the user is disconnected, while the application is waiting
 * for the rover handshake, and when telemetry timeout marks the rover as
 * unreachable.
 *
 * @param {boolean} enabled Whether rover control buttons should be enabled.
 * @returns {void}
 */
function setControlsEnabled(enabled) {
	const buttons = document.querySelectorAll(".control-grid button");
	buttons.forEach((btn) => {
		btn.disabled = !enabled;
	});
}

/**
 * Gets the rover warning overlay, creating it if it does not already exist.
 *
 * The overlay is used to show blocking rover-state messages such as "Waiting for
 * Rover" or "Rover Unreachable". It is created dynamically so the HTML does not
 * need to contain the warning markup up front.
 *
 * @returns {HTMLDivElement} The rover warning overlay element.
 */
function getOrCreateRoverWarningOverlay() {
	let warningOverlay = document.getElementById("rover-warning-overlay");

	if (warningOverlay) {
		return warningOverlay;
	}

	warningOverlay = document.createElement("div");
	warningOverlay.id = "rover-warning-overlay";
	warningOverlay.style.position = "fixed";
	warningOverlay.style.inset = "0";
	warningOverlay.style.background = "rgba(0, 0, 0, 0.55)";
	warningOverlay.style.zIndex = "999";
	warningOverlay.style.display = "none";
	warningOverlay.style.alignItems = "center";
	warningOverlay.style.justifyContent = "center";
	warningOverlay.style.pointerEvents = "none";

	const warningBox = document.createElement("div");
	warningBox.style.maxWidth = "520px";
	warningBox.style.padding = "24px 28px";
	warningBox.style.borderRadius = "12px";
	warningBox.style.background = "#2b1d1d";
	warningBox.style.color = "#ffffff";
	warningBox.style.boxShadow = "0 12px 40px rgba(0, 0, 0, 0.35)";
	warningBox.style.border = "1px solid #ff6b6b";
	warningBox.style.textAlign = "center";

	const title = document.createElement("h2");
	title.id = "rover-warning-title";
	title.textContent = "Rover Unavailable";
	title.style.marginTop = "0";

	const message = document.createElement("p");
	message.id = "rover-warning-message";
	message.textContent = "Waiting for the rover to become reachable...";
	message.style.marginBottom = "0";

	warningBox.appendChild(title);
	warningBox.appendChild(message);
	warningOverlay.appendChild(warningBox);
	document.body.appendChild(warningOverlay);

	return warningOverlay;
}

/**
 * Displays the rover warning overlay with the provided title and message.
 *
 * This function is exposed on `window` so native C++ code can update the UI when
 * the rover is waiting, unreachable, or otherwise unavailable.
 *
 * @param {string} [title="Rover Unavailable"] Warning title to display.
 * @param {string} [message="Waiting for the rover to become reachable..."] Warning body text.
 * @returns {void}
 */
function showRoverWarning(
	title = "Rover Unavailable",
	message = "Waiting for the rover to become reachable...",
) {
	const warningOverlay = getOrCreateRoverWarningOverlay();
	const warningTitle = document.getElementById("rover-warning-title");
	const warningMessage = document.getElementById("rover-warning-message");

	warningTitle.textContent = title;
	warningMessage.textContent = message;
	warningOverlay.style.display = "flex";
}

/**
 * Hides the rover warning overlay if it exists.
 *
 * This function is exposed on `window` so C++ can clear the warning after the
 * rover options handshake succeeds.
 *
 * @returns {void}
 */
function hideRoverWarning() {
	const warningOverlay = document.getElementById("rover-warning-overlay");

	if (warningOverlay) {
		warningOverlay.style.display = "none";
	}
}

/**
 * Resets displayed telemetry fields to their unknown/default values.
 *
 * Called when waiting for the rover or when the rover becomes unreachable.
 *
 * @returns {void}
 */
function resetStatusFields() {
	if (!telemetryList) {
		console.error("Telemetry container not found");
		return;
	}

	telemetryList.innerHTML = "";
	telemetryRows.clear();
	telemetryState.clear();
}

/**
 * Removes the current camera iframe from the camera feed container.
 *
 * This clears stale video when the rover is not currently reachable.
 *
 * @returns {void}
 */
function clearCameraIframe() {
	const container = document.getElementById("camera-feed");
	if (!container) {
		console.error("Camera container not found");
		return;
	}

	const iframe = container.querySelector("iframe");
	if (iframe) {
		iframe.remove();
	}
}

/**
 * Disables UI elements and resets rover-related state.
 *
 * This function is used to prepare the UI for waiting for the rover or when the rover becomes unreachable.
 *
 * @returns {void}
 */
function disableUI() {
	loginBtn.textContent = "Waiting for Rover...";
	loginBtn.disabled = true;

	submitBtn.disabled = false;
	submitBtn.textContent = "Login";

	setControlsEnabled(false);
	clearControlButtons();
	resetStatusFields();
	clearCameraIframe();
}

/**
 * Updates the UI to indicate that XMPP is connected but the rover handshake has
 * not completed yet.
 *
 * This is called by C++ after login succeeds and before rover options have been
 * fetched. It disables command controls, clears stale rover UI, and shows a
 * waiting overlay.
 *
 * @returns {void}
 */
function markRoverWaiting() {
	disableUI();

	showRoverWarning(
		"Waiting for Rover",
		"Connected to XMPP. Waiting for the rover to come online...",
	);
}

/**
 * Updates the UI to indicate that the rover is reachable and ready for commands.
 *
 * This is called by C++ after the rover options handshake succeeds.
 *
 * @returns {void}
 */
function markRoverReachable() {
	loginBtn.textContent = "Connected";
	loginBtn.disabled = true;

	hideRoverWarning();
	setControlsEnabled(true);
}

/**
 * Updates the UI to indicate that the rover has become unreachable.
 *
 * This is called by C++ when telemetry has not been received within the timeout
 * window. It disables controls, clears stale camera/telemetry data, shows a
 * warning overlay, and logs the event.
 *
 * @returns {void}
 */
function markRoverUnreachable() {
	disableUI();

	showRoverWarning(
		"Rover Unreachable",
		"Telemetry has stopped. Waiting for the rover to come back online...",
	);

	addLog(new Date().toLocaleTimeString(), "Rover is unreachable");
}

/**
 * Appends a timestamped message to the UI log.
 *
 * This function is exposed on `window` so C++ can log connection status,
 * handshake status, server details, command results, and telemetry diagnostics.
 *
 * @param {string} time Human-readable timestamp to display.
 * @param {string} message Log message to append.
 * @returns {void}
 */
function addLog(time, message) {
	logBox.innerHTML += `<br/>[${time}] ${message}`;
	logBox.scrollTop = logBox.scrollHeight;
}

// make accessible from saucer
window.addLog = addLog;

/**
 * Removes all dynamically generated rover control buttons.
 *
 * This function is exposed on `window` and called by C++ before repopulating the
 * command grid after a successful rover options handshake.
 *
 * @returns {void}
 */
function clearControlButtons() {
	const controlGrid = document.getElementById("control-grid");
	if (!controlGrid) {
		console.error("Control grid container not found");
		return;
	}
	controlGrid.innerHTML = "";
}

function addControlButtonToControlGrid(commandId, commandName, controlGrid) {
	const button = document.createElement("button");
	button.dataset.command = commandId;
	button.textContent = commandName;
	button.disabled = false;

	button.addEventListener("click", async () => {
		const command = button.dataset.command;
		if (!command) return;

		try {
			await saucer.exposed.SendCommand(command);
		} catch (err) {
			addLog(new Date().toLocaleTimeString(), `Error: ${err}`);
		}
	});

	controlGrid.appendChild(button);
}

/**
 * Adds a single rover command button to the control grid.
 *
 * The created button calls the C++ `SendCommand` binding with the command ID
 * when clicked. C++ generates these buttons from the command list returned by
 * the rover options handshake.
 *
 * @param {string} commandId Command identifier to send to the rover.
 * @param {string} commandName Human-readable label shown on the button.
 * @returns {void}
 */
function addControlButton(commandId, commandName) {
	const controlGrid = document.getElementById("control-grid");
	if (!controlGrid) {
		console.error("Control grid container not found");
		return;
	}

	addControlButtonToControlGrid(commandId, commandName, controlGrid);
}

// Expose to be called from C++
window.clearControlButtons = clearControlButtons;
window.addControlButton = addControlButton;
window.markRoverWaiting = markRoverWaiting;
window.markRoverReachable = markRoverReachable;
window.markRoverUnreachable = markRoverUnreachable;
window.showRoverWarning = showRoverWarning;
window.hideRoverWarning = hideRoverWarning;

/**
 * Replaces the control grid with buttons for the provided rover commands.
 *
 * This helper accepts an array of `[commandId, commandName]` tuples. It is kept
 * available as a bulk population API, although the current C++ flow may populate
 * buttons one at a time with `addControlButton(...)`.
 *
 * @param {Array<[string, string]>} commands List of command ID/name pairs.
 * @returns {void}
 */
function populateControlButtons(commands) {
	const controlGrid = document.getElementById("control-grid");
	if (!controlGrid) {
		console.error("Control grid container not found");
		return;
	}

	controlGrid.innerHTML = "";

	commands.forEach(([commandId, commandName]) =>
		addControlButtonToControlGrid(commandId, commandName, controlGrid),
	);

	addLog(
		new Date().toLocaleTimeString(),
		`Loaded ${commands.length} control commands`,
	);
}

// Expose to be called from C++
window.populateControlButtons = populateControlButtons;

const navbar = document.querySelector(".navbar");

/**
 * Starts native window dragging when the user drags the custom navbar.
 *
 * Clicks on buttons and links inside the navbar are ignored so window controls
 * remain clickable.
 */
navbar.addEventListener("mousedown", (e) => {
	console.log("mousedown");

	if (
		e.target.tagName === "BUTTON" ||
		e.target.tagName === "A" ||
		e.target.closest("button")
	) {
		return;
	}

	if (window.saucer?.startDrag) {
		console.log("start drag");
		window.saucer.startDrag();
	}
});

let devtoolsShown = false;

const debugBtn = document.getElementById("debug-btn");

/**
 * Toggles the native webview developer tools.
 *
 * Calls the C++ `toggleDevTools` binding exposed through Saucer.
 */
debugBtn.addEventListener("click", () => {
	devtoolsShown = !devtoolsShown;
	window.saucer.exposed.toggleDevTools(devtoolsShown);
});

let isMaximized = false;

const minBtn = document.getElementById("min-btn");
const maxBtn = document.getElementById("max-btn");
const closeBtn = document.getElementById("close-btn");

/**
 * Minimizes the native application window.
 */
minBtn.addEventListener("click", () => {
	window.saucer.minimize(true);
});

/**
 * Toggles the native application window between maximized and restored states.
 */
maxBtn.addEventListener("click", () => {
	isMaximized = !isMaximized;
	window.saucer.maximize(isMaximized);

	maxBtn.textContent = isMaximized ? "❐" : "▢";
});

/**
 * Closes the native application window.
 */
closeBtn.addEventListener("click", () => {
	window.saucer.close();
});

// UI Handlers
loginBtn.addEventListener("click", () => {
	overlay.classList.add("show");
	jidInput.focus();
});

cancelBtn.addEventListener("click", () => {
	overlay.classList.remove("show");
	errorMsg.classList.remove("show");
});

/**
 * Submits the login form to the C++ `Login` binding.
 *
 * On success, the XMPP connection has been established and the application will
 * wait for the rover options handshake. On failure, the error returned by C++ is
 * displayed in the login dialog.
 *
 * @async
 * @returns {Promise<void>}
 */
async function submitLogin() {
	const jid = jidInput.value;
	const password = passwordInput.value;

	if (!jid || !password) {
		errorMsg.textContent = "Please enter JID and password";
		errorMsg.classList.add("show");
		return;
	}

	submitBtn.disabled = true;
	submitBtn.textContent = "Connecting...";

	try {
		await saucer.exposed.Login(jid, password);

		overlay.classList.remove("show");
		loginBtn.textContent = "Waiting for Rover...";
		loginBtn.disabled = true;
		addLog(new Date().toLocaleTimeString(), `Logging in as ${jid}`);
		setControlsEnabled(false);
	} catch (err) {
		errorMsg.textContent = err;
		errorMsg.classList.add("show");
		submitBtn.disabled = false;
		submitBtn.textContent = "Login";
	}
}

for (const input of [jidInput, passwordInput]) {
	input.addEventListener("keydown", (event) => {
		if (event.key === "Enter" && !submitBtn.disabled) {
			event.preventDefault();
			submitLogin();
		}
	});
}

submitBtn.addEventListener("click", submitLogin);

controlButtons.forEach((btn) => {
	btn.addEventListener("click", async () => {
		const command = btn.dataset.command;
		if (!command) return;

		try {
			await saucer.exposed.SendCommand(command);
		} catch (err) {
			addLog(new Date().toLocaleTimeString(), `Error: ${err}`);
		}
	});
});

/**
 * Updates or appends a telemetry status field.
 *
 * If a row for the provided key already exists, only its value is updated.
 * Otherwise, a new telemetry row is appended to the status panel and cached.
 *
 * @param {string} key Telemetry name, such as `"Battery"`, `"Signal"`, or `"Speed"`.
 * @param {string|number|boolean|null} value Telemetry value to display.
 * @returns {void}
 */
function updateTelemetry(key, value) {
	if (!telemetryList) {
		console.error("Telemetry container not found");
		return;
	}

	const label = String(key).trim();
	if (!label) {
		console.error("Telemetry key cannot be empty");
		return;
	}

	telemetryState.set(label, {
		value: value,
		last_received: Date.now(),
		warned: false,
	});

	let row = telemetryRows.get(label);

	if (!row) {
		row = document.createElement("p");
		row.dataset.telemetryKey = label;
		row.className = "telemetry-row"; // Apply the new flexbox class
		telemetryRows.set(label, row);
		telemetryList.appendChild(row);
	}

	// Split the data and the timer into separate spans
	row.innerHTML = `
        <span class="telemetry-data">${label}: ${value}</span> 
        <span class="staleness-indicator">0.0s ago</span>
    `;
}

// timer to update telemetry age
setInterval(() => {
	const now = Date.now();

	for (const [label, data] of telemetryState.entries()) {
		const diffSeconds = ((now - data.last_received) / 1000).toFixed(1);
		const row = telemetryRows.get(label);

		if (row) {
			// Update the HTML with the separate spans
			row.innerHTML = `
                <span class="telemetry-data">${label}: ${data.value}</span> 
                <span class="staleness-indicator">${diffSeconds}s ago</span>
            `;
		}
	}
}, 500);

// Expose to the window so Saucer/C++ can call it
window.updateTelemetry = updateTelemetry;

/**
 * Creates or updates the camera feed iframe.
 *
 * C++ calls this after fetching rover options. If the rover provides a camera
 * stream URL, that URL is loaded. Otherwise, C++ may provide a fallback URL.
 *
 * @param {string} url URL to load into the camera iframe.
 * @returns {void}
 */
function setCameraIframe(url) {
	const container = document.getElementById("camera-feed");
	if (!container) {
		console.error("Camera container not found");
		return;
	}

	let iframe = container.querySelector("iframe");
	if (!iframe) {
		iframe = document.createElement("iframe");
		iframe.style.width = "100%";
		iframe.style.height = "100%";
		iframe.style.border = "none";
		iframe.setAttribute("allow", "autoplay; encrypted-media");
		container.appendChild(iframe);
	}

	iframe.src = url;
}

window.setCameraIframe = setCameraIframe;

// Theme toggle
const toggleButton = document.getElementById("theme-toggle");
const body = document.body;
const themeKey = "theme";
const darkModeClass = "dark-mode";

/**
 * Applies and persists the selected UI theme.
 *
 * @param {"dark"|"light"} theme Theme name to apply.
 * @returns {void}
 */
function setTheme(theme) {
	if (theme === "dark") {
		body.classList.add(darkModeClass);
		localStorage.setItem(themeKey, "dark");
	} else {
		body.classList.remove(darkModeClass);
		localStorage.setItem(themeKey, "light");
	}
}

/**
 * Toggles between light and dark themes.
 */
toggleButton.addEventListener("click", () => {
	body.classList.contains(darkModeClass) ? setTheme("light") : setTheme("dark");
});

const savedTheme = localStorage.getItem(themeKey);
if (
	savedTheme === "dark" ||
	(!savedTheme && window.matchMedia("(prefers-color-scheme: dark)").matches)
) {
	setTheme("dark");
}

function initializeResizableDashboard() {
	const dashboard = document.querySelector('.dashboard');
	const leftColumn = document.querySelector('.left-column');
	const columnResizer = document.getElementById('column-resizer');

	if (!dashboard || !leftColumn || !columnResizer) {
		return;
	}

	const minLeftWidth = 240;
	const minRightWidth = 320;

	columnResizer.addEventListener('pointerdown', (event) => {
		event.preventDefault();

		columnResizer.classList.add('dragging');
		columnResizer.setPointerCapture(event.pointerId);

		const handlePointerMove = (moveEvent) => {
			const dashboardRect = dashboard.getBoundingClientRect();
			const requestedLeftWidth = moveEvent.clientX - dashboardRect.left - 16;
			const maxLeftWidth = dashboardRect.width - minRightWidth - columnResizer.offsetWidth - 32;
			const clampedLeftWidth = Math.max(
				minLeftWidth,
				Math.min(requestedLeftWidth, maxLeftWidth)
			);

			dashboard.style.setProperty('--left-column-width', `${clampedLeftWidth}px`);
		};

		const handlePointerUp = (upEvent) => {
			columnResizer.classList.remove('dragging');
			columnResizer.releasePointerCapture(upEvent.pointerId);
			columnResizer.removeEventListener('pointermove', handlePointerMove);
			columnResizer.removeEventListener('pointerup', handlePointerUp);
			columnResizer.removeEventListener('pointercancel', handlePointerUp);
		};

		columnResizer.addEventListener('pointermove', handlePointerMove);
		columnResizer.addEventListener('pointerup', handlePointerUp);
		columnResizer.addEventListener('pointercancel', handlePointerUp);
	});

	document.querySelectorAll('.row-resizer').forEach((resizer) => {
		resizer.addEventListener('pointerdown', (event) => {
			event.preventDefault();

			const column = resizer.parentElement;
			const beforeSelector = resizer.dataset.resizeBefore;
			const afterSelector = resizer.dataset.resizeAfter;
			const beforePanel = column.querySelector(beforeSelector);
			const afterPanel = column.querySelector(afterSelector);

			if (!beforePanel || !afterPanel) {
				return;
			}

			resizer.classList.add('dragging');
			resizer.setPointerCapture(event.pointerId);

			const startY = event.clientY;
			const startBeforeHeight = beforePanel.getBoundingClientRect().height;
			const startAfterHeight = afterPanel.getBoundingClientRect().height;
			const minPanelHeight = 80;

			beforePanel.style.flex = `0 0 ${startBeforeHeight}px`;
			afterPanel.style.flex = `0 0 ${startAfterHeight}px`;

			const handlePointerMove = (moveEvent) => {
				const deltaY = moveEvent.clientY - startY;

				const requestedBeforeHeight = startBeforeHeight + deltaY;
				const requestedAfterHeight = startAfterHeight - deltaY;

				if (
					requestedBeforeHeight < minPanelHeight ||
					requestedAfterHeight < minPanelHeight
				) {
					return;
				}

				beforePanel.style.flexBasis = `${requestedBeforeHeight}px`;
				afterPanel.style.flexBasis = `${requestedAfterHeight}px`;
			};

			const handlePointerUp = (upEvent) => {
				resizer.classList.remove('dragging');
				resizer.releasePointerCapture(upEvent.pointerId);
				resizer.removeEventListener('pointermove', handlePointerMove);
				resizer.removeEventListener('pointerup', handlePointerUp);
				resizer.removeEventListener('pointercancel', handlePointerUp);
			};

			resizer.addEventListener('pointermove', handlePointerMove);
			resizer.addEventListener('pointerup', handlePointerUp);
			resizer.addEventListener('pointercancel', handlePointerUp);
		});
	});
}

initializeResizableDashboard();
