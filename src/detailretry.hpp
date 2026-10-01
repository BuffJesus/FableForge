#pragma once
#include <algorithm>
#include <map>
#include <string>

namespace albion::worldview {
// Failure state is scoped to the current world/content generation. A broken map
// must not occupy the sole worker repeatedly while healthy neighbours wait.
class DetailRetry {
    struct State { unsigned attempts=0; double next=0; };
    std::map<std::string, State> failed_;
public:
    bool ready(const std::string& name, double now) const {
        const auto it=failed_.find(name);
        return it==failed_.end() || now>=it->second.next;
    }
    unsigned fail(const std::string& name, double now) {
        auto& state=failed_[name];
        state.attempts=std::min(5u,state.attempts+1);
        const unsigned delay=std::min(30u,1u<<state.attempts);
        state.next=now+delay;
        return delay;
    }
    void success(const std::string& name) { failed_.erase(name); }
    void clear() { failed_.clear(); }
};
} // namespace albion::worldview
