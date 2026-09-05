#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_hal_usb_hid.h>
#include <extra_profiles/hid_profile.h>

#include <bt/bt_service/bt.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/elements.h>

#define TAG "SpotifyRemote"

#define SPOTIFY_REMOTE_KEYS_PATH APP_DATA_PATH("spotify_remote.keys")

typedef enum {
    SpotifyRemoteViewMain,
} SpotifyRemoteView;

typedef struct {
    Bt* bt;
    Gui* gui;
    NotificationApp* notifications;
    ViewDispatcher* view_dispatcher;
    View* view;
    FuriHalBleProfileBase* ble_hid_profile;
} SpotifyRemote;

typedef struct {
    bool connected;
    bool up_pressed;
    bool down_pressed;
    bool left_pressed;
    bool right_pressed;
    bool ok_pressed;
    bool back_pressed;
} SpotifyRemoteModel;

static void spotify_remote_draw_callback(Canvas* canvas, void* context) {
    SpotifyRemoteModel* model = context;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    elements_multiline_text_aligned(canvas, 3, 2, AlignLeft, AlignTop, "Spotify Remote");

    canvas_set_font(canvas, FontSecondary);
    elements_multiline_text_aligned(
        canvas, 3, 14, AlignLeft, AlignTop, model->connected ? "Connected" : "Advertising...");

    // D-pad ring
    canvas_draw_circle(canvas, 64, 40, 22);

    // Up = volume up
    canvas_draw_box(canvas, 58, 15, 12, 10);
    if(model->up_pressed) {
        canvas_set_color(canvas, ColorWhite);
        elements_multiline_text_aligned(canvas, 64, 20, AlignCenter, AlignCenter, "V+");
        canvas_set_color(canvas, ColorBlack);
    } else {
        elements_multiline_text_aligned(canvas, 64, 20, AlignCenter, AlignCenter, "V+");
    }

    // Down = volume down
    canvas_draw_box(canvas, 58, 55, 12, 10);
    if(model->down_pressed) {
        canvas_set_color(canvas, ColorWhite);
        elements_multiline_text_aligned(canvas, 64, 60, AlignCenter, AlignCenter, "V-");
        canvas_set_color(canvas, ColorBlack);
    } else {
        elements_multiline_text_aligned(canvas, 64, 60, AlignCenter, AlignCenter, "V-");
    }

    // Left = previous track
    canvas_draw_box(canvas, 33, 35, 12, 10);
    if(model->left_pressed) {
        canvas_set_color(canvas, ColorWhite);
        elements_multiline_text_aligned(canvas, 39, 40, AlignCenter, AlignCenter, "|<");
        canvas_set_color(canvas, ColorBlack);
    } else {
        elements_multiline_text_aligned(canvas, 39, 40, AlignCenter, AlignCenter, "|<");
    }

    // Right = next track
    canvas_draw_box(canvas, 83, 35, 12, 10);
    if(model->right_pressed) {
        canvas_set_color(canvas, ColorWhite);
        elements_multiline_text_aligned(canvas, 89, 40, AlignCenter, AlignCenter, ">|");
        canvas_set_color(canvas, ColorBlack);
    } else {
        elements_multiline_text_aligned(canvas, 89, 40, AlignCenter, AlignCenter, ">|");
    }

    // Ok = play/pause
    canvas_draw_disc(canvas, 64, 40, 8);
    canvas_set_color(canvas, model->ok_pressed ? ColorBlack : ColorWhite);
    elements_multiline_text_aligned(canvas, 64, 40, AlignCenter, AlignCenter, "> ||");
    canvas_set_color(canvas, ColorBlack);

    elements_multiline_text_aligned(canvas, 3, 63, AlignLeft, AlignBottom, "Hold BACK to exit");
}

static void spotify_remote_press(SpotifyRemote* app, InputEvent* event) {
    with_view_model(
        app->view,
        SpotifyRemoteModel * model,
        {
            if(event->key == InputKeyUp) {
                model->up_pressed = true;
                ble_profile_hid_consumer_key_press(
                    app->ble_hid_profile, HID_CONSUMER_VOLUME_INCREMENT);
            } else if(event->key == InputKeyDown) {
                model->down_pressed = true;
                ble_profile_hid_consumer_key_press(
                    app->ble_hid_profile, HID_CONSUMER_VOLUME_DECREMENT);
            } else if(event->key == InputKeyLeft) {
                model->left_pressed = true;
                ble_profile_hid_consumer_key_press(
                    app->ble_hid_profile, HID_CONSUMER_SCAN_PREVIOUS_TRACK);
            } else if(event->key == InputKeyRight) {
                model->right_pressed = true;
                ble_profile_hid_consumer_key_press(
                    app->ble_hid_profile, HID_CONSUMER_SCAN_NEXT_TRACK);
            } else if(event->key == InputKeyOk) {
                model->ok_pressed = true;
                ble_profile_hid_consumer_key_press(app->ble_hid_profile, HID_CONSUMER_PLAY_PAUSE);
            } else if(event->key == InputKeyBack) {
                model->back_pressed = true;
            }
        },
        true);
}

