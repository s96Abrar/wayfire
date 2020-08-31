#ifndef CORE_HPP
#define CORE_HPP

#include "wayfire/object.hpp"
#include <wayfire/geometry.hpp>

#include <sys/types.h>
#include <limits>
#include <vector>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/config/config-manager.hpp>

extern "C"
{
    struct wlr_backend;
    struct wlr_renderer;
    struct wlr_seat;
    struct wlr_cursor;
    struct wlr_data_device_manager;
    struct wlr_data_control_manager_v1;
    struct wlr_gamma_control_manager_v1;
    struct wlr_xdg_output_manager_v1;
    struct wlr_export_dmabuf_manager_v1;
    struct wlr_server_decoration_manager;
    struct wlr_xdg_decoration_manager_v1;
    struct wlr_input_inhibit_manager;
    struct wlr_virtual_keyboard_manager_v1;
    struct wlr_virtual_pointer_manager_v1;
    struct wlr_idle;
    struct wlr_idle_inhibit_manager_v1;
    struct wlr_screencopy_manager_v1;
    struct wlr_foreign_toplevel_manager_v1;
    struct wlr_pointer_gestures_v1;
    struct wlr_relative_pointer_manager_v1;
    struct wlr_pointer_constraints_v1;
    struct wlr_tablet_manager_v2;
    struct wlr_input_method_manager_v2;
    struct wlr_text_input_manager_v3;
    struct wlr_presentation;
    struct wlr_gtk_primary_selection_device_manager;
    struct wlr_primary_selection_v1_device_manager;

#include <wayland-server.h>
}

namespace wf
{
class surface_interface_t;
class view_interface_t;

namespace touch
{
class gesture_t;
}
}

using wayfire_view = nonstd::observer_ptr<wf::view_interface_t>;

namespace wf
{
class output_t;
class output_layout_t;
class input_device_t;

class compositor_core_t : public wf::object_base_t
{
  public:
    /**
     * The current configuration used by Wayfire
     */
    wf::config::config_manager_t config;

    /**
     * The wayland display and its event loop
     */
    wl_display *display;
    wl_event_loop *ev_loop;

    /**
     * The current wlr backend in use. The only case where another backend is
     * used is when there are no outputs added, in which case a noop backend is
     * used instead of this one
     */
    wlr_backend *backend;
    wlr_renderer *renderer;

    std::unique_ptr<wf::output_layout_t> output_layout;

    /**
     * Various protocols supported by wlroots
     */
    struct
    {
        wlr_data_device_manager *data_device;
        wlr_data_control_manager_v1 *data_control;
        wlr_gamma_control_manager_v1 *gamma_v1;
        wlr_screencopy_manager_v1 *screencopy;
        wlr_export_dmabuf_manager_v1 *export_dmabuf;
        wlr_server_decoration_manager *decorator_manager;
        wlr_xdg_decoration_manager_v1 *xdg_decorator;
        wlr_xdg_output_manager_v1 *output_manager;
        wlr_virtual_keyboard_manager_v1 *vkbd_manager;
        wlr_virtual_pointer_manager_v1 *vptr_manager;
        wlr_input_inhibit_manager *input_inhibit;
        wlr_idle *idle;
        wlr_idle_inhibit_manager_v1 *idle_inhibit;
        wlr_foreign_toplevel_manager_v1 *toplevel_manager;
        wlr_pointer_gestures_v1 *pointer_gestures;
        wlr_relative_pointer_manager_v1 *relative_pointer;
        wlr_pointer_constraints_v1 *pointer_constraints;
        wlr_tablet_manager_v2 *tablet_v2;
        wlr_input_method_manager_v2 *input_method;
        wlr_text_input_manager_v3 *text_input;
        wlr_presentation *presentation;
        wlr_gtk_primary_selection_device_manager *gtk_primary_selection;
        wlr_primary_selection_v1_device_manager *primary_selection_v1;
    } protocols;

    std::string to_string() const
    {
        return "wayfire-core";
    }

    /**
     * @return the current seat. For now, Wayfire supports only a single seat,
     * which means get_current_seat() will always return the same (and only) seat.
     */
    virtual wlr_seat *get_current_seat() = 0;

    /**
     * @return A bit mask of the currently pressed modifiers
     */
    virtual uint32_t get_keyboard_modifiers() = 0;

    /** Set the cursor to the given name from the cursor theme, if available */
    virtual void set_cursor(std::string name) = 0;
    /** Hides the cursor, until something sets it up again, for ex. by set_cursor()
     * */
    virtual void hide_cursor() = 0;
    /**
     * Move the cursor to a specific position.
     * @param position the new position for the cursor, in global coordinates.
     */
    virtual void warp_cursor(wf::pointf_t position) = 0;

