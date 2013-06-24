// Boldwatch, for Pebble
// by Tom Gidden <tom@gidden.net>
// May 7, 2013

#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"

#include "config.h"

#define NO 0
#define YES 1

PBL_APP_INFO(MY_UUID, APP_NAME, "Tom Gidden",
             1, 3, /* App version */
             RESOURCE_ID_IMAGE_MENU_ICON, APP_INFO_WATCH_FACE);

// Boolean preferences:
const int enable_seconds = BOLDWATCH_SECONDS;
const int enable_date = BOLDWATCH_DATE;
const int enable_invert = BOLDWATCH_INVERT; /* Even though it looks stupid */

const int enable_alarms = BOLDWATCH_ALARMS;

const VibePattern alarm_pattern = {
    .durations = (uint32_t []){200, 300, 200, 300, 200, 300, 200, 300, 200},
    .num_segments = 9
};
const uint8_t alarm_hours[] = {
    0,0,0,0,0,0,0,0, // midnight - 7am
    1,0,0,0,0,0,0,0, // 8am - 3pm
    0,1,0,0,0,0,1,0  // 4pm - 11pm
};

Window window;

// watchface is the background image
BmpContainer watchface_container;

// The frame of the watchface is unsurprisingly pivotal (no pun intended),
// and is used in all of the graphics update routines, so it's best to
// just store it rather than recalculating it each time. In addition, the
// middle point of the frame is also used, so we store it too: relative to
// the frame.
GPoint watchface_center;
GRect watchface_frame;

// centerdot is to cover up the where the hands cross the center, which is
// unsightly. Previous versions used a rounded rectangle or a circle, but
// I think this might be more efficient...
BmpContainer centerdot_container;

// At time of writing, most recent firmware (1.10) has bugs relating to
// filling of narrow polygons. As a result, using GPaths to draw the hands
// of the watch is not as good as rotating bitmaps. So, the hour and
// minute hands are just white rectangle bitmaps that get rotated into
// place.
RotBmpContainer hourhand_container;
RotBmpContainer minutehand_container;
Layer hmhands_layer;

// The second hand, however, can be drawn with a simple line, as in this
// design it's not meant to be thick.
Layer sechand_layer;

// Text layers and fonts for date display. If this is not enabled, they
// just don't get loaded or used, so no big deal.
TextLayer date_layer;
GFont date_font;
#define FONT_NUMBERS FONT_KEY_GOTHAM_30_BLACK

// Store the time from the event, so we can use it in later
// functions. Since we're not building a world-clock, or handling General
// Relativity, I think it's safe to treat time as a global variable...
PblTm pebble_time;

// Text buffers for the digital readout.
char date_text[] = "00";
GPoint date_center;
const int date_radius = 40;
const int date_width = 42;
const int date_height = 42;

// Inverter layer, if black-on-white has been requested
InverterLayer inverter_layer;

////////////////////////////////////////////////////////////////////////////
// This comes direct from
// pebble/pebble-sdk-examples/watches/brains/src/brains.c, as bitmap
// rotation is inexplicable at the moment.

/* -------------- TODO: Remove this and use Public API ! ------------------- */

// from src/core/util/misc.h

#define MAX(a,b) (((a)>(b))?(a):(b))

// From src/fw/ui/rotate_bitmap_layer.c

//! newton's method for floor(sqrt(x)) -> should always converge
static int32_t integer_sqrt(int32_t x) {
  if (x < 0) {
    ////    PBL_LOG(LOG_LEVEL_ERROR, "Looking for sqrt of negative number");
    return 0;
  }

  int32_t last_res = 0;
  int32_t res = (x + 1)/2;
  while (last_res != res) {
    last_res = res;
    res = (last_res + x / last_res) / 2;
  }
  return res;
}

