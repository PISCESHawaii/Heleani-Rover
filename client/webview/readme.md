# Controller Webview

This folder contains the embedded frontend for the Helelani Rover controller client.

The webview is the browser-based UI shown inside the native Saucer application. It handles the visible dashboard, login
form, telemetry display, command buttons, camera iframes, window controls, theme switching, and other browser-side behavior.

The native C++ client owns the XMPP connection and rover communication. This webview owns the UI presentation and calls into
C++ through Saucer bindings.

## Folder Layout
```text
webview/
    index.html
    js/
        index.js
    stylesheets/
        rover.css
```
### `index.html`

Defines the static UI shell.

It contains:

- XMPP login modal.
- Top navigation bar.
- Native-style window control buttons.
- Rover status panel.
- Dynamic rover controls panel.
- Camera feed panel.
- Command log panel.
- Resizer elements for adjustable dashboard panes.

Most rover-specific content is populated dynamically after the C++ client completes the rover handshake.

### `js/index.js`

Main browser-side controller script.

It handles:

- Login form behavior.
- Calls from JavaScript into C++ through `window.saucer.exposed`.
- Functions exposed on `window` so C++ can update the UI.
- Rover connection state UI.
- Dynamic command button creation.
- Telemetry row updates.
- Telemetry staleness timers.
- Camera iframe creation/removal.
- Command log updates.
- Copying logs to the clipboard.
- Light/dark theme switching.
- Native window controls.
- Dashboard resizing.

### `stylesheets/rover.css`

Main stylesheet for the controller UI.

It defines:

- Light and dark theme variables.
- Dashboard layout.
- Status, controls, camera, and log panels.
- Login dialog styling.
- Dynamic telemetry row styling.
- Camera iframe layout.
- Resizer styling.
- Custom title/window control styling.

## How the Webview Is Loaded

The webview is not served by an external web server during normal application use.

Instead, the C++ client embeds this directory into the native executable using Saucer's embedded asset support. At runtime,
the C++ application loads the embedded `index.html` file into the native webview.

Because of this, paths in the HTML should stay relative to the `webview/` directory:
```html
<link rel="stylesheet" href="stylesheets/rover.css">
<script type="module" src="js/index.js" defer></script>
```
## C++ to JavaScript Boundary

The C++ client calls JavaScript functions exposed on `window` to update the UI after XMPP events.

Important functions exposed by `index.js` include:

- `addLog(time, message)`
- `clearControlButtons()`
- `addControlButton(commandId, commandName)`
- `populateControlButtons(commands)`
- `markRoverWaiting()`
- `markRoverReachable()`
- `markRoverUnreachable()`
- `showRoverWarning(title, message)`
- `hideRoverWarning()`
- `updateTelemetry(key, value)`
- `setCameraIframe(url, id)`
- `removeCameraIframe(id)`
- `clearAllCameraIframes()`

If one of these functions is renamed or removed, the C++ client may fail when it tries to update the webview.

To expose a function to JavaScript, simply assign it to a member of the `window` global. 
```js
function myFunction() {
    // do something
}

window.myFunction = myFunction;
```
or alternatively,
```js
window.myFunction = () => {
    // do something
};
```

## JavaScript to C++ Boundary

JavaScript calls native C++ functions through Saucer's exposed bindings:
```javascript
window.saucer.exposed.Login(jid, password);
window.saucer.exposed.SendCommand(command);
window.saucer.exposed.toggleDevTools(enabled);
```
### `Login(jid, password)`

Called when the user submits the login form.

The C++ side attempts to connect to the XMPP server using the provided credentials. If login succeeds, the UI transitions
to a "Waiting for Rover" state while the C++ client repeatedly attempts the `rover::getopts` handshake.

### `SendCommand(command)`

Called when a dynamic rover control button is clicked.

The command value is normally one of the command IDs returned by the rover during the `rover::getopts` handshake, such as:
```text
rover::movements::forward
rover::movements::stop
```
The C++ side sends that command to the rover as an XMPP IQ request.

### `toggleDevTools(enabled)`

Called by the debug button in the title bar.

This toggles the native webview developer tools, which is useful for debugging HTML, CSS, and JavaScript issues inside the
embedded webview.

## Runtime UI Flow

The general UI flow is:

1. The login overlay is shown by default.
2. The user enters their XMPP JID and password.
3. JavaScript calls the C++ `Login(...)` binding.
4. If login fails, the error is shown in the login dialog.
5. If login succeeds:
    - the login overlay closes
    - controls are disabled
    - the UI shows "Waiting for Rover"
