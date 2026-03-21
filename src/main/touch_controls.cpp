#include "banjo_touch_controls.h"

#ifdef __ANDROID__

#include "banjo_config.h"
#include "recompinput/input_state.h"
#include "recompinput/input_types.h"
#include "recompui/recompui.h"
#include "recompui/config.h"
#include "core/ui_context.h"
#include "elements/ui_element.h"
#include "elements/ui_label.h"
#include "ultramodern/ultramodern.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace banjo::touch_controls {
namespace {
    enum class TouchTarget {
        None,
        LeftStick,
        A,
        B,
        Z,
        R,
        Start,
        CPad,
        Config,
    };

    struct Circle {
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.0f;

        bool contains(float px, float py, float extra = 0.0f) const {
            float dx = px - x;
            float dy = py - y;
            float max_radius = radius + extra;
            return (dx * dx + dy * dy) <= (max_radius * max_radius);
        }
    };

    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        bool contains(float px, float py, float extra = 0.0f) const {
            return px >= (x - extra) && px <= (x + width + extra) && py >= (y - extra) && py <= (y + height + extra);
        }
    };

    struct Layout {
        int width_px = 0;
        int height_px = 0;
        float dp_to_px = 1.0f;
        Circle left_stick;
        Circle left_knob;
        Circle button_a;
        Circle button_b;
        Circle button_z;
        Circle button_r;
        Circle button_start;
        Circle c_up;
        Circle c_down;
        Circle c_left;
        Circle c_right;
        Circle c_center;
        float c_pad_radius = 0.0f;
        Rect config;
        float button_font_size_dp = 0.0f;
        float small_button_font_size_dp = 0.0f;
        float overlay_opacity = 0.55f;
    };

    struct ActiveTouch {
        TouchTarget target = TouchTarget::None;
        float x_px = 0.0f;
        float y_px = 0.0f;
        uint64_t sequence = 0;
    };

    struct VisualButton {
        recompui::Element* element = nullptr;
        recompui::Label* label = nullptr;
    };

    struct TouchInputSnapshot {
        std::array<bool, recompinput::num_game_inputs> buttons{};
        float stick_x = 0.0f;
        float stick_y = 0.0f;
    };

    struct State {
        std::atomic_bool initialized = false;
        bool event_watch_installed = false;
        bool context_shown = false;
        bool pending_open_config = false;
        uint64_t next_sequence = 1;
        recompui::ContextId context = recompui::ContextId::null();
        recompui::Element* root = nullptr;
        recompui::Element* left_stick_base = nullptr;
        recompui::Element* left_stick_knob = nullptr;
        VisualButton button_a;
        VisualButton button_b;
        VisualButton button_z;
        VisualButton button_r;
        VisualButton button_start;
        VisualButton c_up;
        VisualButton c_down;
        VisualButton c_left;
        VisualButton c_right;
        VisualButton config;
        Layout layout{};
        std::unordered_map<SDL_FingerID, ActiveTouch> active_touches;
        std::mutex mutex;
    } state;

    float to_dp(float px) {
        return px / std::max(state.layout.dp_to_px, 0.0001f);
    }

    Layout compute_layout(int width_px, int height_px, float dp_to_px) {
        Layout layout{};
        layout.width_px = width_px;
        layout.height_px = height_px;
        layout.dp_to_px = std::max(dp_to_px, 1.0f);

        float short_side = static_cast<float>(std::min(width_px, height_px));
        float margin = std::clamp(short_side * 0.035f, 18.0f * layout.dp_to_px, 42.0f * layout.dp_to_px);
        float stick_radius = std::clamp(short_side * 0.12f, 56.0f * layout.dp_to_px, 104.0f * layout.dp_to_px);
        float button_radius = std::clamp(short_side * 0.07f, 32.0f * layout.dp_to_px, 58.0f * layout.dp_to_px);
        float c_button_radius = button_radius * 0.74f;

        layout.left_stick = {
            margin + stick_radius,
            static_cast<float>(height_px) - margin - stick_radius,
            stick_radius
        };
        layout.left_knob = {
            layout.left_stick.x,
            layout.left_stick.y,
            stick_radius * 0.42f
        };

        Circle action_center = {
            static_cast<float>(width_px) - margin - button_radius * 2.0f,
            static_cast<float>(height_px) - margin - button_radius * 1.5f,
            button_radius
        };

        layout.button_a = { action_center.x + button_radius * 1.05f, action_center.y, button_radius };
        layout.button_b = { action_center.x - button_radius * 0.20f, action_center.y + button_radius * 0.95f, button_radius };
        layout.button_z = { action_center.x - button_radius * 1.45f, action_center.y + button_radius * 0.60f, button_radius };
        layout.button_r = { action_center.x - button_radius * 0.15f, action_center.y - button_radius * 1.30f, button_radius };
        layout.button_start = { action_center.x - button_radius * 2.25f, action_center.y - button_radius * 1.05f, button_radius * 0.92f };

        layout.c_center = {
            action_center.x - button_radius * 0.95f,
            action_center.y - button_radius * 3.05f,
            c_button_radius
        };
        layout.c_up = { layout.c_center.x, layout.c_center.y - c_button_radius * 1.25f, c_button_radius };
        layout.c_down = { layout.c_center.x, layout.c_center.y + c_button_radius * 1.25f, c_button_radius };
        layout.c_left = { layout.c_center.x - c_button_radius * 1.25f, layout.c_center.y, c_button_radius };
        layout.c_right = { layout.c_center.x + c_button_radius * 1.25f, layout.c_center.y, c_button_radius };
        layout.c_pad_radius = c_button_radius * 2.35f;

        float config_width = std::clamp(short_side * 0.18f, 72.0f * layout.dp_to_px, 116.0f * layout.dp_to_px);
        float config_height = std::clamp(short_side * 0.085f, 34.0f * layout.dp_to_px, 48.0f * layout.dp_to_px);
        layout.config = {
            static_cast<float>(width_px) - margin - config_width,
            margin,
            config_width,
            config_height
        };

        layout.button_font_size_dp = std::clamp(button_radius / layout.dp_to_px * 0.52f, 18.0f, 30.0f);
        layout.small_button_font_size_dp = std::clamp(c_button_radius / layout.dp_to_px * 0.48f, 14.0f, 22.0f);
        return layout;
    }

    VisualButton create_button(recompui::Element* parent, const char* text) {
        auto context = recompui::get_current_context();
        auto* element = context.create_element<recompui::Element>(parent);
        element->set_position(recompui::Position::Absolute);
        element->set_display(recompui::Display::Flex);
        element->set_align_items(recompui::AlignItems::Center);
        element->set_justify_content(recompui::JustifyContent::Center);
        element->set_border_width(2.0f);
        element->set_border_color(recompui::theme::color::WhiteA50);
        element->set_background_color(recompui::theme::color::BW25);

        auto* label = context.create_element<recompui::Label>(element, text, recompui::theme::Typography::LabelMD);
        label->set_color(recompui::theme::color::Text);
        label->set_text_align(recompui::TextAlign::Center);
        label->set_white_space(recompui::WhiteSpace::Nowrap);

        return { element, label };
    }

    void apply_circle_visual(const VisualButton& button, const Circle& circle, float font_size_dp, bool active, recompui::theme::color active_color) {
        if (button.element == nullptr || button.label == nullptr) {
            return;
        }

        float diameter_dp = to_dp(circle.radius * 2.0f);
        button.element->set_left(to_dp(circle.x - circle.radius));
        button.element->set_top(to_dp(circle.y - circle.radius));
        button.element->set_width(diameter_dp);
        button.element->set_height(diameter_dp);
        button.element->set_border_radius(diameter_dp * 0.5f);
        button.element->set_background_color(active ? active_color : recompui::theme::color::BW25);
        button.element->set_border_color(active ? recompui::theme::color::WhiteA80 : recompui::theme::color::WhiteA50);
        button.label->set_font_size(font_size_dp);
        button.label->set_line_height(font_size_dp * 1.1f);
    }

    void apply_rect_visual(const VisualButton& button, const Rect& rect, float font_size_dp, bool active) {
        if (button.element == nullptr || button.label == nullptr) {
            return;
        }

        button.element->set_left(to_dp(rect.x));
        button.element->set_top(to_dp(rect.y));
        button.element->set_width(to_dp(rect.width));
        button.element->set_height(to_dp(rect.height));
        button.element->set_border_radius(to_dp(rect.height * 0.5f));
        button.element->set_background_color(active ? recompui::theme::color::PrimaryA50 : recompui::theme::color::BW25);
        button.element->set_border_color(active ? recompui::theme::color::WhiteA80 : recompui::theme::color::WhiteA50);
        button.label->set_font_size(font_size_dp);
        button.label->set_line_height(font_size_dp * 1.1f);
    }

    void clear_active_touches_locked() {
        state.active_touches.clear();
        state.pending_open_config = false;
    }

    void set_snapshot_button(TouchInputSnapshot& snapshot, recompinput::GameInput input) {
        size_t input_index = static_cast<size_t>(input);
        if (input_index < snapshot.buttons.size()) {
            snapshot.buttons[input_index] = true;
        }
    }

    void update_touch_position(ActiveTouch& touch, const SDL_TouchFingerEvent& finger_event) {
        touch.x_px = finger_event.x * state.layout.width_px;
        touch.y_px = finger_event.y * state.layout.height_px;
    }

    TouchTarget pick_target(float x_px, float y_px) {
        if (state.layout.config.contains(x_px, y_px, 12.0f * state.layout.dp_to_px)) {
            return TouchTarget::Config;
        }
        if (state.layout.button_a.contains(x_px, y_px, state.layout.button_a.radius * 0.35f)) {
            return TouchTarget::A;
        }
        if (state.layout.button_b.contains(x_px, y_px, state.layout.button_b.radius * 0.35f)) {
            return TouchTarget::B;
        }
        if (state.layout.button_z.contains(x_px, y_px, state.layout.button_z.radius * 0.35f)) {
            return TouchTarget::Z;
        }
        if (state.layout.button_r.contains(x_px, y_px, state.layout.button_r.radius * 0.35f)) {
            return TouchTarget::R;
        }
        if (state.layout.button_start.contains(x_px, y_px, state.layout.button_start.radius * 0.45f)) {
            return TouchTarget::Start;
        }
        if (state.layout.c_center.contains(x_px, y_px, state.layout.c_pad_radius)) {
            return TouchTarget::CPad;
        }
        if (state.layout.left_stick.contains(x_px, y_px, state.layout.left_stick.radius * 0.45f)) {
            return TouchTarget::LeftStick;
        }
        return TouchTarget::None;
    }

    TouchInputSnapshot compute_touch_input_locked() {
        TouchInputSnapshot snapshot{};
        const ActiveTouch* stick_touch = nullptr;
        for (const auto& [finger_id, touch] : state.active_touches) {
            (void)finger_id;
            if (touch.target == TouchTarget::LeftStick &&
                (stick_touch == nullptr || touch.sequence > stick_touch->sequence)) {
                stick_touch = &touch;
            }
        }

        if (stick_touch != nullptr) {
            float dx = (stick_touch->x_px - state.layout.left_stick.x) / state.layout.left_stick.radius;
            float dy = (state.layout.left_stick.y - stick_touch->y_px) / state.layout.left_stick.radius;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length > 1.0f) {
                dx /= length;
                dy /= length;
                length = 1.0f;
            }

            constexpr float deadzone = 0.14f;
            if (length < deadzone) {
                dx = 0.0f;
                dy = 0.0f;
            }
            else if (length > 0.0f) {
                float scaled_length = (length - deadzone) / (1.0f - deadzone);
                dx = dx / length * scaled_length;
                dy = dy / length * scaled_length;
            }

            snapshot.stick_x = dx;
            snapshot.stick_y = dy;
        }

        for (const auto& [finger_id, touch] : state.active_touches) {
            (void)finger_id;
            switch (touch.target) {
            case TouchTarget::A:
                if (state.layout.button_a.contains(touch.x_px, touch.y_px, state.layout.button_a.radius * 0.55f)) {
                    set_snapshot_button(snapshot, recompinput::GameInput::A);
                }
                break;
            case TouchTarget::B:
                if (state.layout.button_b.contains(touch.x_px, touch.y_px, state.layout.button_b.radius * 0.55f)) {
                    set_snapshot_button(snapshot, recompinput::GameInput::B);
                }
                break;
            case TouchTarget::Z:
                if (state.layout.button_z.contains(touch.x_px, touch.y_px, state.layout.button_z.radius * 0.55f)) {
                    set_snapshot_button(snapshot, recompinput::GameInput::Z);
                }
                break;
            case TouchTarget::R:
                if (state.layout.button_r.contains(touch.x_px, touch.y_px, state.layout.button_r.radius * 0.55f)) {
                    set_snapshot_button(snapshot, recompinput::GameInput::R);
                }
                break;
            case TouchTarget::Start:
                if (state.layout.button_start.contains(touch.x_px, touch.y_px, state.layout.button_start.radius * 0.65f)) {
                    set_snapshot_button(snapshot, recompinput::GameInput::START);
                }
                break;
            case TouchTarget::CPad:
                if (state.layout.c_center.contains(touch.x_px, touch.y_px, state.layout.c_pad_radius)) {
                    float dx = touch.x_px - state.layout.c_center.x;
                    float dy = touch.y_px - state.layout.c_center.y;
                    float center_deadzone = state.layout.c_center.radius * 0.45f;
                    if ((dx * dx + dy * dy) >= (center_deadzone * center_deadzone)) {
                        if (std::fabs(dx) > std::fabs(dy)) {
                            set_snapshot_button(snapshot, dx > 0.0f ? recompinput::GameInput::C_RIGHT : recompinput::GameInput::C_LEFT);
                        }
                        else {
                            set_snapshot_button(snapshot, dy > 0.0f ? recompinput::GameInput::C_DOWN : recompinput::GameInput::C_UP);
                        }
                    }
                }
                break;
            default:
                break;
            }
        }

        return snapshot;
    }

    bool overlay_enabled_now() {
        if (!ultramodern::is_game_started() || recompui::is_context_capturing_input()) {
            return false;
        }

        if (banjo::get_hide_onscreen_controls_with_controller() && recompinput::has_connected_controllers()) {
            return false;
        }

        return true;
    }

    int touch_event_watch(void*, SDL_Event* event) {
        if (!state.initialized || !overlay_enabled_now()) {
            return 1;
        }

        TouchInputSnapshot snapshot{};
        bool snapshot_valid = false;
        {
            std::lock_guard lock{ state.mutex };
            switch (event->type) {
            case SDL_EventType::SDL_FINGERDOWN: {
                auto& finger_event = event->tfinger;
                float x_px = finger_event.x * state.layout.width_px;
                float y_px = finger_event.y * state.layout.height_px;
                TouchTarget target = pick_target(x_px, y_px);
                if (target == TouchTarget::None) {
                    return 1;
                }

                ActiveTouch& touch = state.active_touches[finger_event.fingerId];
                touch.target = target;
                touch.sequence = state.next_sequence++;
                update_touch_position(touch, finger_event);
                snapshot = compute_touch_input_locked();
                snapshot_valid = true;
                break;
            }
            case SDL_EventType::SDL_FINGERMOTION: {
                auto it = state.active_touches.find(event->tfinger.fingerId);
                if (it == state.active_touches.end()) {
                    return 1;
                }

                update_touch_position(it->second, event->tfinger);
                snapshot = compute_touch_input_locked();
                snapshot_valid = true;
                break;
            }
            case SDL_EventType::SDL_FINGERUP: {
                auto it = state.active_touches.find(event->tfinger.fingerId);
                if (it == state.active_touches.end()) {
                    return 1;
                }

                update_touch_position(it->second, event->tfinger);
                if (it->second.target == TouchTarget::Config &&
                    state.layout.config.contains(it->second.x_px, it->second.y_px, 12.0f * state.layout.dp_to_px)) {
                    state.pending_open_config = true;
                }
                state.active_touches.erase(it);
                snapshot = compute_touch_input_locked();
                snapshot_valid = true;
                break;
            }
            default:
                break;
            }

            if (snapshot_valid) {
                recompinput::set_touch_input_state(snapshot.buttons, snapshot.stick_x, snapshot.stick_y);
            }
        }

        return 1;
    }

    void ensure_initialized() {
        if (state.initialized || !ultramodern::is_game_started()) {
            return;
        }

        state.context = recompui::create_context();
        state.context.set_captures_input(false);
        state.context.set_captures_mouse(false);
        state.context.open();

        state.root = state.context.create_element<recompui::Element>(state.context.get_root_element());
        state.root->set_position(recompui::Position::Absolute);
        state.root->set_top(0.0f);
        state.root->set_left(0.0f);
        state.root->set_width(100.0f, recompui::Unit::Percent);
        state.root->set_height(100.0f, recompui::Unit::Percent);

        state.left_stick_base = state.context.create_element<recompui::Element>(state.root);
        state.left_stick_base->set_position(recompui::Position::Absolute);
        state.left_stick_base->set_border_width(2.0f);
        state.left_stick_base->set_border_color(recompui::theme::color::WhiteA30);
        state.left_stick_base->set_background_color(recompui::theme::color::BW10);

        state.left_stick_knob = state.context.create_element<recompui::Element>(state.root);
        state.left_stick_knob->set_position(recompui::Position::Absolute);
        state.left_stick_knob->set_border_width(2.0f);
        state.left_stick_knob->set_border_color(recompui::theme::color::WhiteA50);
        state.left_stick_knob->set_background_color(recompui::theme::color::PrimaryA30);

        state.button_a = create_button(state.root, "A");
        state.button_b = create_button(state.root, "B");
        state.button_z = create_button(state.root, "Z");
        state.button_r = create_button(state.root, "R");
        state.button_start = create_button(state.root, "Start");
        state.c_up = create_button(state.root, "CU");
        state.c_down = create_button(state.root, "CD");
        state.c_left = create_button(state.root, "CL");
        state.c_right = create_button(state.root, "CR");
        state.config = create_button(state.root, "Menu");

        int width_px = 0;
        int height_px = 0;
        recompui::get_window_size(width_px, height_px);
        if (width_px > 0 && height_px > 0) {
            state.layout = compute_layout(width_px, height_px, std::max(state.root->get_dp_to_pixel_ratio(), 1.0f));
        }

        if (!state.event_watch_installed) {
            SDL_AddEventWatch(touch_event_watch, nullptr);
            state.event_watch_installed = true;
        }

        state.context.close();
        state.initialized = true;
    }
}

