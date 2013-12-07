// Boldwatch, for Pebble
// by Tom Gidden <tom@gidden.net>
//
// Written: May 4, 2013
// Updated to Pebble OS 2: December 2013
//
// This code is hacked together from a number of sources, most notably the
// Pebble SDK examples.

#include "pebble.h"
#include "pebble_fonts.h"

#define NO 0
#define YES 1

// Boolean preferences:
enum Settings {
    SETTING_SECHAND = 1,
    SETTING_SHOWDATE = 3,
    SETTING_INVERT = 5,
    SETTING_LEDS = 6
};

uint8_t sechand = NO;
uint8_t showdate = YES;
uint8_t invert = NO;
uint8_t leds = NO;

uint8_t update_on_next_tick = YES;
uint8_t update_leds_on_next_tick = YES;

#define BLUETOOTH_CONNECTED 1
#define BLUETOOTH_DISCONNECTED 0
#define BLUETOOTH_UNKNOWN 255

#define BATTERY_CHARGING 253
#define BATTERY_CHARGED 254
#define BATTERY_UNKNOWN 255

uint8_t bluetooth_state = BLUETOOTH_UNKNOWN;
uint8_t battery_state = BATTERY_UNKNOWN;

// Syncing of configuration settings with PebbleJS
static AppSync app;
static uint8_t buffer[256];


// The main window itself
static Window *window;
static Layer *window_layer;

// watchface is the background image
static GBitmap *watchface_image;
static BitmapLayer *watchface_layer;

// The frame of the watchface is unsurprisingly pivotal (no pun intended),
// and is used in all of the graphics update routines, so it's best to
// just store it rather than recalculating it each time. In addition, the
// middle point of the frame is also used, so we store it too: relative to
// the frame.
static GPoint watchface_center;
static GRect watchface_frame;

// centerdot is to cover up the where the hands cross the center, which is
// unsightly. Previous versions used a rounded rectangle or a circle, but
// I think this might be more efficient...
static GBitmap *centerdot_image;
static BitmapLayer *centerdot_layer;
static GRect centerdot_frame;

// Hour and minute hands. Was previously done with rotating bitmaps, but
// the current 2.0 BETA SDK is incomplete here.
static GPath *minute_path;
static GPath *hour_path;
static Layer *hmhands_layer;

// The second hand, however, can be drawn with a simple line, as in this
// design it's not meant to be thick.
static Layer *sechand_layer;

// Text layers and fonts for digital time and date. If these are not
// enabled, they just don't get loaded or used, so no big deal.
static TextLayer *date_layer;
static GFont date_font;
#define FONT_NUMBERS FONT_KEY_BITHAM_30_BLACK

// Text buffers for the digital readout.
static char date_text[] = "XX";
static GPoint date_center;

const int date_radius = 40;
const int date_width = 42;
const int date_height = 42;

// LEDs (indicators)
#define LEDS_PAD_X 2
#define LEDS_PAD_Y 2
#define LEDS_BATTERY_LENGTH 15
#define LEDS_BATTERY_HEIGHT 8
#define LEDS_WIDTH 144-LEDS_PAD_X*2
#define LEDS_HEIGHT 13
static Layer *leds_layer;
static GRect leds_frame = {.origin={.x=LEDS_PAD_X,.y=LEDS_PAD_Y},
                             .size={.w=LEDS_WIDTH,.h=LEDS_HEIGHT}};

static GPath *bluetooth_path;


static const GPathInfo BLUETOOTH_ICON = {
    6, (GPoint []){
        {-1, 3-1},
        {6, 9},
        {3, 12},
        {3, 0},
        {6, 3},
        {-1, 9+1}
    }
};

static GPath *charge_path;
static GPoint charge_origin = {.x=LEDS_WIDTH-LEDS_BATTERY_LENGTH+1, .y=0};

static GRect battery_rect = {.origin={.x=LEDS_WIDTH-LEDS_BATTERY_LENGTH-2,.y=2},
                             .size={.w=LEDS_BATTERY_LENGTH,.h=LEDS_BATTERY_HEIGHT}};

static const GPathInfo CHARGE_ICON = {
    9, (GPoint []){
        {7, 0},
        {6, 5},
        {8, 6},
        {2, 12},
        {2, 11},
        {3, 6},
        {1, 6},
        {1, 5},
        {6, 0}
    }
};



// Inverter layer, if black-on-white has been requested
InverterLayer *invert_layer;


