#include <cmath>

#include <wayfire/util/log.hpp>

#include "touch.hpp"
#include "input-manager.hpp"
#include "../core-impl.hpp"
#include "wayfire/output.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/compositor-surface.hpp"
#include "wayfire/output-layout.hpp"

wf::touch_interface_t::touch_interface_t(wlr_cursor *cursor, wlr_seat *seat,
    input_surface_selector_t surface_at)
{
    this->cursor = cursor;
    this->seat   = seat;
    this->surface_at = surface_at;

    // connect handlers
    on_down.set_callback([=] (void *data)
    {
        auto ev = static_cast<wlr_event_touch_down*>(data);
        emit_device_event_signal("touch_down", &ev);

        double lx, ly;
        wlr_cursor_absolute_to_layout_coords(cursor, ev->device,
            ev->x, ev->y, &lx, &ly);

        wf::pointf_t point;
        wf::get_core().output_layout->get_output_coords_at({lx, ly}, point);
        handle_touch_down(ev->touch_id, ev->time_msec, point);
        wlr_idle_notify_activity(wf::get_core().protocols.idle,
            wf::get_core().get_current_seat());
    });

    on_up.set_callback([=] (void *data)
    {
        auto ev = static_cast<wlr_event_touch_up*>(data);
        emit_device_event_signal("touch_up", ev);
        handle_touch_up(ev->touch_id, ev->time_msec);
        wlr_idle_notify_activity(wf::get_core().protocols.idle,
            wf::get_core().get_current_seat());
    });

    on_motion.set_callback([=] (void *data)
    {
        auto ev = static_cast<wlr_event_touch_motion*>(data);
        emit_device_event_signal("touch_motion", &ev);

        double lx, ly;
        wlr_cursor_absolute_to_layout_coords(
            wf::get_core_impl().input->cursor->cursor, ev->device,
            ev->x, ev->y, &lx, &ly);

        wf::pointf_t point;
        wf::get_core().output_layout->get_output_coords_at({lx, ly}, point);
        handle_touch_motion(ev->touch_id, ev->time_msec, point, true);
        wlr_idle_notify_activity(wf::get_core().protocols.idle,
            wf::get_core().get_current_seat());
    });

    on_up.connect(&cursor->events.touch_up);
    on_down.connect(&cursor->events.touch_down);
    on_motion.connect(&cursor->events.touch_motion);

    on_surface_map_state_change.set_callback(
        [=] (wf::surface_interface_t *surface)
    {
        if ((this->grabbed_surface == surface) && !surface->is_mapped())
        {
            end_touch_down_grab();
            on_stack_order_changed.emit(nullptr);
        }
    });

    on_stack_order_changed.set_callback([=] (wf::signal_data_t *data)
    {
        for (auto f : this->get_state().fingers)
        {
            this->handle_touch_motion(f.first, get_current_time(),
                {f.second.current.x, f.second.current.y}, false);
        }
    });

    wf::get_core().connect_signal("output-stack-order-changed",
        &on_stack_order_changed);
    wf::get_core().connect_signal("view-geometry-changed", &on_stack_order_changed);

    add_default_gestures();
}

wf::touch_interface_t::~touch_interface_t()
{}

const wf::touch::gesture_state_t& wf::touch_interface_t::get_state() const
{
    return this->finger_state;
}

wf::surface_interface_t*wf::touch_interface_t::get_focus() const
{
    return this->focus;
}

void wf::touch_interface_t::handle_new_device(wlr_input_device *device)
{
    wlr_cursor_attach_input_device(cursor, device);
}

void wf::touch_interface_t::set_grab(wf::plugin_grab_interface_t *grab)
{
    if (grab)
    {
        this->grab = grab;
        end_touch_down_grab();
        for (auto& f : this->get_state().fingers)
        {
            set_touch_focus(nullptr, f.first, get_current_time(), {0, 0});
        }
    } else
    {
        this->grab = nullptr;
        for (auto& f : this->get_state().fingers)
        {
            handle_touch_motion(f.first, get_current_time(),
                {f.second.current.x, f.second.current.y}, false);
        }
    }
}

void wf::touch_interface_t::add_touch_gesture(
    nonstd::observer_ptr<touch::gesture_t> gesture)
{
    this->gestures.emplace_back(gesture);
}

void wf::touch_interface_t::rem_touch_gesture(
    nonstd::observer_ptr<touch::gesture_t> gesture)
{
    gestures.erase(std::remove(gestures.begin(), gestures.end(), gesture),
        gestures.end());
}

