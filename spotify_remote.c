#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_hal_usb_hid.h>
#include <extra_profiles/hid_profile.h>

#include <bt/bt_service/bt.h>
#include <cli/cli.h>
#include <toolbox/args.h>
#include <toolbox/pipe.h>
#include <toolbox/cli/cli_registry.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/elements.h>

#define TAG "SpotifyRemote"

#define SPOTIFY_REMOTE_KEYS_PATH APP_DATA_PATH("spotify_remote.keys")

#define SPOTIFY_ART_MAX_DIM 40
#define SPOTIFY_ART_SLOT_X  2
#define SPOTIFY_ART_SLOT_Y  2
#define SPOTIFY_SCROLL_PERIOD_MS 200

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
    CliRegistry* cli_registry;
    FuriTimer* scroll_timer;
} SpotifyRemote;

typedef struct {
    bool connected;
    bool left_pressed;
    bool right_pressed;
    bool ok_pressed;
    bool back_pressed;

    FuriString* title;
    FuriString* artist;
    bool playing;
    bool has_track;

    uint8_t* art;
    size_t art_w;
    size_t art_h;

    size_t scroll_counter;
} SpotifyRemoteModel;

static void spotify_remote_draw_art_placeholder(Canvas* canvas) {
    elements_slightly_rounded_frame(
        canvas, SPOTIFY_ART_SLOT_X, SPOTIFY_ART_SLOT_Y, SPOTIFY_ART_MAX_DIM, SPOTIFY_ART_MAX_DIM);
    // Crude eighth-note glyph as a placeholder
    canvas_draw_disc(canvas, SPOTIFY_ART_SLOT_X + 12, SPOTIFY_ART_SLOT_Y + 28, 4);
    canvas_draw_line(
        canvas,
        SPOTIFY_ART_SLOT_X + 16,
        SPOTIFY_ART_SLOT_Y + 28,
        SPOTIFY_ART_SLOT_X + 16,
        SPOTIFY_ART_SLOT_Y + 8);
    canvas_draw_line(
        canvas,
        SPOTIFY_ART_SLOT_X + 16,
        SPOTIFY_ART_SLOT_Y + 8,
        SPOTIFY_ART_SLOT_X + 26,
        SPOTIFY_ART_SLOT_Y + 11);
    canvas_draw_line(
        canvas,
        SPOTIFY_ART_SLOT_X + 26,
        SPOTIFY_ART_SLOT_Y + 11,
        SPOTIFY_ART_SLOT_X + 26,
        SPOTIFY_ART_SLOT_Y + 18);
}

static void spotify_remote_draw_button(
    Canvas* canvas,
    int32_t cx,
    int32_t y,
    size_t width,
    const char* label,
    bool pressed) {
    int32_t x = cx - (int32_t)width / 2;
    if(pressed) {
        canvas_draw_box(canvas, x, y, width, 11);
        canvas_set_color(canvas, ColorWhite);
        elements_multiline_text_aligned(
            canvas, cx, y + 5, AlignCenter, AlignCenter, label);
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_frame(canvas, x, y, width, 11);
        elements_multiline_text_aligned(
            canvas, cx, y + 5, AlignCenter, AlignCenter, label);
    }
}