// Store the time from the event, so we can use it in later
// functions. Since we're not building a world-clock, or handling General
// Relativity, I think it's safe to treat time as a global variable...
static struct tm *pebble_time;

// Simple rectangle paths for the hands
static const GPathInfo HOUR_HAND = {
    4, (GPoint []){
        {-4, -40},
        {4, -40},
        {4, -8},
        {-4, -8}
    }
};
static const GPathInfo MINUTE_HAND = {
    4, (GPoint []){
        {-4, -54},
        {4, -54},
        {4, -8},
        {-4, -8}
    }
};

static void load_image_to_bitmap_layer(GBitmap **image, BitmapLayer **layer, GRect *frame, const int resource_id)
// Loads a resource into an image, layer and frame.
{
    // If the vars are already set, then we'll need to delete them at the
    // end of this routine to prevent a leak.
    GBitmap *old_image = *image;
    BitmapLayer *old_layer = *layer;

    // Load the image
    *image = gbitmap_create_with_resource(resource_id);

    // Get the size and store it.
    frame->size = (*image)->bounds.size;

    // Create a new bitmap layer
    *layer = bitmap_layer_create(*frame);

    // And load the image into the layer.
    bitmap_layer_set_bitmap(*layer, *image);

    // If it previously existed, we can kill the old data now.
    if(old_image) gbitmap_destroy(old_image);
    if(old_layer) bitmap_layer_destroy(old_layer);
}

static void hmhands_update_proc(Layer *layer, GContext *ctx)
// On Pebble OS 1, the polygon drawing routine was fairly flaky, so the
// hands were done with bitmap rotations.  On Pebble OS 2 BETA, bitmap
// rotations seem incomplete (or at least, undocumented), so it's back to
// polygons. Fortunately, they work a lot better now.
{
    if(!pebble_time) return;

    // We're not going to bother stroking the polygon: just fill
    graphics_context_set_fill_color(ctx, GColorWhite);

    // Rotate and draw minute hand
    gpath_rotate_to(minute_path, TRIG_MAX_ANGLE * pebble_time->tm_min / 60);
    gpath_draw_filled(ctx, minute_path);

    // Rotate and draw hour hand
    gpath_rotate_to(hour_path, (TRIG_MAX_ANGLE * (((pebble_time->tm_hour % 12) * 6) + (pebble_time->tm_min / 10))) / (12 * 6));
    gpath_draw_filled(ctx, hour_path);
}

static void sechand_update_proc(Layer *layer, GContext *ctx)
// The second-hand is drawn as a simple line.
{
    if(!pebble_time) return;

    // The second-hand has a "counterbalance", so we actually start
    // the other side of the center.
    static GPoint endpoint1, endpoint2;

    graphics_context_set_stroke_color(ctx, GColorWhite);

    // We could probably precalc these calculations, but screw it.  The
    // length of the second-hand is the same as the radius of the
    // watchface, which is the same as watchface_center.x (as that's half
    // the width a.k.a. diameter)
    int32_t a = TRIG_MAX_ANGLE * pebble_time->tm_sec / 60;
    int16_t dy = (int16_t)(-cos_lookup(a) * (int32_t)watchface_center.x / TRIG_MAX_RATIO);
    int16_t dx = (int16_t)(sin_lookup(a) * (int32_t)watchface_center.x / TRIG_MAX_RATIO);

    // Draw a line _across_ the center to the edge of the face.
    endpoint1.x = watchface_center.x + dx;
    endpoint1.y = watchface_center.y + dy;
    endpoint2.x = watchface_center.x - dx/3;
    endpoint2.y = watchface_center.y - dy/3;

    graphics_draw_line(ctx, endpoint1, endpoint2);
}

static inline void battery_changed_translate(BatteryChargeState charge_state)
{
    if(charge_state.is_charging)
        battery_state = BATTERY_CHARGING;
    else if(charge_state.is_plugged)
        battery_state = BATTERY_CHARGED;
    else
        battery_state = charge_state.charge_percent;
}

