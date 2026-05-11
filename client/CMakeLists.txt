add_executable(client_saucer main.cpp)

# Embed the frontend folder located in this directory
saucer_embed("webview" TARGET client_saucer)

# Link both the UI framework and your XMPP logic
target_link_libraries(client_saucer PRIVATE
    saucer::saucer
    saucer::embedded
    shared_xmpp
)