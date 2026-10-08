#pragma once

#include <typeinfo>

namespace HyprThanos::Compat::Detail {

    // The modern list exposes a plan with one stage per active transformer, but no iterators.
    // Finding motion blur alone is insufficient: another transformer may coexist with it.
    template <typename MotionBlur, typename List, typename Box>
    bool hasUnsupportedActiveTransformers(const List& list, const Box& box) {
        if (list.empty())
            return false;

        const auto* motionBlur = list.template get<MotionBlur>();
        if (!motionBlur || typeid(*motionBlur) != typeid(MotionBlur) || !motionBlur->active())
            return true;

        return list.plan(box, box).stages.size() != 1;
    }

}