static void leds_update_proc(Layer *layer, GContext *ctx)
// LEDS for Bluetooth and Battery
{
    update_leds_on_next_tick = NO;

    if(!leds) return;

    graphics_context_set_stroke_color(ctx, GColorWhite);

    if(bluetooth_state == BLUETOOTH_UNKNOWN)
        bluetooth_state = bluetooth_connection_service_peek();

    if(battery_state == BATTERY_UNKNOWN)
        battery_changed_translate(battery_state_service_peek());

    if(bluetooth_state == BLUETOOTH_CONNECTED)
        graphics_context_set_stroke_color(ctx, GColorWhite);
    else
        graphics_context_set_stroke_color(ctx, GColorBlack);
    gpath_draw_outline(ctx, bluetooth_path);


    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_rect(ctx, battery_rect);

    GRect fill_rect = battery_rect;
    fill_rect.origin.x+=2;
    fill_rect.origin.y+=2;
    fill_rect.size.h -= 4;
    fill_rect.size.w -= 4;

    switch (battery_state) {
    case BATTERY_CHARGED:
    case BATTERY_CHARGING:
        graphics_context_set_fill_color(ctx, GColorWhite);
        break;

    case BATTERY_UNKNOWN:
        graphics_context_set_fill_color(ctx, GColorBlack);
        break;

    default:
        graphics_context_set_fill_color(ctx, GColorWhite);
        fill_rect.size.w = ((int)(fill_rect.size.w) * (int)battery_state)/100;
    }

    graphics_fill_rect(ctx, fill_rect, 0, 0);

    graphics_draw_line(ctx,
                       GPoint(battery_rect.origin.x+battery_rect.size.w,
                              battery_rect.origin.y+2),
                       GPoint(battery_rect.origin.x+battery_rect.size.w,
                              battery_rect.origin.y+battery_rect.size.h-3));

    if(battery_state == BATTERY_CHARGING ||
       battery_state == BATTERY_CHARGED) {
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_context_set_fill_color(ctx, GColorWhite);
        gpath_draw_filled(ctx, charge_path);
        gpath_draw_outline(ctx, charge_path);
    }
}

static void clear_ui()
// Pebble has little to no memory management, so we have to clear up. This might
// be a bit excessive... in particular, is it necessary to remove a layer before
// destroying it?
//
// Regardless, this shouldn't harm and gives setup_ui() a clean run at it.
{
    if(hmhands_layer) {
        layer_remove_from_parent(hmhands_layer);
        layer_destroy(hmhands_layer);
        hmhands_layer = NULL;
    }

    if(hour_path) {
        gpath_destroy(hour_path);
        hour_path = NULL;
    }

    if(sechand_layer) {
        layer_remove_from_parent(sechand_layer);
        layer_destroy(sechand_layer);
        sechand_layer = NULL;
    }

    if(minute_path) {
        gpath_destroy(minute_path);
        minute_path = NULL;
    }

    if(leds_layer) {
        layer_remove_from_parent(leds_layer);
        layer_destroy(leds_layer);
        leds_layer = NULL;
    }

    if(bluetooth_path) {
        gpath_destroy(bluetooth_path);
        bluetooth_path = NULL;
    }

    if(charge_path) {
        gpath_destroy(charge_path);
        charge_path = NULL;
    }

    if(centerdot_layer) {
        layer_remove_from_parent(bitmap_layer_get_layer(centerdot_layer));
        bitmap_layer_destroy(centerdot_layer);
        centerdot_layer = NULL;
    }

    if(centerdot_image) {
        gbitmap_destroy(centerdot_image);
        centerdot_image = NULL;
    }

    if(watchface_layer) {
        layer_remove_from_parent(bitmap_layer_get_layer(watchface_layer));
        bitmap_layer_destroy(watchface_layer);
        watchface_layer = NULL;
    }

    if(watchface_image) {
        gbitmap_destroy(watchface_image);
        watchface_image = NULL;
    }

    if(invert_layer) {
        layer_remove_from_parent(inverter_layer_get_layer(invert_layer));
        inverter_layer_destroy(invert_layer);
        invert_layer = NULL;
    }

    if(date_layer) {
        layer_remove_from_parent(text_layer_get_layer(date_layer));
        text_layer_destroy(date_layer);
        date_layer = NULL;
    }
}