void rot_bitmap_set_src_ic(RotBitmapLayer *image, GPoint ic) {
  image->src_ic = ic;

  // adjust the frame so the whole image will still be visible
  const int32_t horiz = MAX(ic.x, abs(image->bitmap->bounds.size.w - ic.x));
  const int32_t vert = MAX(ic.y, abs(image->bitmap->bounds.size.h - ic.y));

  GRect r = layer_get_frame(&image->layer);
  //// const int32_t new_dist = integer_sqrt(horiz*horiz + vert*vert) * 2;
  const int32_t new_dist = (integer_sqrt(horiz*horiz + vert*vert) * 2) + 1; //// Fudge to deal with non-even dimensions--to ensure right-most and bottom-most edges aren't cut off.

  r.size.w = new_dist;
  r.size.h = new_dist;
  layer_set_frame(&image->layer, r);

  r.origin = GPoint(0, 0);
  ////layer_set_bounds(&image->layer, r);
  image->layer.bounds = r;

  image->dest_ic = GPoint(new_dist / 2, new_dist / 2);

  layer_mark_dirty(&(image->layer));
}

////////////////////////////////////////////////////////////////////////////

void set_hand(RotBmpContainer *container, int ang)
{
    if(ang == 0)
        // As of Pebble OS 1.11, rotation=0 seems to disappear the image,
        // but after checking, path drawing is still broken, so let's hack
        // it.
        container->layer.rotation = TRIG_MAX_ANGLE;
    else
        container->layer.rotation = TRIG_MAX_ANGLE * ang / 360;
}

void hmhands_update_proc(Layer *me, GContext *ctx)
{
    (void)me;
    set_hand(&hourhand_container, (pebble_time.tm_hour % 12)*30 + pebble_time.tm_min/2);
    set_hand(&minutehand_container, pebble_time.tm_min*6);
}

void sechand_update_proc(Layer *me, GContext *ctx)
// The second-hand is drawn as a simple line, rather than using image
// rotation.
{
    // The second-hand has a "counterbalance", so we actually start
    // the other side of the center.
    static GPoint endpoint1, endpoint2;

    graphics_context_set_stroke_color(ctx, GColorWhite);

    // We could probably precalc these calculations, but screw it.  The
    // length of the second-hand is the same as the radius of the
    // watchface, which is the same as watchface_center.x (as that's half
    // the width a.k.a. diameter)
    int32_t a = TRIG_MAX_ANGLE * pebble_time.tm_sec / 60;
    int16_t dy = (int16_t)(-cos_lookup(a) * (int32_t)watchface_center.x / TRIG_MAX_RATIO);
    int16_t dx = (int16_t)(sin_lookup(a) * (int32_t)watchface_center.x / TRIG_MAX_RATIO);

    // Draw a line _across_ the center to the edge of the face.
    endpoint1.x = watchface_center.x + dx;
    endpoint1.y = watchface_center.y + dy;
    endpoint2.x = watchface_center.x - dx/3;
    endpoint2.y = watchface_center.y - dy/3;

    graphics_draw_line(ctx, endpoint1, endpoint2);
}