static void spotify_remote_draw_callback(Canvas* canvas, void* context) {
    SpotifyRemoteModel* model = context;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    // Album art / placeholder
    if(model->art && model->art_w > 0 && model->art_h > 0) {
        canvas_draw_xbm(
            canvas, SPOTIFY_ART_SLOT_X, SPOTIFY_ART_SLOT_Y, model->art_w, model->art_h, model->art);
    } else {
        spotify_remote_draw_art_placeholder(canvas);
    }

    // BLE connection dot, top-right
    if(model->connected) {
        canvas_draw_disc(canvas, 123, 4, 3);
    } else {
        canvas_draw_circle(canvas, 123, 4, 3);
    }

    // Title / artist, right of the art
    int32_t text_x = SPOTIFY_ART_SLOT_X + SPOTIFY_ART_MAX_DIM + 4;
    size_t text_width = 128 - (size_t)text_x - 8;

    canvas_set_font(canvas, FontPrimary);
    if(model->has_track) {
        elements_scrollable_text_line(
            canvas, text_x, 10, text_width, model->title, model->scroll_counter, true);
    } else {
        elements_multiline_text_aligned(
            canvas, text_x, 6, AlignLeft, AlignTop, "Not Playing");
    }

    canvas_set_font(canvas, FontSecondary);
    if(model->has_track) {
        elements_scrollable_text_line(
            canvas, text_x, 24, text_width, model->artist, model->scroll_counter, true);
    } else {
        elements_multiline_text_aligned(
            canvas, text_x, 20, AlignLeft, AlignTop, "Run companion script");
    }

    // Transport row
    const int32_t row_y = SPOTIFY_ART_SLOT_Y + SPOTIFY_ART_MAX_DIM + 4;
    spotify_remote_draw_button(canvas, 20, row_y, 24, "<<", model->left_pressed);
    spotify_remote_draw_button(
        canvas, 64, row_y, 24, model->playing ? "||" : ">", model->ok_pressed);
    spotify_remote_draw_button(canvas, 108, row_y, 24, ">>", model->right_pressed);

    canvas_set_font(canvas, FontSecondary);
    elements_multiline_text_aligned(
        canvas, 2, 63, AlignLeft, AlignBottom, "Vol:Up/Dn  Hold BACK: exit");
}