static void setup_ui()
// Load the images, fonts and paths for the UI elements, either big or
// small.
{
    // If this routine has been run before, we clear the old paths. This
    // will allow dynamic config of watch size.
    clear_ui();

    // Not unexpectedly, most coordinates for the face, hands, etc. are
    // relative to the watchface size and position.
    watchface_frame = (GRect) {
        .origin = { .x = 0, .y = 12 }
    };

    // Load the watchface first
    load_image_to_bitmap_layer(&watchface_image, &watchface_layer, &watchface_frame, RESOURCE_ID_IMAGE_WATCHFACE);

    // The center of the watchface (relative to the origin of the frame)
    // is used in laying out the hands.
    watchface_center = GPoint(watchface_frame.size.w/2, watchface_frame.size.h/2);

    // Add the background layer to the window.
    layer_add_child(window_layer, bitmap_layer_get_layer(watchface_layer));

    // Add the LEDs layer
    if(leds) {
        bluetooth_path = gpath_create(&BLUETOOTH_ICON);
        charge_path = gpath_create(&CHARGE_ICON);
        gpath_move_to(charge_path, charge_origin);

        leds_layer = layer_create(leds_frame);

        layer_set_update_proc(leds_layer, leds_update_proc);
        layer_add_child(window_layer, leds_layer);
    }

    // Load and position the hour hand
    hour_path = gpath_create(&HOUR_HAND);
    gpath_move_to(hour_path, watchface_center);

    // Load and position the minute hand
    minute_path = gpath_create(&MINUTE_HAND);
    gpath_move_to(minute_path, watchface_center);

    // Hands: To make updates easier (as hours and minutes are always
    // updated at the same time due to slew on the hour hand through the
    // hour), the hour- and minute-hand are separate image sublayers of a
    // generic layer for both hands. This means they share the same update
    // routine.
    hmhands_layer = layer_create(watchface_frame);
    layer_set_update_proc(hmhands_layer, hmhands_update_proc);

    // Add the combined hands layer to the window
    layer_add_child(window_layer, hmhands_layer);

    // Second-hand, if there is one:
    if(sechand) {
        sechand_layer = layer_create(watchface_frame);
        layer_set_update_proc(sechand_layer, sechand_update_proc);
        layer_add_child(window_layer, sechand_layer);
    }

    // Load bitmap for center dot background
    load_image_to_bitmap_layer(&centerdot_image, &centerdot_layer, &centerdot_frame, RESOURCE_ID_IMAGE_CENTERDOT);

    // Center-align it on the watchface center
    centerdot_frame.origin.x = watchface_frame.origin.x + watchface_center.x - centerdot_frame.size.w / 2;
    centerdot_frame.origin.y = watchface_frame.origin.y + watchface_center.y - centerdot_frame.size.h / 2;
    layer_set_frame(bitmap_layer_get_layer(centerdot_layer), centerdot_frame);

    // Add it to the window.
    layer_add_child(window_layer, bitmap_layer_get_layer(centerdot_layer));

    // Date display
    if(showdate) {
        date_center = GPoint(watchface_frame.origin.x + watchface_center.x,
                             watchface_frame.origin.y + watchface_center.y);

        date_layer = text_layer_create(GRect(0, 0, date_width, date_height));
        text_layer_set_text_color(date_layer, GColorWhite);
        text_layer_set_background_color(date_layer, GColorClear);
        text_layer_set_text_alignment(date_layer, GTextAlignmentCenter);

        date_font = fonts_get_system_font(FONT_NUMBERS);
        text_layer_set_font(date_layer, date_font);

        layer_add_child(window_layer, text_layer_get_layer(date_layer));
    }

    // Add an inverter if black-on-white is desired (WHY?!)
    if(invert) {
        invert_layer = inverter_layer_create(layer_get_frame(window_layer));
        layer_add_child(window_layer, inverter_layer_get_layer(invert_layer));
    }
}

