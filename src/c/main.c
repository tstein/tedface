#include <pebble.h>

static const bool debug_layout = false;
static const uint8_t padding = 5;

static Window *s_main_window;
static TextLayer *s_time_layer;
static TextLayer *s_ampm_layer;
static TextLayer *s_date_layer;
static GBitmap *bh_flag;
static BitmapLayer *bh_flag_layer;
static TextLayer *s_bh_time_layer;
static TextLayer *s_battery_level_layer;
static TextLayer *s_temperature_layer;
static BitmapLayer *s_icon_layer;
static GBitmap *s_icon_bitmap = NULL;

static uint8_t battery_charge;

static AppSync s_sync;
static uint8_t s_sync_buffer[64];

enum WeatherKey {
  WEATHER_ICON_KEY = 0x0,         // TUPLE_INT
  WEATHER_TEMPERATURE_KEY = 0x1,  // TUPLE_CSTRING
  WEATHER_CITY_KEY = 0x2,         // TUPLE_CSTRING
};

static const uint32_t WEATHER_ICONS[] = {
  RESOURCE_ID_IMAGE_SUN, // 0
  RESOURCE_ID_IMAGE_CLOUD, // 1
  RESOURCE_ID_IMAGE_RAIN, // 2
  RESOURCE_ID_IMAGE_SNOW // 3
};

static void sync_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Sync Error: %d", app_message_error);
}

static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
  switch (key) {
    case WEATHER_ICON_KEY:
      if (s_icon_bitmap) {
        gbitmap_destroy(s_icon_bitmap);
      }

      s_icon_bitmap = gbitmap_create_with_resource(WEATHER_ICONS[new_tuple->value->uint8]);
      bitmap_layer_set_compositing_mode(s_icon_layer, GCompOpSet);
      bitmap_layer_set_bitmap(s_icon_layer, s_icon_bitmap);
      break;

    case WEATHER_TEMPERATURE_KEY:
      // App Sync keeps new_tuple in s_sync_buffer, so we may use it directly
      text_layer_set_text(s_temperature_layer, new_tuple->value->cstring);
      break;

    case WEATHER_CITY_KEY:
      break;
  }
}

static void request_weather(void) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

  if (!iter) {
    // Error creating outbound message
    return;
  }

  int value = 1;
  dict_write_int(iter, 1, &value, sizeof(int), true);
  dict_write_end(iter);

  app_message_outbox_send();
}


static void update_time() {
  static char time_buffer[11];
  static char ampm_buffer[3];
  static char date_buffer[16];
  static char bh_time_buffer[15];
  static char battery_level_buffer[4];

  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  struct tm *bh_temp_tm = gmtime(&temp);
  bh_temp_tm->tm_hour += 3;
  time_t bh_time = mktime(bh_temp_tm);
  struct tm *bh_tm = gmtime(&bh_time);

  strftime(time_buffer, sizeof(time_buffer), "%l:%M", tick_time);
  text_layer_set_text(s_time_layer, time_buffer);

  strncpy(ampm_buffer, tick_time->tm_hour < 12 ? "AM" : "PM", sizeof(ampm_buffer));
  text_layer_set_text(s_ampm_layer, ampm_buffer);

  strftime(date_buffer, sizeof(date_buffer), "%a, %b %d", tick_time);
  text_layer_set_text(s_date_layer, date_buffer);

  strftime(bh_time_buffer, sizeof(bh_time_buffer),
           bh_tm->tm_hour < 12 ? "%l AM" : "%l PM",
           bh_tm);
  text_layer_set_text(s_bh_time_layer, bh_time_buffer);

  snprintf(battery_level_buffer, sizeof(battery_level_buffer),
           battery_charge == 100 ? "1.0" : ".%u", battery_charge / 10);
  text_layer_set_text(s_battery_level_layer, battery_level_buffer);
}