void wf::touch_interface_t::set_touch_focus(wf::surface_interface_t *surface,
    int id, uint32_t time, wf::pointf_t point)
{
    bool focus_compositor_surface = wf::compositor_surface_from_surface(surface);
    bool had_focus = wlr_seat_touch_get_point(seat, id);

    wlr_surface *next_focus = NULL;
    if (surface && !focus_compositor_surface)
    {
        next_focus = surface->get_wlr_surface();
    }

    // create a new touch point, we have a valid new focus
    if (!had_focus && next_focus)
    {
        wlr_seat_touch_notify_down(seat, next_focus, time, id, point.x, point.y);
    }

    if (had_focus && !next_focus)
    {
        wlr_seat_touch_notify_up(seat, time, id);
    }

    if (next_focus)
    {
        wlr_seat_touch_point_focus(seat, next_focus, time, id, point.x, point.y);
    }

    /* Manage the touch_focus, we take only the first finger for that */
    if (id == 0)
    {
        auto compositor_surface =
            wf::compositor_surface_from_surface(this->focus);
        if (compositor_surface)
        {
            compositor_surface->on_touch_up();
        }

        compositor_surface = wf::compositor_surface_from_surface(surface);
        if (compositor_surface)
        {
            compositor_surface->on_touch_down(point.x, point.y);
        }

        this->focus = surface;
    }
}

void wf::touch_interface_t::update_gestures(const wf::touch::gesture_event_t& ev)
{
    finger_state.update(ev);
    for (auto& gesture : this->gestures)
    {
        if ((this->finger_state.fingers.size() == 1) &&
            (ev.type == touch::EVENT_TYPE_TOUCH_DOWN))
        {
            gesture->reset(ev.time);
        }

        gesture->update_state(ev);
    }
}

void wf::touch_interface_t::handle_touch_down(int32_t id, uint32_t time,
    wf::pointf_t point)
{
    // TODO: bad design
    auto& input = wf::get_core_impl().input;
    input->mod_binding_key = 0;

    if (id == 0)
    {
        wf::get_core().focus_output(
            wf::get_core().output_layout->get_output_at(point.x, point.y));
    }

    // NB. We first update the focus, and then update the gesture,
    // except if the input is grabbed.
    //
    // This is necessary because wm-focus needs to know the touch focus at the
    // moment the tap happens
    wf::touch::gesture_event_t gesture_event = {
        .type   = wf::touch::EVENT_TYPE_TOUCH_DOWN,
        .time   = time,
        .finger = id,
        .pos    = {point.x, point.y}
    };

    if (this->grab)
    {
        update_gestures(gesture_event);
        if (grab->callbacks.touch.down)
        {
            auto wo = wf::get_core().get_active_output();
            auto og = wo->get_layout_geometry();
            grab->callbacks.touch.down(id, point.x - og.x, point.y - og.y);
        }

        return;
    }

    wf::pointf_t local;
    auto focus = this->surface_at(point, local);
    if (finger_state.fingers.empty()) // finger state is not updated yet
    {
        start_touch_down_grab(focus);
    } else if (grabbed_surface && !input->drag_icon)
    {
        focus = grabbed_surface;
        local = get_surface_relative_coords(focus, point);
    }

    set_touch_focus(focus, id, time, local);
    input->update_drag_icon();
    update_gestures(gesture_event);
}

void wf::touch_interface_t::handle_touch_motion(int32_t id, uint32_t time,
    wf::pointf_t point, bool is_real_event)
{
    update_gestures({
        .type   = wf::touch::EVENT_TYPE_MOTION,
        .time   = time,
        .finger = id,
        .pos    = {point.x, point.y}
    });

    if (this->grab)
    {
        auto wo = wf::get_core().output_layout->get_output_at(point.x, point.y);
        auto og = wo->get_layout_geometry();
        if (grab->callbacks.touch.motion && is_real_event)
        {
            grab->callbacks.touch.motion(id, point.x - og.x, point.y - og.y);
        }

        return;
    }

    wf::pointf_t local;
    wf::surface_interface_t *surface = nullptr;
    /* Same as cursor motion handling: make sure we send to the grabbed surface,
     * except if we need this for DnD */
    if (grabbed_surface && !wf::get_core_impl().input->drag_icon)
    {
        surface = grabbed_surface;
        local   = get_surface_relative_coords(surface, point);
    } else
    {
        surface = surface_at(point, local);
        set_touch_focus(surface, id, time, local);
    }

    wlr_seat_touch_notify_motion(seat, time, id, local.x, local.y);
    wf::get_core_impl().input->update_drag_icon();

    auto compositor_surface = wf::compositor_surface_from_surface(surface);
    if ((id == 0) && compositor_surface && is_real_event)
    {
        compositor_surface->on_touch_motion(local.x, local.y);
    }
}