static void tick(struct tm *t, TimeUnits units_changed)
{
    // If we were passed an event, then we should already have the current
    // time. If not, then we're probably being called from handle_init, so
    // we need to get it.
    if(t)
        pebble_time = t;
    else {
        time_t now = time(NULL);
        pebble_time = localtime(&now);
    }

    // If the settings have been changed, or they haven't been initialised
    // yet, then we run the UI setup routine before any drawing takes
    // place.
    if(update_on_next_tick) {
        setup_ui();
    }

    // If we're displaying the date, update the date string
    // every hour, and also on initialisation.
    if(showdate) {

        // Store the current minute for the next tick
        static int min = -1;

        // If the minute has changed since the last tick or showdate has
        // been changed. We indicate this by showdate being 2 rather than
        // YES / 1.  It'll get reset to 1 at the end.
        if(min != pebble_time->tm_min || showdate==2) {
            min = pebble_time->tm_min;

            // Work out the position of the hour hand, taking into account
            // minute slew
            float h = (pebble_time->tm_hour % 12) + ((float)min/60);

            // Store the angle for the next update: we only bother moving
            // the frame if it actually moved.
            static int ang = -1;

            // We need to find the average of the two hands' positions and
            // then find the furthest place from them.  There might be an
            // easier way of doing this, but this works...

            // Find the heading of each hand, 0-360
            int ang1 = (int)(h*360)/12;
            int ang2 = min * 6;

            // First, check the difference between them...
            int _ang = ang1 - ang2;

            // If the angle between them is reflex (ie. > 180 degrees) ,
            // then we just use the average of the two angles. Otherwise,
            // we use the opposite of the average.

            if(_ang > -180 && _ang < 180)
                // Use the opposite of the average:
                _ang = (360 + 180 + (ang1+ang2) / 2) % 360;
            else
                // Use the average:
                _ang = (360 + (ang1+ang2) / 2) % 360;

            // If the angle has changed (or showdate is dirty):
            if(ang != _ang || showdate==2) {
                ang = _ang;

                if(date_layer) {
                    // Update the location of the date box.
                    GRect r = layer_get_frame(text_layer_get_layer(date_layer));

                    r.origin.x = date_center.x + (date_radius * sin_lookup(ang * TRIG_MAX_ANGLE / 360)) / TRIG_MAX_RATIO - date_width/2;
                    r.origin.y = date_center.y - (date_radius * cos_lookup(ang * TRIG_MAX_ANGLE / 360)) / TRIG_MAX_RATIO - date_height/2;

                    layer_set_frame(text_layer_get_layer(date_layer), r);
                }
            }

            // Store the day-of-the-month for the next tick
            static int mday = -1;

            if(date_layer && (mday != pebble_time->tm_mday || showdate==2)) {
                mday = pebble_time->tm_mday;

                // We could use a simple integer to string conversion, but
                // I don't know if that's available in PebbleOS, and can't
                // be bothered to look it up. Screw it: it only happens
                // once a day, and on init.
                strftime(date_text, sizeof(date_text), "%e", pebble_time);

                // We only want a single digit.
                if(mday < 10)
                    text_layer_set_text(date_layer, date_text+1);
                else
                    text_layer_set_text(date_layer, date_text);
            }

            // Reset the dirty thing
            if(showdate==2)
                showdate = YES;
        }
    }

    // If we're displaying a second-hand, then mark the second-hand layer
    // as dirty for redrawing.
    if(sechand) {
        if(sechand_layer) layer_mark_dirty(sechand_layer);
    }

    // Update the hour/minute hands, if:
    //
    //   a) !t, in which case, we're being called from handle_init, so
    //      need to do the initial update;
    //
    //   b) Current seconds = 0 (ie. we're on the minute); or
    //
    //   c) Display of seconds is not enabled, which implies this event
    //      handler is running every minute anyway.
    if (update_on_next_tick || !sechand || (pebble_time->tm_sec == 0)) {
        if(hmhands_layer) layer_mark_dirty(hmhands_layer);
    }

    // LEDs need an update, so we'll do that now.
    if(update_leds_on_next_tick) {
        if(leds_layer) layer_mark_dirty(leds_layer);
    }

    // Unless we get an update...
    update_on_next_tick = NO;
}

static void battery_changed_callback(BatteryChargeState charge_state)
{
    battery_changed_translate(charge_state);

    APP_LOG(APP_LOG_LEVEL_DEBUG, "battery_changed_callback: %d", battery_state);

    if(!leds) return;

    update_leds_on_next_tick = YES;
    tick(NULL, sechand ? SECOND_UNIT : MINUTE_UNIT);
}

static void bluetooth_changed_callback(bool connected)
{
    bluetooth_state = connected ? BLUETOOTH_CONNECTED : BLUETOOTH_DISCONNECTED;

    APP_LOG(APP_LOG_LEVEL_DEBUG, "bluetooth_changed_callback: %d", bluetooth_state);

    if(!leds) return;

    update_leds_on_next_tick = YES;
    tick(NULL, sechand ? SECOND_UNIT : MINUTE_UNIT);
}