static void main_window_load(Window *window) {
  bh_flag = gbitmap_create_with_resource(RESOURCE_ID_BH_FLAG);

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  uint8_t w = bounds.size.w;
  uint8_t h = bounds.size.h;

  uint8_t w_min = padding, w_full = w - (2 * padding), w_max = w - padding;
  uint8_t h_min = padding, h_full = h - (2 * padding), h_max = h - padding;

  s_battery_level_layer = text_layer_create(GRect(w_max - 20, h_min, 20, 15));

  s_date_layer = text_layer_create(GRect(w_min, 0, w_full, 28));
  s_time_layer = text_layer_create(GRect(w_min, 24, w_full, 49));
  s_ampm_layer = text_layer_create(GRect(w_max - 50, 73, 50, 30));

  s_icon_layer = bitmap_layer_create(GRect(w_max - 0, 103, 0, 0));
  s_temperature_layer = text_layer_create(GRect(w_min, 80, 50, 30));

  bh_flag_layer = bitmap_layer_create(GRect(w_max - 48, 113, 48, 30));
  s_bh_time_layer = text_layer_create(GRect(w_max - 60, 136, 60, 28));

  // style battery level layer
  text_layer_set_background_color(s_battery_level_layer, debug_layout ? GColorWhite : GColorClear);
  text_layer_set_text_color(s_battery_level_layer, debug_layout ? GColorBlack : GColorWhite);
  text_layer_set_text_alignment(s_battery_level_layer, GTextAlignmentRight);

  // style time layer
  text_layer_set_background_color(s_time_layer, debug_layout ? GColorWhite : GColorClear);
  text_layer_set_text_color(s_time_layer, debug_layout ? GColorBlack : GColorWhite);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

  // style ampm layer
  text_layer_set_background_color(s_ampm_layer, debug_layout ? GColorWhite : GColorClear);
  text_layer_set_text_color(s_ampm_layer, debug_layout ? GColorBlack : GColorWhite);
  text_layer_set_font(s_ampm_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_text_alignment(s_ampm_layer, GTextAlignmentRight);

  // style date layer
  text_layer_set_background_color(s_date_layer, debug_layout ? GColorWhite : GColorClear);
  text_layer_set_text_color(s_date_layer, debug_layout ? GColorBlack : GColorWhite);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentLeft);

  // style bh time layers
  bitmap_layer_set_compositing_mode(bh_flag_layer, GCompOpSet);
  bitmap_layer_set_bitmap(bh_flag_layer, bh_flag);

  text_layer_set_background_color(s_bh_time_layer, debug_layout ? GColorWhite : GColorClear);
  text_layer_set_text_color(s_bh_time_layer, debug_layout ? GColorBlack : GColorWhite);
  text_layer_set_font(s_bh_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_bh_time_layer, GTextAlignmentRight);

  // style local weather
  bitmap_layer_set_compositing_mode(s_icon_layer, GCompOpSet);
  bitmap_layer_set_bitmap(s_icon_layer, s_icon_bitmap);
  text_layer_set_background_color(s_temperature_layer, debug_layout ? GColorWhite : GColorClear);
  text_layer_set_text_color(s_temperature_layer, debug_layout ? GColorBlack : GColorWhite);
  text_layer_set_font(s_temperature_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_temperature_layer, GTextAlignmentRight);


  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_ampm_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(bh_flag_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_bh_time_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_battery_level_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_icon_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_temperature_layer));

  // Start requesting weather.
  Tuplet initial_values[] = {
    TupletInteger(WEATHER_ICON_KEY, (uint8_t) 1),
    TupletCString(WEATHER_TEMPERATURE_KEY, "...\u00B0F"),
    TupletCString(WEATHER_CITY_KEY, "St Pebblesburg"),
  };

  app_sync_init(&s_sync, s_sync_buffer, sizeof(s_sync_buffer),
      initial_values, ARRAY_LENGTH(initial_values),
      sync_tuple_changed_callback, sync_error_callback, NULL);

  request_weather();
}

static void main_window_unload(Window *window) {
  // Destroy TextLayer
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_ampm_layer);
  text_layer_destroy(s_date_layer);
  gbitmap_destroy(bh_flag);
  bitmap_layer_destroy(bh_flag_layer);
  text_layer_destroy(s_bh_time_layer);
  text_layer_destroy(s_battery_level_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static void battery_state_handler(BatteryChargeState charge) {
  battery_charge = charge.charge_percent;
}



static void init() {
  // Create main Window element and assign to pointer
  s_main_window = window_create();

  window_set_background_color(s_main_window, GColorBlack);

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_state_handler);

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);

  // make sure we have data
  update_time();
  battery_state_handler(battery_state_service_peek());

  app_message_open(64, 64);
}

static void deinit() {
  // Destroy Window
  window_destroy(s_main_window);

  app_sync_deinit(&s_sync);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
