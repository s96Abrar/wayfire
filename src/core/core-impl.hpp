#ifndef WF_CORE_CORE_IMPL_HPP
#define WF_CORE_CORE_IMPL_HPP

#include "wayfire/core.hpp"
#include "wayfire/util.hpp"

#include <set>
#include <unordered_map>

extern "C"
{
    struct wlr_egl;
    struct wlr_compositor;
    struct wlr_surface;
}

class input_manager;
class input_method_relay;
struct wayfire_shell;
struct wf_gtk_shell;

namespace wf
{
class compositor_core_impl_t : public compositor_core_t
{
  public:
    /**
     * When we get a request for setting CSD, the view might not have been
     * created. So, we store all requests in core, and the views pick this
     * information when they are created
     */
    std::unordered_map<wlr_surface*, uint32_t> uses_csd;

    wlr_egl *egl;
    wlr_compositor *compositor;

    std::unique_ptr<input_manager> input;
    std::unique_ptr<input_method_relay> im_relay;

    /**
     * Initialize the compositor core. Called only by main()
     */
    void init();
    wayfire_shell *wf_shell;
    wf_gtk_shell *gtk_shell;

    /**
     * Remove a view from the compositor list. This is called when the view's
     * keep_count reaches zero for the first time after its creation.
     */
    void erase_view(wayfire_view view);

    static compositor_core_impl_t& get();

    wlr_seat *get_current_seat() override;
    uint32_t get_keyboard_modifiers() override;
    void set_cursor(std::string name) override;
    void hide_cursor() override;
    void warp_cursor(wf::pointf_t pos) override;

    wf::pointf_t get_cursor_position() override;
    wf::pointf_t get_touch_position(int id) override;

    wf::surface_interface_t *get_cursor_focus() override;
    wf::surface_interface_t *get_touch_focus() override;
    wf::surface_interface_t *get_surface_at(wf::pointf_t point) override;

    void add_touch_gesture(
        nonstd::observer_ptr<wf::touch::gesture_t> gesture) override;
    void rem_touch_gesture(
        nonstd::observer_ptr<wf::touch::gesture_t> gesture) override;

    std::vector<nonstd::observer_ptr<wf::input_device_t>> get_input_devices()
    override;
    virtual wlr_cursor *get_wlr_cursor() override;

    void add_view(std::unique_ptr<wf::view_interface_t> view) override;
    std::vector<wayfire_view> get_all_views() override;
    void set_active_view(wayfire_view v) override;
    void focus_view(wayfire_view win) override;
    void move_view_to_output(wayfire_view v, wf::output_t *new_output,
        bool reconfigure) override;

    void focus_output(wf::output_t *o) override;
    wf::output_t *get_active_output() override;
    int focus_layer(uint32_t layer, int request) override;
    void unfocus_layer(int request) override;
    uint32_t get_focused_layer() override;
    std::string get_xwayland_display() override;
    pid_t run(std::string command) override;

  private:
    wf::wl_listener_wrapper decoration_created;
    wf::wl_listener_wrapper xdg_decoration_created;
    wf::wl_listener_wrapper vkbd_created;
    wf::wl_listener_wrapper vptr_created;
    wf::wl_listener_wrapper input_inhibit_activated;
    wf::wl_listener_wrapper input_inhibit_deactivated;
    wf::wl_listener_wrapper pointer_constraint_added;

    wf::output_t *active_output = nullptr;
    std::vector<std::unique_ptr<wf::view_interface_t>> views;

    /* pairs (layer, request_id) */
    std::set<std::pair<uint32_t, int>> layer_focus_requests;

    wayfire_view last_active_toplevel;

    /**
     * The last view which was attempted to be focused.
     * The view might not actually have focus, because of plugin grabs.
     */
    wayfire_view last_active_view;

    compositor_core_impl_t();
    virtual ~compositor_core_impl_t();
};

compositor_core_impl_t& get_core_impl();
}


#endif /* end of include guard: WF_CORE_CORE_IMPL_HPP */