void wf::touch_interface_t::handle_touch_up(int32_t id, uint32_t time)
{
    update_gestures({
        .type   = wf::touch::EVENT_TYPE_TOUCH_UP,
        .time   = time,
        .finger = id,
        .pos    = finger_state.fingers[id].current
    });

    if (this->grab)
    {
        if (grab->callbacks.touch.up)
        {
            grab->callbacks.touch.up(id);
        }

        return;
    }

    set_touch_focus(nullptr, id, time, {0, 0});
    if (finger_state.fingers.empty())
    {
        end_touch_down_grab();
    }
}

void wf::touch_interface_t::start_touch_down_grab(
    wf::surface_interface_t *surface)
{
    this->grabbed_surface = surface;
}

void wf::touch_interface_t::end_touch_down_grab()
{
    if (grabbed_surface)
    {
        grabbed_surface = nullptr;
        for (auto& f : finger_state.fingers)
        {
            handle_touch_motion(f.first, wf::get_current_time(),
                {f.second.current.x, f.second.current.y}, false);
        }
    }
}

constexpr static int MIN_FINGERS = 3;
constexpr static double MIN_SWIPE_DISTANCE = 30;
constexpr static double MAX_SWIPE_DISTANCE = 100;
constexpr static double GESTURE_INITIAL_TOLERANCE = 40;
constexpr static double SWIPE_INCORRECT_DRAG_TOLERANCE = 20;
constexpr static double PINCH_INCORRECT_DRAG_TOLERANCE = 100;
constexpr static int EDGE_SWIPE_THRESHOLD = 50;
constexpr static double PINCH_THRESHOLD   = 1.5;

using namespace wf::touch;
/**
 * swipe and with multiple fingers and directions
 */
class multi_action_t : public gesture_action_t
{
  public:
    multi_action_t(bool pinch)
    {
        this->pinch = pinch;
    }

    bool pinch;
    bool last_pinch_was_pinch_in = false;

    uint32_t target_direction = 0;
    int32_t cnt_fingers = 0;

    action_status_t update_state(const gesture_state_t& state,
        const gesture_event_t& event) override
    {
        if (event.type == EVENT_TYPE_TOUCH_UP)
        {
            return ACTION_STATUS_CANCELLED;
        }

        if (event.type == EVENT_TYPE_TOUCH_DOWN)
        {
            cnt_fingers = state.fingers.size();
            for (auto& finger : state.fingers)
            {
                if (glm::length(finger.second.delta()) > GESTURE_INITIAL_TOLERANCE)
                {
                    return ACTION_STATUS_CANCELLED;
                }
            }

            return ACTION_STATUS_RUNNING;
        }

        if (this->pinch)
        {
            if (glm::length(state.get_center().delta()) >=
                PINCH_INCORRECT_DRAG_TOLERANCE)
            {
                return ACTION_STATUS_CANCELLED;
            }

            double pinch = state.get_pinch_scale();
            last_pinch_was_pinch_in = pinch <= 1.0;
            if ((pinch <= 1.0 / PINCH_THRESHOLD) || (pinch >= PINCH_THRESHOLD))
            {
                return ACTION_STATUS_COMPLETED;
            }

            return ACTION_STATUS_RUNNING;
        }

        // swipe case
        if ((glm::length(state.get_center().delta()) >= MIN_SWIPE_DISTANCE) &&
            (this->target_direction == 0))
        {
            this->target_direction = state.get_center().get_direction();
        }

        if (this->target_direction == 0)
        {
            return ACTION_STATUS_RUNNING;
        }

        for (auto& finger : state.fingers)
        {
            if (finger.second.get_incorrect_drag_distance(this->target_direction) >
                this->get_move_tolerance())
            {
                return ACTION_STATUS_CANCELLED;
            }
        }

        if (state.get_center().get_drag_distance(this->target_direction) >=
            MAX_SWIPE_DISTANCE)
        {
            return ACTION_STATUS_COMPLETED;
        }

        return ACTION_STATUS_RUNNING;
    }

    void reset(uint32_t time) override
    {
        gesture_action_t::reset(time);
        target_direction = 0;
    }
};