    /** no such coordinate will ever realistically be used for input */
    static constexpr double invalid_coordinate =
        std::numeric_limits<double>::quiet_NaN();

    /**
     * @return The current cursor position in global coordinates or
     * {invalid_coordinate, invalid_coordinate} if no cursor.
     */
    virtual wf::pointf_t get_cursor_position() = 0;

    /**
     * @return The current position of the given touch point, or
     * {invalid_coordinate,invalid_coordinate} if it is not found.
     */
    virtual wf::pointf_t get_touch_position(int id) = 0;

    /**
     * @return The surface which has the cursor focus, or null if none.
     */
    virtual wf::surface_interface_t *get_cursor_focus() = 0;

    /**
     * @return The surface which has touch focus, or null if none.
     */
    virtual wf::surface_interface_t *get_touch_focus() = 0;

    /**
     * @return The surface under the given global coordinates, or null if none.
     */
    virtual wf::surface_interface_t *get_surface_at(wf::pointf_t point) = 0;

    /** @return The view whose surface is cursor focus */
    wayfire_view get_cursor_focus_view();
    /** @return The view whose surface is touch focus */
    wayfire_view get_touch_focus_view();
    /**
     * @return The view whose surface is under the given global coordinates,
     *  or null if none */
    wayfire_view get_view_at(wf::pointf_t point);

    /**
     * @return A list of all currently attached input devices.
     */
    virtual std::vector<nonstd::observer_ptr<wf::input_device_t>> get_input_devices()
    = 0;

    /**
     * @return the wlr_cursor used for the input devices
     */
    virtual wlr_cursor *get_wlr_cursor() = 0;

    /**
     * Register a new touchscreen gesture.
     */
    virtual void add_touch_gesture(
        nonstd::observer_ptr<wf::touch::gesture_t> gesture) = 0;

    /**
     * Unregister a touchscreen gesture.
     */
    virtual void rem_touch_gesture(
        nonstd::observer_ptr<wf::touch::gesture_t> gesture) = 0;

    /**
     * Add a view to the compositor's view list. The view will be freed when
     * its keep_count drops to zero, hence a plugin using this doesn't have to
     * erase the view manually (instead it should just drop the keep_count)
     */
    virtual void add_view(std::unique_ptr<wf::view_interface_t> view) = 0;

    /**
     * @return A list of all views core manages, regardless of their output,
     *  properties, etc.
     */
    virtual std::vector<wayfire_view> get_all_views() = 0;

    /**
     * Set the keyboard focus view. The stacking order on the view's output
     * won't be changed.
     */
    virtual void set_active_view(wayfire_view v) = 0;

    /**
     * Focus the given view and its output (if necessary).
     * Will also bring the view to the top of the stack.
     */
    virtual void focus_view(wayfire_view win) = 0;

    /**
     * Focus the given output. The currently focused output is used to determine
     * which plugins receive various events (including bindings)
     */
    virtual void focus_output(wf::output_t *o) = 0;

    /**
     * Get the currently focused "active" output
     */
    virtual wf::output_t *get_active_output() = 0;

    /**
     * Change the view's output to new_output. If the reconfigure flag is
     * set, it will adjust the view geometry for the new output and clamp
     * it to the output geometry so it is at an expected size and position.
     */
    virtual void move_view_to_output(wayfire_view v,
        wf::output_t *new_output, bool reconfigure) = 0;

    /**
     * Add a request to focus the given layer, or update an existing request.
     * Returns the UID of the request which was added/modified.
     *
     * Calling this with request >= 0 will have no effect if the given
     * request doesn't exist, in which case -1 is returned
     */
    virtual int focus_layer(uint32_t layer, int request) = 0;

    /**
     * Removes a request from the list. No-op for requests that do not exist
     * currently or for request < 0
     */
    virtual void unfocus_layer(int request) = 0;

    /**
     * @return The highest layer for which there exists a focus request, or 0, if
     * no focus requests.
     */
    virtual uint32_t get_focused_layer() = 0;

    /** The wayland socket name of Wayfire */
    std::string wayland_display;

    /**
     * Return the xwayland display name.
     *
     * @return The xwayland display name, or empty string if xwayland is not
     *   available.
     */
    virtual std::string get_xwayland_display() = 0;

    /**
     * Execute the given command in a bash shell.
     *
     * This also sets some environment variables for the new process, including
     * correct WAYLAND_DISPLAY and DISPLAY.
     *
     * @return The PID of the started client, or -1 on failure.
     */
    virtual pid_t run(std::string command) = 0;

    /**
     * Returns a reference to the only core instance.
     */
    static compositor_core_t& get();

  protected:
    compositor_core_t();
    virtual ~compositor_core_t();
};

/**
 * Simply a convenience function to call wf::compositor_core_t::get()
 */
compositor_core_t& get_core();
}

#endif // CORE_HPP
