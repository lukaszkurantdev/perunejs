#ifndef PERUNEJS_DEPTH_GUARD_H
#define PERUNEJS_DEPTH_GUARD_H
#include <utility>
#include <vector>

namespace perunejs {
    struct LabelInfo {
        std::string label;
        bool is_loop = false;
    };

    class DepthGuard {
        uint32_t& counter;
    public:
        explicit DepthGuard(uint32_t& counter) : counter(counter) {++counter;}
        ~DepthGuard() { --counter;}
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;
    };

    class LabelGuard {
        std::vector<LabelInfo>& labels;

    public:
        LabelGuard(std::vector<LabelInfo>& labels, std::string name, bool is_loop) :
            labels(labels) { labels.push_back({.label = std::move(name), .is_loop = is_loop}); }
        ~LabelGuard() { labels.pop_back(); }
        LabelGuard(const LabelGuard&) = delete;
        LabelGuard& operator=(const LabelGuard&) = delete;
    };

    class FunctionScope {
        uint32_t& function_depth;
        uint32_t& loop_depth;
        uint32_t& switch_depth;
        std::vector<LabelInfo>& labels;
        uint32_t saved_loop;
        uint32_t saved_switch;
        std::vector<LabelInfo> saved_labels;
    public:
        FunctionScope(uint32_t& function_depth, uint32_t& loop_depth,
                      uint32_t& switch_depth, std::vector<LabelInfo>& labels)
            : function_depth(function_depth), loop_depth(loop_depth), switch_depth(switch_depth), labels(labels),
              saved_loop(loop_depth), saved_switch(switch_depth),saved_labels(std::move(labels)) {
            ++function_depth;
            loop_depth = 0;
            switch_depth = 0;
            labels.clear();
        }
        ~FunctionScope() {
            --function_depth;
            loop_depth = saved_loop;
            switch_depth = saved_switch;
            labels = std::move(saved_labels);
        }
        FunctionScope(const FunctionScope&) = delete;
        FunctionScope& operator=(const FunctionScope&) = delete;
    };
}

#endif //PERUNEJS_DEPTH_GUARD_H