static void spotify_remote_press(SpotifyRemote* app, InputEvent* event) {
    with_view_model(
        app->view,
        SpotifyRemoteModel * model,
        {
            if(event->key == InputKeyUp) {
                ble_profile_hid_consumer_key_press(
                    app->ble_hid_profile, HID_CONSUMER_VOLUME_INCREMENT);
            } else if(event->key == InputKeyDown) {
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
                ble_profile_hid_consumer_key_release(
                    app->ble_hid_profile, HID_CONSUMER_VOLUME_INCREMENT);
            } else if(event->key == InputKeyDown) {
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

static void spotify_remote_scroll_timer_callback(void* context) {
    SpotifyRemote* app = context;
    with_view_model(
        app->view, SpotifyRemoteModel * model, { model->scroll_counter++; }, true);
}

// --- CLI: `spotify_np "title" "artist" [playing|paused]` ---
static void spotify_remote_cli_np(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    SpotifyRemote* app = context;

    FuriString* title = furi_string_alloc();
    FuriString* artist = furi_string_alloc();
    FuriString* state = furi_string_alloc();

    bool ok = args_read_probably_quoted_string_and_trim(args, title) &&
              args_read_probably_quoted_string_and_trim(args, artist);
    bool playing = true;
    if(ok && args_read_string_and_trim(args, state)) {
        playing = furi_string_cmp_str(state, "paused") != 0;
    }

    if(ok) {
        with_view_model(
            app->view,
            SpotifyRemoteModel * model,
            {
                furi_string_set(model->title, title);
                furi_string_set(model->artist, artist);
                model->playing = playing;
                model->has_track = true;
                model->scroll_counter = 0;
            },
            true);
        printf("OK\r\n");
    } else {
        printf("Usage: spotify_np \"title\" \"artist\" [playing|paused]\r\n");
    }

    furi_string_free(title);
    furi_string_free(artist);
    furi_string_free(state);
}

// --- CLI: `spotify_art <width> <height> <hex-bytes>` (1bpp XBM, row bytes = ceil(width/8)) ---
static void spotify_remote_cli_art(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    SpotifyRemote* app = context;
    int w = 0, h = 0;

    if(!args_read_int_and_trim(args, &w) || !args_read_int_and_trim(args, &h)) {
        printf("Usage: spotify_art <width> <height> <hex-bytes>\r\n");
        return;
    }
    if(w <= 0 || h <= 0 || w > SPOTIFY_ART_MAX_DIM || h > SPOTIFY_ART_MAX_DIM) {
        printf("Error: width/height must be 1-%d\r\n", SPOTIFY_ART_MAX_DIM);
        return;
    }

    size_t row_bytes = ((size_t)w + 7) / 8;
    size_t total_bytes = row_bytes * (size_t)h;
    uint8_t* buf = malloc(total_bytes);

    if(!args_read_hex_bytes(args, buf, total_bytes)) {
        printf("Error: expected %zu hex bytes (%d x %d, 1bpp)\r\n", total_bytes, w, h);
        free(buf);
        return;
    }

    with_view_model(
        app->view,
        SpotifyRemoteModel * model,
        {
            free(model->art);
            model->art = buf;
            model->art_w = (size_t)w;
            model->art_h = (size_t)h;
        },
        true);

    printf("OK\r\n");
}

// --- CLI: `spotify_clear` ---
static void spotify_remote_cli_clear(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    UNUSED(args);
    SpotifyRemote* app = context;

    with_view_model(
        app->view,
        SpotifyRemoteModel * model,
        {
            furi_string_reset(model->title);
            furi_string_reset(model->artist);
            model->playing = false;
            model->has_track = false;
            free(model->art);
            model->art = NULL;
            model->art_w = 0;
            model->art_h = 0;
        },
        true);

    printf("OK\r\n");
}

static SpotifyRemote* spotify_remote_alloc(void) {
    SpotifyRemote* app = malloc(sizeof(SpotifyRemote));

    app->gui = furi_record_open(RECORD_GUI);
    app->bt = furi_record_open(RECORD_BT);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->cli_registry = furi_record_open(RECORD_CLI);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->view = view_alloc();
    view_set_context(app->view, app);
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(SpotifyRemoteModel));
    view_set_draw_callback(app->view, spotify_remote_draw_callback);
    view_set_input_callback(app->view, spotify_remote_input_callback);

    with_view_model(
        app->view,
        SpotifyRemoteModel * model,
        {
            model->title = furi_string_alloc();
            model->artist = furi_string_alloc();
            model->art = NULL;
        },
        false);

    view_dispatcher_add_view(app->view_dispatcher, SpotifyRemoteViewMain, app->view);
    view_dispatcher_switch_to_view(app->view_dispatcher, SpotifyRemoteViewMain);

    app->scroll_timer =
        furi_timer_alloc(spotify_remote_scroll_timer_callback, FuriTimerTypePeriodic, app);

    return app;
}

static void spotify_remote_free(SpotifyRemote* app) {
    notification_internal_message(app->notifications, &sequence_reset_blue);

    furi_timer_free(app->scroll_timer);

    with_view_model(
        app->view,
        SpotifyRemoteModel * model,
        {
            furi_string_free(model->title);
            furi_string_free(model->artist);
            free(model->art);
        },
        false);

    view_dispatcher_remove_view(app->view_dispatcher, SpotifyRemoteViewMain);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_CLI);
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

    cli_registry_add_command(
        app->cli_registry, "spotify_np", CliCommandFlagParallelSafe, spotify_remote_cli_np, app);
    cli_registry_add_command(
        app->cli_registry,
        "spotify_art",
        CliCommandFlagParallelSafe,
        spotify_remote_cli_art,
        app);
    cli_registry_add_command(
        app->cli_registry,
        "spotify_clear",
        CliCommandFlagParallelSafe,
        spotify_remote_cli_clear,
        app);

    furi_timer_start(app->scroll_timer, furi_ms_to_ticks(SPOTIFY_SCROLL_PERIOD_MS));

    view_dispatcher_run(app->view_dispatcher);

    furi_timer_stop(app->scroll_timer);

    cli_registry_delete_command(app->cli_registry, "spotify_np");
    cli_registry_delete_command(app->cli_registry, "spotify_art");
    cli_registry_delete_command(app->cli_registry, "spotify_clear");

    bt_set_status_changed_callback(app->bt, NULL, NULL);
    bt_disconnect(app->bt);
    furi_delay_ms(200);

    bt_keys_storage_set_default_path(app->bt);
    furi_check(bt_profile_restore_default(app->bt));

    spotify_remote_free(app);
    return 0;
}