void handle_tick(AppContextRef ctx, PebbleTickEvent *t)
{
    (void)ctx;

    // If we were passed an event, then we should already have the current
    // time. If not, then we're probably being called from handle_init, so
    // we need to get it.
    if(t)
        pebble_time = *t->tick_time;
    else
        get_time(&pebble_time);

    // Update the date string whenever it changes, and on initialization
    if(enable_date) {

        // Store the current minute for the next tick
        static int min = -1;

        // If the minute has changed since the last tick
        if(min != pebble_time.tm_min) {
            min = pebble_time.tm_min;

            // Work out the position of the hour hand, taking into account
            // minute slew
            float h = (pebble_time.tm_hour % 12) + ((float)min/60);

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

            // If the angle has changed:
            if(ang != _ang) {
                ang = _ang;

                // Update the location of the date box.
                GRect r = layer_get_frame(&date_layer.layer);
                r.origin.x = date_center.x + (date_radius * sin_lookup(ang * TRIG_MAX_ANGLE / 360)) / TRIG_MAX_RATIO - date_width/2;
                r.origin.y = date_center.y - (date_radius * cos_lookup(ang * TRIG_MAX_ANGLE / 360)) / TRIG_MAX_RATIO - date_height/2;
                layer_set_frame(&date_layer.layer, r);
            }

            // Store the day-of-the-month for the next tick
            static int mday = -1;

            // If the day-of-the-month has changed, then reformat it.
            if(mday != pebble_time.tm_mday) {
                mday = pebble_time.tm_mday;

                // We could use a simple integer to string conversion, but
                // I don't know if that's available in PebbleOS, and can't
                // be bothered to look it up. Screw it: it only happens
                // once a day, and on init.
                string_format_time(date_text, sizeof(date_text), "%e", &pebble_time);

                // We only want a single digit.
                if(mday < 10)
                    text_layer_set_text(&date_layer, date_text+1);
                else
                    text_layer_set_text(&date_layer, date_text);
            }
        }
    }

    // If we're displaying a second-hand, then mark the second-hand layer
    // as dirty for redrawing.
    if(enable_seconds) {
        layer_mark_dirty(&sechand_layer);
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
    if (!enable_seconds || (pebble_time.tm_sec == 0)) {
        layer_mark_dirty(&hmhands_layer);

        // If on the hour and alarms are enabled, do an alarm.
        if (enable_alarms && alarm_hours[pebble_time.tm_hour] && pebble_time.tm_min == 0 && pebble_time.tm_sec == 0) {
            vibes_enqueue_custom_pattern(alarm_pattern);
        }
    }
}

void handle_init(AppContextRef ctx)
{
    (void)ctx;

    window_init(&window, APP_NAME);
    window_stack_push(&window, true /* Animated */);
    window_set_background_color(&window, GColorBlack);

    resource_init_current_app(&APP_RESOURCES);

    // Load bitmap for watch face background
    bmp_init_container(RESOURCE_ID_IMAGE_WATCHFACE, &watchface_container);

    // Not unexpectedly, most coordinates for the face, hands, etc. are
    // relative to the watchface size and position.
    watchface_frame = layer_get_frame(&watchface_container.layer.layer);
    watchface_frame.origin.x = 0;
    watchface_frame.origin.y = 12;
    layer_set_frame(&watchface_container.layer.layer, watchface_frame);

    // The center of the watchface (relative to the origin of the frame)
    // is used in laying out the hands.
    watchface_center = GPoint(watchface_frame.size.w/2, watchface_frame.size.h/2);

    // Add the background layer to the window.
    layer_add_child(&window.layer, &watchface_container.layer.layer);

    // Date display
    if(enable_date) {
        date_center = GPoint(watchface_frame.origin.x + watchface_center.x,
                             watchface_frame.origin.y + watchface_center.y);

        text_layer_init(&date_layer, GRect(0, 0, date_width, date_height));
        text_layer_set_text_color(&date_layer, GColorWhite);
        text_layer_set_background_color(&date_layer, GColorClear);
        text_layer_set_text_alignment(&date_layer, GTextAlignmentCenter);
        date_font = fonts_get_system_font(FONT_NUMBERS);
        text_layer_set_font(&date_layer, date_font);
        layer_add_child(&window.layer, &date_layer.layer);
    }

    // Hands: To make updates easier (as hours and minutes are always
    // updated at the same time due to slew on the hour hand through the
    // hour), the hour- and minute-hand are separate image sublayers of a
    // generic layer for both hands. This means they share the same update
    // routine.
    layer_init(&hmhands_layer, watchface_frame);
    hmhands_layer.update_proc = &hmhands_update_proc;

    // It would be better to do both hour- and minute-hand with polygons,
    // but polygon-drawing in PebbleOS 1.10 is broken: it can't do narrow
    // polygons worth a damn. Contrary to initial assumptions, it's not
    // down to rotation error. To test this, I generated all positions of
    // the minute hand with known-good coordinates without resorting to
    // Pebble trig or rotation, and it couldn't draw them well.
    //
    // So, instead, we use Pebble's crazy-ass rotbmp system instead.
    rotbmp_init_container(RESOURCE_ID_IMAGE_HOURHAND, &hourhand_container);
    layer_add_child(&hmhands_layer, &hourhand_container.layer.layer);

    // The bounding boxes of RotBmpLayers are a mystery to me, so I'm just
    // going to copy what everyone else seems to do:
    rot_bitmap_set_src_ic(&hourhand_container.layer, GPoint(2, 40));
    hourhand_container.layer.layer.frame.origin.x = watchface_center.x - hourhand_container.layer.layer.frame.size.w/2;
    hourhand_container.layer.layer.frame.origin.y = watchface_center.y - hourhand_container.layer.layer.frame.size.h/2;

    // Same as with minute hands...
    rotbmp_init_container(RESOURCE_ID_IMAGE_MINUTEHAND, &minutehand_container);
    layer_add_child(&hmhands_layer, &minutehand_container.layer.layer);

    rot_bitmap_set_src_ic(&minutehand_container.layer, GPoint(2, 54));
    minutehand_container.layer.layer.frame.origin.x = watchface_center.x - minutehand_container.layer.layer.frame.size.w/2;
    minutehand_container.layer.layer.frame.origin.y = watchface_center.y - minutehand_container.layer.layer.frame.size.h/2;

    // Add the combined hands layer to the window
    layer_add_child(&window.layer, &hmhands_layer);

    // Second-hand, if there is one:
    if(enable_seconds) {
        layer_init(&sechand_layer, watchface_frame);
        sechand_layer.update_proc = &sechand_update_proc;
        layer_add_child(&window.layer, &sechand_layer);
    }

    // Load bitmap for center dot background
    bmp_init_container(RESOURCE_ID_IMAGE_CENTERDOT, &centerdot_container);

    // Center-align it on the watchface center
    GRect centerdot_frame = layer_get_frame(&centerdot_container.layer.layer);
    centerdot_frame.origin.x = watchface_frame.origin.x + watchface_center.x - centerdot_frame.size.w / 2;
    centerdot_frame.origin.y = watchface_frame.origin.y + watchface_center.y - centerdot_frame.size.h / 2;
    layer_set_frame(&centerdot_container.layer.layer, centerdot_frame);

    // Add it to the window.
    layer_add_child(&window.layer, &centerdot_container.layer.layer);

    // Add an inverter if black-on-white is desired (WHY?!)
    if(enable_invert) {
        inverter_layer_init(&inverter_layer, window.layer.frame);
        layer_add_child(&window.layer, &inverter_layer.layer);
    }

    // Finally, do the rest of the initialisation by calling the tick
    // event handler directly (for this first run):
    handle_tick(NULL, NULL);
}

void handle_deinit(AppContextRef ctx)
// Pebble has little to no memory management, so we have to clear up. I'm
// not convinced this is all of it, mind you. It'd be nice if we could
// work out where all the leaks are somehow.
{
    (void)ctx;

    bmp_deinit_container(&watchface_container);
    rotbmp_deinit_container(&hourhand_container);
    rotbmp_deinit_container(&minutehand_container);
    bmp_deinit_container(&centerdot_container);
    window_deinit(&window);
}

void pbl_main(void *params)
{
    PebbleAppHandlers handlers = {
        .init_handler = &handle_init,
        .deinit_handler = &handle_deinit,
        .tick_info = {
            .tick_handler = &handle_tick,
            .tick_units = SECOND_UNIT
        }
    };

    // If there's no second-hand, we can update every minute instead and
    // save some power.
    if(!enable_seconds)
        handlers.tick_info.tick_units = MINUTE_UNIT;

    app_event_loop(params, &handlers);
}