static void spotify_remote_release(SpotifyRemote* app, InputEvent* event) {
    with_view_model(
        app->view,
        SpotifyRemoteModel * model,
        {
            if(event->key == InputKeyUp) {
                model->up_pressed = false;
                ble_profile_hid_consumer_key_release(
                    app->ble_hid_profile, HID_CONSUMER_VOLUME_INCREMENT);
            } else if(event->key == InputKeyDown) {
                model->down_pressed = false;
                ble_profile_hid_consumer_key_release(
                    app->ble_hid_profile, HID_CONSUMER_VOLUME_DECREMENT);
            } else if(event->key == InputKeyLeft) {
                model->left_pressed = false;
                ble_profile_hid_consumer_key_release(
                    app->ble_hid_profile, HID_CONSUMER_SCAN_PREVIOUS_TRACK);
            } else if(event->key == InputKeyRight) {
                model->right_pressed = false;
                ble_profile_hid_consumer_key_release(
                    app->ble_hid_profile, HID_CONSUMER_SCAN_NEXT_TRACK);
            } else if(event->key == InputKeyOk) {
                model->ok_pressed = false;
                ble_profile_hid_consumer_key_release(app->ble_hid_profile, HID_CONSUMER_PLAY_PAUSE);
            } else if(event->key == InputKeyBack) {
                model->back_pressed = false;
            }
        },
        true);
}

static bool spotify_remote_input_callback(InputEvent* event, void* context) {
    SpotifyRemote* app = context;
    bool consumed = false;

    if(event->type == InputTypeLong && event->key == InputKeyBack) {
        ble_profile_hid_consumer_key_release_all(app->ble_hid_profile);
        view_dispatcher_stop(app->view_dispatcher);
    } else {
        consumed = true;
        if(event->type == InputTypePress) {
            spotify_remote_press(app, event);
        } else if(event->type == InputTypeRelease) {
            spotify_remote_release(app, event);
        }
    }

    return consumed;
}

static void spotify_remote_bt_status_callback(BtStatus status, void* context) {
    SpotifyRemote* app = context;
    bool connected = (status == BtStatusConnected);
    notification_internal_message(
        app->notifications, connected ? &sequence_set_blue_255 : &sequence_reset_blue);
    with_view_model(
        app->view, SpotifyRemoteModel * model, { model->connected = connected; }, true);
}

static SpotifyRemote* spotify_remote_alloc(void) {
    SpotifyRemote* app = malloc(sizeof(SpotifyRemote));

    app->gui = furi_record_open(RECORD_GUI);
    app->bt = furi_record_open(RECORD_BT);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->view = view_alloc();
    view_set_context(app->view, app);
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(SpotifyRemoteModel));
    view_set_draw_callback(app->view, spotify_remote_draw_callback);
    view_set_input_callback(app->view, spotify_remote_input_callback);

    view_dispatcher_add_view(app->view_dispatcher, SpotifyRemoteViewMain, app->view);
    view_dispatcher_switch_to_view(app->view_dispatcher, SpotifyRemoteViewMain);

    return app;
}

static void spotify_remote_free(SpotifyRemote* app) {
    notification_internal_message(app->notifications, &sequence_reset_blue);

    view_dispatcher_remove_view(app->view_dispatcher, SpotifyRemoteViewMain);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t spotify_remote_app(void* p) {
    UNUSED(p);
    SpotifyRemote* app = spotify_remote_alloc();

    bt_disconnect(app->bt);
    furi_delay_ms(200);

    bt_keys_storage_set_storage_path(app->bt, SPOTIFY_REMOTE_KEYS_PATH);

    BleProfileHidParams hid_params = {
        .device_name_prefix = "Spotify",
        .mac_xor = 0x5F17,
    };
    app->ble_hid_profile = bt_profile_start(app->bt, ble_profile_hid, &hid_params);
    furi_check(app->ble_hid_profile);

    furi_hal_bt_start_advertising();
    bt_set_status_changed_callback(app->bt, spotify_remote_bt_status_callback, app);

    view_dispatcher_run(app->view_dispatcher);

    bt_set_status_changed_callback(app->bt, NULL, NULL);
    bt_disconnect(app->bt);
    furi_delay_ms(200);

    bt_keys_storage_set_default_path(app->bt);
    furi_check(bt_profile_restore_default(app->bt));

    spotify_remote_free(app);
    return 0;
}