static void tuple_changed_callback(const uint32_t key, const Tuple* tuple_new, const Tuple* tuple_old, void* context)
// Configuration data from PebbleJS has been received.
{
    uint8_t value = tuple_new->value->uint8;

    switch (key) {
    case SETTING_SECHAND:
        sechand = value ? YES : NO;
        update_on_next_tick = YES;
        break;

    case SETTING_SHOWDATE:
        showdate = value ? YES : NO;
        if(showdate) showdate=2; // Dirty the date so it gets refreshed on next tick
        update_on_next_tick = YES;
        break;

    case SETTING_INVERT:
        invert = value ? YES : NO;
        update_on_next_tick = YES;
        break;

    case SETTING_LEDS:
        leds = value ? YES : NO;
        battery_state = BATTERY_UNKNOWN;
        bluetooth_state = BLUETOOTH_UNKNOWN;
        update_on_next_tick = YES;
        update_leds_on_next_tick = YES;
        break;
    }

    // If the settings were updated, then we need to schedule the next
    // tick.  Since it might change from every-minute to every-second (or
    // vice versa), we need to do a full resubscribe.
    if(update_on_next_tick) {

        // Write the value to persistent storage
        persist_write_int(key, value);

        // (Re-)schedule the timer
        tick_timer_service_unsubscribe();
        tick_timer_service_subscribe(sechand ? SECOND_UNIT : MINUTE_UNIT, tick);
    }
}

static void app_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void* context)
// Error from... um... the thing.
{
    APP_LOG(APP_LOG_LEVEL_DEBUG, "app error %d", app_message_error);
}

static void window_load(Window *_window)
// Window load event.
{
    // Initialise the window variables
    window = _window;
    window_layer = window_get_root_layer(window);

    // Monitor bluetooth and battery status, even if we're not displaying.
    // (Otherwise it gets tricky)
    bluetooth_connection_service_subscribe(&bluetooth_changed_callback);
    battery_state_service_subscribe(&battery_changed_callback);

    // And schedule a reinitialise on the next tick
    update_on_next_tick = YES;

    // Schedule the timer
    tick_timer_service_subscribe(sechand ? SECOND_UNIT : MINUTE_UNIT, tick);

    // Call the tick handler once to initialise the face.  This should
    // schedule the timer.
    tick(NULL, sechand ? SECOND_UNIT : MINUTE_UNIT);
}

static void window_unload(Window *window)
// Window unload event
{
    // Stop the tick handler
    tick_timer_service_unsubscribe();

    // Quit monitoring battery and bluetooth status.
    bluetooth_connection_service_unsubscribe();
    battery_state_service_unsubscribe();

    // And deallocate everything
    clear_ui();
}

static void send_cfg_to_js(void)
// Send settings to PebbleJS so it can store them
{
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

  if (iter == NULL) return;

  dict_write_uint8(iter, SETTING_SECHAND, sechand);
  dict_write_uint8(iter, SETTING_SHOWDATE, showdate);
  dict_write_uint8(iter, SETTING_INVERT, invert);
  dict_write_uint8(iter, SETTING_LEDS, leds);

  dict_write_end(iter);

  app_message_outbox_send();
}

static void init(void)
// Initialise the app
{
    // Initialise settings from persistent storage
    if(persist_exists(SETTING_SECHAND))
        sechand = persist_read_int(SETTING_SECHAND);

    if(persist_exists(SETTING_SHOWDATE))
        showdate = persist_read_int(SETTING_SHOWDATE);

    if(persist_exists(SETTING_INVERT))
        invert = persist_read_int(SETTING_INVERT);

    if(persist_exists(SETTING_LEDS))
        leds = persist_read_int(SETTING_LEDS);

    // Create and initialise the main window
    window = window_create();
    window_set_background_color(window, GColorBlack);
    window_set_window_handlers(window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload,
    });

    if (window == NULL) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "OOM: couldn't allocate window");
        return;
    }

    app_message_open(160, 160);

    Tuplet tuples[] = {
        TupletInteger(SETTING_SECHAND, sechand),
        TupletInteger(SETTING_SHOWDATE, showdate),
        TupletInteger(SETTING_INVERT, invert),
        TupletInteger(SETTING_LEDS, leds)
    };

    app_sync_init(&app,
                  buffer, sizeof(buffer),
                  tuples, ARRAY_LENGTH(tuples),
                  tuple_changed_callback,
                  app_error_callback,
                  NULL);

    // And send the persistent config to PebbleJS
    send_cfg_to_js();

    // Load the window onto the UI stack
    window_stack_push(window, true);
}

static void deinit()
{
    // Shut down PebbleJS
    app_sync_deinit(&app);

    // And close the main window
    window_destroy(window);
}

int main(void)
{
    init();
    app_event_loop();
    deinit();
}
