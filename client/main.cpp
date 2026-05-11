#include <saucer/smartview.hpp>
#include <saucer/embedded/all.hpp>
#include <print>
#include <iostream>
#include <string>

#define WEBVIEW_DEBUG_FLAG false

coco::stray start(saucer::application *app) {
    auto window = saucer::window::create(app).value();
    auto webview = saucer::smartview::create({.window = window});

    // 1. Window Configuration
    window->set_title("PICSES Helelani Rover");

    window->set_decorations(saucer::window::decoration::partial);

    webview->set_dev_tools(WEBVIEW_DEBUG_FLAG);

    webview->expose("toggleDevTools", [&](const bool devtoolsShown) -> void {
        webview->set_dev_tools(devtoolsShown);
    });

    // 2. Bindings (Replaces Wails 'Bind' and 'App' struct)
    webview->expose("Login", [](std::string jid, std::string password) -> void {

        // const auto& [resolve, reject] = exec;

        std::cout << "Login attempt: " << jid << " " << password << std::endl;
        // Logic for XMPP goes here later. Returning true for now.
        // resolve(std::format("you logged in as {} with password {}", jid, password));
    });

    webview->expose("SendCommand", [](std::string command) -> coco::task<void> {
        std::cout << "Sending command: " << command << std::endl;
        co_return;
    });

    // auto index = saucer::
    // 3. Asset Hosting
    // For tonight's sprint: If your UI is running on a dev server (like Vite),
    // use webview->set_url("http://localhost:5173");
    // Otherwise, point to your dist folder:
    webview->embed(saucer::embedded::all());
    webview->serve("/index.html");

    window->show();

    co_await app->finish();

}

int main() {
    return saucer::application::create({.id = "rover-fe-cpp"})->run(start);
}