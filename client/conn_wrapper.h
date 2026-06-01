//
// Created by joseph on 5/31/26.
//

#ifndef HELELANIROVER_CONN_WRAPPER_H
#define HELELANIROVER_CONN_WRAPPER_H
#include <memory>
#include <utility>

#include "libstrophe_cpp.h"

class XmppClientState {
public:
    void replace(std::shared_ptr<libstrophe_cpp> next) {
        std::shared_ptr<libstrophe_cpp> old; {
            std::lock_guard lock(mutex_);
            old = std::exchange(client_, std::move(next));
        }

        if (old) {
            old->disconnect();
        }
    }

    std::shared_ptr<libstrophe_cpp> get() const {
        std::lock_guard lock(mutex_);
        return client_;
    }

    void clear_if_current(const std::shared_ptr<libstrophe_cpp> &client) {
        std::lock_guard lock(mutex_);
        if (client_ == client) {
            client_.reset();
        }
    }

    void disconnect_current() {
        std::shared_ptr<libstrophe_cpp> client; {
            std::lock_guard lock(mutex_);
            client = client_;
        }

        if (client) {
            client->disconnect();
        }
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<libstrophe_cpp> client_;
};

#endif //HELELANIROVER_CONN_WRAPPER_H