static uint32_t find_swipe_edges(wf::touch::point_t point)
{
    auto output   = wf::get_core().get_active_output();
    auto geometry = output->get_layout_geometry();

    uint32_t edge_directions = 0;
    if (point.x <= geometry.x + EDGE_SWIPE_THRESHOLD)
    {
        edge_directions |= wf::GESTURE_DIRECTION_RIGHT;
    }

    if (point.x >= geometry.x + geometry.width - EDGE_SWIPE_THRESHOLD)
    {
        edge_directions |= wf::GESTURE_DIRECTION_LEFT;
    }

    if (point.y <= geometry.y + EDGE_SWIPE_THRESHOLD)
    {
        edge_directions |= wf::GESTURE_DIRECTION_DOWN;
    }

    if (point.y >= geometry.y + geometry.height - EDGE_SWIPE_THRESHOLD)
    {
        edge_directions |= wf::GESTURE_DIRECTION_UP;
    }

    return edge_directions;
}

static uint32_t wf_touch_to_wf_dir(uint32_t touch_dir)
{
    uint32_t gesture_dir = 0;
    if (touch_dir & MOVE_DIRECTION_RIGHT)
    {
        gesture_dir |= wf::GESTURE_DIRECTION_RIGHT;
    }

    if (touch_dir & MOVE_DIRECTION_LEFT)
    {
        gesture_dir |= wf::GESTURE_DIRECTION_LEFT;
    }

    if (touch_dir & MOVE_DIRECTION_UP)
    {
        gesture_dir |= wf::GESTURE_DIRECTION_UP;
    }

    if (touch_dir & MOVE_DIRECTION_DOWN)
    {
        gesture_dir |= wf::GESTURE_DIRECTION_DOWN;
    }

    return gesture_dir;
}

void wf::touch_interface_t::add_default_gestures()
{
    std::unique_ptr<multi_action_t> swipe = std::make_unique<multi_action_t>(false);
    std::unique_ptr<multi_action_t> pinch = std::make_unique<multi_action_t>(true);
    nonstd::observer_ptr<multi_action_t> swp_ptr = swipe;
    nonstd::observer_ptr<multi_action_t> pnc_ptr = pinch;

    std::vector<std::unique_ptr<gesture_action_t>> swipe_actions, pinch_actions;
    swipe_actions.emplace_back(std::move(swipe));
    pinch_actions.emplace_back(std::move(pinch));

    auto ack_swipe = [swp_ptr, this] ()
    {
        uint32_t possible_edges =
            find_swipe_edges(finger_state.get_center().origin);
        uint32_t direction = wf_touch_to_wf_dir(swp_ptr->target_direction);

        touch_gesture_type_t type = GESTURE_TYPE_SWIPE;
        if (possible_edges & direction)
        {
            direction = possible_edges & direction;
            type = GESTURE_TYPE_EDGE_SWIPE;
        }

        wf::touchgesture_t gesture{type, direction, swp_ptr->cnt_fingers};
        wf::get_core_impl().input->handle_gesture(gesture);
    };

    auto ack_pinch = [pnc_ptr] ()
    {
        wf::touchgesture_t gesture{GESTURE_TYPE_PINCH,
            pnc_ptr->last_pinch_was_pinch_in ? GESTURE_DIRECTION_IN :
            GESTURE_DIRECTION_OUT,
            pnc_ptr->cnt_fingers
        };
        wf::get_core_impl().input->handle_gesture(gesture);
    };

    this->multiswipe = std::make_unique<gesture_t>(std::move(
        swipe_actions), ack_swipe);
    this->multipinch = std::make_unique<gesture_t>(std::move(
        pinch_actions), ack_pinch);
    this->add_touch_gesture(multiswipe);
    this->add_touch_gesture(multipinch);
}

void input_manager::handle_gesture(wf::touchgesture_t g)
{
    std::vector<std::function<void()>> callbacks;

    for (auto& binding : bindings[WF_BINDING_GESTURE])
    {
        auto as_gesture = std::dynamic_pointer_cast<
            wf::config::option_t<wf::touchgesture_t>>(binding->value);
        assert(as_gesture);

        if ((binding->output == wf::get_core().get_active_output()) &&
            (as_gesture->get_value() == g))
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto call = binding->call.gesture;
            callbacks.push_back([=, &g] ()
            {
                (*call)(&g);
            });
        }
    }

    for (auto& binding : bindings[WF_BINDING_ACTIVATOR])
    {
        auto as_activator = std::dynamic_pointer_cast<
            wf::config::option_t<wf::activatorbinding_t>>(binding->value);
        assert(as_activator);

        if ((binding->output == wf::get_core().get_active_output()) &&
            as_activator->get_value().has_match(g))
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto call = binding->call.activator;
            callbacks.push_back([=] ()
            {
                (*call)(wf::ACTIVATOR_SOURCE_GESTURE, 0);
            });
        }
    }

    for (auto call : callbacks)
    {
        call();
    }
}