void update() {
    ensure_initialized();
    if (!state.initialized) {
        return;
    }

    int width_px = 0;
    int height_px = 0;
    recompui::get_window_size(width_px, height_px);
    if (width_px <= 0 || height_px <= 0) {
        return;
    }

    bool should_show = overlay_enabled_now();
    bool context_shown = recompui::is_context_shown(state.context);
    bool controller_dimmed = recompui::get_cont_active();
    TouchInputSnapshot touch_snapshot{};
    bool active_touches_empty = true;
    bool pending_open_config = false;
    bool sync_touch_input = false;

    // Avoid holding the overlay context lock while querying global UI capture state, as draw_hook
    // takes ui_state_mutex before opening shown contexts and can deadlock with this update path.
    state.context.open();
    float dp_to_px = std::max(state.root->get_dp_to_pixel_ratio(), 1.0f);
    Layout layout = compute_layout(width_px, height_px, dp_to_px);

    if (should_show && !context_shown) {
        state.context.close();
        recompui::show_context(state.context, "");
        state.context.open();
        context_shown = true;
    }
    else if (!should_show && context_shown) {
        state.context.close();
        recompui::hide_context(state.context);
        state.context.open();
        context_shown = false;
    }
    state.context_shown = context_shown;
    {
        std::lock_guard lock{ state.mutex };
        state.layout = layout;
        if (!should_show) {
            clear_active_touches_locked();
            sync_touch_input = true;
        }
        active_touches_empty = state.active_touches.empty();
        pending_open_config = state.pending_open_config;
    }

    if (sync_touch_input) {
        recompinput::set_touch_input_state(touch_snapshot.buttons, touch_snapshot.stick_x, touch_snapshot.stick_y);
    }
    else {
        recompinput::get_touch_input_state(touch_snapshot.buttons, &touch_snapshot.stick_x, &touch_snapshot.stick_y);
    }

    float knob_x = layout.left_stick.x + touch_snapshot.stick_x * layout.left_stick.radius * 0.55f;
    float knob_y = layout.left_stick.y - touch_snapshot.stick_y * layout.left_stick.radius * 0.55f;

    state.root->set_opacity((controller_dimmed && active_touches_empty) ? 0.28f : layout.overlay_opacity);

    float stick_diameter_dp = to_dp(layout.left_stick.radius * 2.0f);
    state.left_stick_base->set_left(to_dp(layout.left_stick.x - layout.left_stick.radius));
    state.left_stick_base->set_top(to_dp(layout.left_stick.y - layout.left_stick.radius));
    state.left_stick_base->set_width(stick_diameter_dp);
    state.left_stick_base->set_height(stick_diameter_dp);
    state.left_stick_base->set_border_radius(stick_diameter_dp * 0.5f);

    float knob_diameter_dp = to_dp(layout.left_knob.radius * 2.0f);
    state.left_stick_knob->set_left(to_dp(knob_x - layout.left_knob.radius));
    state.left_stick_knob->set_top(to_dp(knob_y - layout.left_knob.radius));
    state.left_stick_knob->set_width(knob_diameter_dp);
    state.left_stick_knob->set_height(knob_diameter_dp);
    state.left_stick_knob->set_border_radius(knob_diameter_dp * 0.5f);
    state.left_stick_knob->set_background_color((touch_snapshot.stick_x != 0.0f || touch_snapshot.stick_y != 0.0f) ? recompui::theme::color::PrimaryA50 : recompui::theme::color::PrimaryA30);

    apply_circle_visual(state.button_a, layout.button_a, layout.button_font_size_dp, touch_snapshot.buttons[static_cast<size_t>(recompinput::GameInput::A)], recompui::theme::color::SuccessA50);
    apply_circle_visual(state.button_b, layout.button_b, layout.button_font_size_dp, touch_snapshot.buttons[static_cast<size_t>(recompinput::GameInput::B)], recompui::theme::color::WarningA50);
    apply_circle_visual(state.button_z, layout.button_z, layout.button_font_size_dp, touch_snapshot.buttons[static_cast<size_t>(recompinput::GameInput::Z)], recompui::theme::color::PrimaryA50);
    apply_circle_visual(state.button_r, layout.button_r, layout.button_font_size_dp, touch_snapshot.buttons[static_cast<size_t>(recompinput::GameInput::R)], recompui::theme::color::SecondaryA50);
    apply_circle_visual(state.button_start, layout.button_start, std::max(layout.small_button_font_size_dp, 16.0f), touch_snapshot.buttons[static_cast<size_t>(recompinput::GameInput::START)], recompui::theme::color::PrimaryA50);
    apply_circle_visual(state.c_up, layout.c_up, layout.small_button_font_size_dp, touch_snapshot.buttons[static_cast<size_t>(recompinput::GameInput::C_UP)], recompui::theme::color::PrimaryA50);
    apply_circle_visual(state.c_down, layout.c_down, layout.small_button_font_size_dp, touch_snapshot.buttons[static_cast<size_t>(recompinput::GameInput::C_DOWN)], recompui::theme::color::PrimaryA50);
    apply_circle_visual(state.c_left, layout.c_left, layout.small_button_font_size_dp, touch_snapshot.buttons[static_cast<size_t>(recompinput::GameInput::C_LEFT)], recompui::theme::color::PrimaryA50);
    apply_circle_visual(state.c_right, layout.c_right, layout.small_button_font_size_dp, touch_snapshot.buttons[static_cast<size_t>(recompinput::GameInput::C_RIGHT)], recompui::theme::color::PrimaryA50);
    apply_rect_visual(state.config, layout.config, 16.0f, pending_open_config);

    state.context.close();

    bool open_config = false;
    bool clear_touch_input = false;
    {
        std::lock_guard lock{ state.mutex };
        if (state.pending_open_config) {
            state.pending_open_config = false;
            clear_active_touches_locked();
            open_config = true;
            clear_touch_input = true;
        }
    }

    if (clear_touch_input) {
        recompinput::set_touch_input_state(TouchInputSnapshot{}.buttons, 0.0f, 0.0f);
    }

    if (open_config) {
        recompui::activate_mouse();
        recompui::config::open();
    }
}

void shutdown() {
    if (state.event_watch_installed) {
        SDL_DelEventWatch(touch_event_watch, nullptr);
        state.event_watch_installed = false;
    }

    recompinput::clear_touch_input();
    if (state.initialized && state.context_shown && recompui::is_context_shown(state.context)) {
        recompui::hide_context(state.context);
    }
    state.context_shown = false;
}

} // namespace banjo::touch_controls

#else

namespace banjo::touch_controls {
void update() {}
void shutdown() {}
}

#endif