6. C++ repeatedly attempts the `rover::getopts` handshake.
7. When the rover responds:
    - C++ calls `clearControlButtons()`
    - C++ calls `addControlButton(...)` for each advertised command
    - C++ calls `setCameraIframe(...)` for each advertised camera feed
    - C++ calls `markRoverReachable()`
8. Telemetry updates are pushed from C++ into `updateTelemetry(...)`.
9. If telemetry stops, C++ calls `markRoverUnreachable()`.
10. The UI disables controls and waits for the rover to become reachable again.

## Dynamic Rover Controls

The control button grid starts empty.

Buttons are added at runtime based on the command list returned by the rover. Each button stores the command ID in a
`data-command` attribute and sends that value back to C++ when clicked.

This means the UI does not need to be recompiled or edited when new rover commands are added, as long as the rover advertises
them through the options response.

## Telemetry Display

Telemetry rows are created dynamically.

When C++ calls:
```js
updateTelemetry("battery", "84");
```
the webview either creates or updates a telemetry row for `battery`.

Each telemetry row also tracks when it was last updated. A timer refreshes the displayed age every 500ms, so the UI can show
how stale each telemetry value is.

Telemetry is cleared when:

- the client is waiting for the rover
- the rover becomes unreachable
- the UI is reset for a new connection state

## Camera Feed Handling

Camera feeds are represented as iframes inside the camera panel.

C++ calls:
```js
setCameraIframe(url, id);
```
The `id` is used as a stable camera identifier. If an iframe with the same camera ID already exists, its URL is updated.
Otherwise, a new iframe is created.

Related functions:

- `setCameraIframe(url, id)` creates or updates one feed.
- `removeCameraIframe(id)` removes one feed.
- `clearAllCameraIframes()` removes every feed.

The current camera area supports multiple feeds using a responsive CSS grid.

## Rover Availability UI

The webview has three main rover availability states:

### Waiting for Rover

Used after XMPP login succeeds but before the rover responds to `rover::getopts`.

Triggered by:
```js
markRoverWaiting();
```
This disables controls, clears stale rover data, and displays a warning overlay.

### Rover Reachable

Used after the rover options handshake succeeds.

Triggered by:
```js
markRoverReachable();
```
This hides the warning overlay and enables command buttons.

### Rover Unreachable

Used when telemetry stops for too long.

Triggered by:
```js
markRoverUnreachable();
```
This disables controls, clears camera/telemetry data, shows a warning overlay, and logs the event.

## Theme Handling

The UI supports light and dark themes.

The selected theme is stored in `localStorage` under:
```text
theme
```
If no theme has been saved, the UI uses the system color preference when available.

The dark theme is applied by adding this class to the body:
```text
dark-mode
```
Most colors are controlled through CSS variables, so theme edits should generally be made in `rover.css`.

## Window Controls

The top bar includes custom controls for:

- theme toggle
- devtools toggle
- minimize
- maximize/restore
- close

These use Saucer's browser-side window helpers where available.

Dragging the navbar also starts a native window drag operation, except when the user clicks on buttons or links inside the
navbar.

## Dashboard Resizing

The dashboard has two resizing systems:

- A vertical column resizer between the left and right columns.
- Row resizers between panels.

The resize behavior is implemented in `initializeResizableDashboard()` in `index.js`.

The left column width is controlled through the CSS variable:
```text
--left-column-width
```
Panel heights are adjusted by changing flex basis values during pointer drag events.

## Development Notes

This frontend is designed to be edited like a normal static web page, but it runs inside a Saucer-controlled native webview.

Useful tips:

- Use the debug/devtools button to inspect the embedded page.
- Keep IDs stable if C++ or JavaScript depends on them.
- Keep C++-called functions exposed on `window`.
- Prefer adding new rover UI data dynamically from the rover options response instead of hardcoding command buttons.
- If CSS or JavaScript appears corrupted after editing and rebuilding, restart and build again. This may be related to how
  Saucer embeds or refreshes frontend assets during the build process.
- Generally where possible, business logic should be moved to C++, and js should be treated as a UI meta-language

## Important IDs and Elements

The JavaScript expects these IDs to exist in `index.html`:
```text
login-overlay
login-btn
cancel-login
submit-login
jid
password
login-error
log-box
copy-log-btn
telemetry-list
control-grid
camera-feed
theme-toggle
debug-btn
min-btn
max-btn
close-btn
column-resizer
```
Changing these IDs requires updating `js/index.js`.

## Future Work Ideas

Possible future additions:

- Dedicated map/location panel.
- Better camera feed controls.
- Popout windows
- Batched commands (possibly read in from a file)
- Screenshot video feed (likely possible through a browser api)
- Latency indicator
