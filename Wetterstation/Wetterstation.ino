/*
* Wetterstation mit WLAN
* Open Meteo Wetterdaten
* Tag/ Nachtmodus
* Datum und Uhrzeit synchronisiert
* Temperatur farbig nach Grad
* Luftfeuchte, Luftdruck
* Windrichtung mit Richtungspfeil aktiv nach Windrichtung
* Sonnenauf- und untergang
* Seite 2  per Touch >>Wettervorhersage 5 Tage
* Transparente Icons je nach Wetterdaten
*/

// #### USER SETUP beachten! PIN- Belegung TFT Display ##########################
// #### LVGL 8.2... lvgl.h muss im Ordner  ../Dokumente/Arduino/lvgl.h
// #### TFT eSPI Version 2.5.43 Bodmer
// #### ArduinoJson Version 7.0.4 Benoit
// #### ESP32 S3  ES3C28P Modul mit capazitiven Touch - 16MB Flash - 8MB PSRAM -
// ###############################################################################
#define LV_COLOR_DEPTH 16

#include <lvgl.h>
#include <TFT_eSPI.h>
#include "weather_images.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "touch.h"


TFT_eSPI tft = TFT_eSPI();

void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color16_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp_drv);
}
static lv_disp_draw_buf_t draw_buf_dsc;
static lv_color_t *draw_buf;

const char *ssid = "#######";               // ### WLAN SSID ###
const char *password = "########";  // ### WLAN PASSWORD ###

String latitude = "##.##";
String longitude = "##.##";
String location = "#######";
String timezone = "Europe/Berlin"; // für Deutschland

String current_date;
String last_weather_update;
String temperature;
String humidity;
String pressure;
String wind;

int is_day;
int weather_code = 0;
String weather_description;
int windrichtung_grad = 0;

JsonDocument doc;  // !! Hier setzen

#define TEMP_CELSIUS 1

#if TEMP_CELSIUS
String temperature_unit = "";
const char degree_symbol[] = "\u00B0C";
#else
String temperature_unit = "&temperature_unit=fahrenheit";
const char degree_symbol[] = "\u00B0F";
#endif

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))

unsigned long lastWeatherUpdate = 0;
const unsigned long weatherInterval = 30 * 60 * 1000;  // 30 Minuten in Millisekunden

// --- Hauptseite (Screen 1) ---
static lv_obj_t *ui_screen_main;
static lv_obj_t *bg_image;
lv_obj_t *img_sunrise;
lv_obj_t *img_sunset;
lv_obj_t *label_sunrise;
lv_obj_t *label_sunset;
static lv_obj_t *weather_image;  // Großes Icon
static lv_obj_t *text_label_date, *text_label_time, *text_label_temperature;
static lv_obj_t *text_label_humidity, *text_label_pressure, *text_label_wind;
static lv_obj_t *text_label_weather_description, *text_label_time_location;
static lv_obj_t *weather_wind_arrow;

// --- Vorschau (Screen 2) ---
lv_obj_t *screen_forecast;
static lv_obj_t *forecast_day_labels[5];
static lv_obj_t *forecast_temp_labels[5];
static lv_obj_t *forecast_icons[5];

// --- Daten-Arrays für Wetter ---
float forecast_max[5], forecast_min[5];
int forecast_code[5];

// Für die Astronomie-Daten
String sunrise_time = "00:00", sunset_time = "00:00";

//--- Uhrzeit-----
String get_local_time() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "--:-- Uhr";
  }
  char timeStringBuff[15];
  // %H:%M für Stunden:Minuten
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M Uhr", &timeinfo);
  return String(timeStringBuff);
}
// Tagberechnung
// Erweiterte Tagberechnung mit Versatz (offset)
String get_formatted_date(int offset = 0) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "--.--.----";
  }

  // Wir rechnen den Offset auf die aktuelle Zeit oben drauf
  time_t rawtime = mktime(&timeinfo);
  rawtime += (offset * 24 * 3600);  // offset in Sekunden (24h * 3600s)
  struct tm *target_time = localtime(&rawtime);

  const char *days[] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" };

  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%s, %02d.%02d.%d",
           days[target_time->tm_wday],
           target_time->tm_mday,
           target_time->tm_mon + 1,
           target_time->tm_year + 1900);

  return String(buffer);
}

// Windpfeil aktiv ausrichten
String get_wind_direction_short(int degree) {
  if (degree >= 337.5 || degree < 22.5) return " N";
  if (degree >= 22.5 && degree < 67.5) return "N-O";
  if (degree >= 67.5 && degree < 112.5) return " O";
  if (degree >= 112.5 && degree < 157.5) return "S-O";
  if (degree >= 157.5 && degree < 202.5) return " S";
  if (degree >= 202.5 && degree < 247.5) return "S-W";
  if (degree >= 247.5 && degree < 292.5) return " W";
  if (degree >= 292.5 && degree < 337.5) return "N-W";
  return "-";  // Fallback
}


// ################################### Wetterseite 2 ####################################################

//--------------------------------------------------
void create_forecast_gui() {
  screen_forecast = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen_forecast, lv_color_hex(0x000820), 0);
  lv_obj_add_flag(screen_forecast, LV_OBJ_FLAG_CLICKABLE);

  // Navigation zurück
  lv_obj_add_event_cb(
    screen_forecast, [](lv_event_t *e) {
      if (ui_screen_main) {
        lv_scr_load_anim(ui_screen_main, LV_SCR_LOAD_ANIM_FADE_ON, 350, 0, false);
      }
    },
    LV_EVENT_CLICKED, NULL);

  // Titel
  lv_obj_t *title = lv_label_create(screen_forecast);
  lv_label_set_text(title, "5-Tage Wettervorhersage");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_set_style_text_color(title, lv_color_hex(0xAAAAAA), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

  for (int i = 0; i < 5; i++) {
    // Y-Position: Start bei 40px, Zeilenhöhe ca. 38px
    int y_pos = 40 + (i * 38);

    // 1. TAG (Ganz links)
    forecast_day_labels[i] = lv_label_create(screen_forecast);
    lv_obj_align(forecast_day_labels[i], LV_ALIGN_TOP_LEFT, 10, y_pos);
    lv_obj_set_style_text_color(forecast_day_labels[i], lv_color_hex(0xABCDEF), 0);
    lv_obj_set_style_text_font(forecast_day_labels[i], &lv_font_montserrat_14, 0);

    // 2. ICON (Mittig-Links)
    forecast_icons[i] = lv_img_create(screen_forecast);
    lv_obj_align(forecast_icons[i], LV_ALIGN_TOP_LEFT, 110, y_pos - 4);
    lv_img_set_zoom(forecast_icons[i], 240);

    // 3. TEMPERATUR (Ganz rechts)
    forecast_temp_labels[i] = lv_label_create(screen_forecast);
    lv_obj_align(forecast_temp_labels[i], LV_ALIGN_TOP_RIGHT, -10, y_pos);
    lv_obj_set_style_text_color(forecast_temp_labels[i], lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(forecast_temp_labels[i], &lv_font_montserrat_14, 0);

    // 4. TRENNLINIE (Horizontaler Strich unter jeder Zeile, außer der letzten)
    if (i < 4) {
      lv_obj_t *line = lv_obj_create(screen_forecast);
      lv_obj_set_size(line, 300, 1);                               // Breite 280px, Höhe 1px
      lv_obj_set_style_bg_color(line, lv_color_hex(0x333333), 0);  // Dunkelgrau
      lv_obj_set_style_border_side(line, LV_BORDER_SIDE_NONE, 0);
      lv_obj_align(line, LV_ALIGN_TOP_MID, 0, y_pos + 28);
    }
  }
}

//----------------------------------------------------------
void update_forecast_ui() {
  for (int i = 0; i < 5; i++) {
    // Tag setzen (z.B. "Heute" oder "Mo, 21.03.")
    String d_name = (i == 0) ? "Heute" : get_formatted_date(i).substring(0, 10);
    lv_label_set_text(forecast_day_labels[i], d_name.c_str());

    // Icon Mapping
    const lv_img_dsc_t *src;
    int code = forecast_code[i];
    if (code == 0) src = &sonne32;
    if (code == 1) src = &sonneleicht32;
    if (code == 3) src = &wolkeleicht32;
    else if (code >= 51 && code <= 82) src = &regen32;
    else if (code >= 85 && code <= 86) src = &schnee32;
    else if (code >= 95) src = &gewitter32;
    else src = &wolkeleicht32;

    lv_img_set_src(forecast_icons[i], src);

    // Temperatur: Max / Min mit °C
    char buf[32];
    snprintf(buf, sizeof(buf), "Max: %.0f°C | Min: %.0f°C", forecast_max[i], forecast_min[i]);
    lv_label_set_text(forecast_temp_labels[i], buf);
  }
}


//----------------------------------------------------------------------






// ##############################--- Wetter UI-Aktualisierung ---#######################################
void update_weather_ui() {
  if (temperature == "" || humidity == "") return;

  // --- 1. HINTERGRUND & ALLGEMEINE SCHRIFTFARBEN (TAG/NACHT) ---
  if (bg_image) {
    if (is_day == 1) {
      // TAGMODUS
      lv_obj_set_style_img_recolor_opa(bg_image, 0, 0);
      if (text_label_date) lv_obj_set_style_text_color(text_label_date, lv_color_hex(0x000099), 0);
      if (text_label_time) lv_obj_set_style_text_color(text_label_time, lv_color_hex(0x000099), 0);
      if (text_label_humidity) lv_obj_set_style_text_color(text_label_humidity, lv_color_hex(0x5959ff), 0);
      if (text_label_pressure) lv_obj_set_style_text_color(text_label_pressure, lv_color_hex(0x476185), 0);
      if (text_label_wind) lv_obj_set_style_text_color(text_label_wind, lv_color_hex(0x478547), 0);
      if (text_label_time_location) lv_obj_set_style_text_color(text_label_time_location, lv_color_hex(0x2e5ba8), 0);
      if (text_label_weather_description) lv_obj_set_style_text_color(text_label_weather_description, lv_color_hex(0x000000), 0);
    } else {
      // NACHTMODUS (Abdunkeln & Helle Schriften)
      lv_obj_set_style_img_recolor(bg_image, lv_color_hex(0x001040), 0);
      lv_obj_set_style_img_recolor_opa(bg_image, 170, 0);
      if (text_label_date) lv_obj_set_style_text_color(text_label_date, lv_color_hex(0xABCDEF), 0);
      if (text_label_time) lv_obj_set_style_text_color(text_label_time, lv_color_hex(0xABCDEF), 0);
      if (text_label_humidity) lv_obj_set_style_text_color(text_label_humidity, lv_color_hex(0x8080FF), 0);
      if (text_label_pressure) lv_obj_set_style_text_color(text_label_pressure, lv_color_hex(0xBDC3C7), 0);
      if (text_label_wind) lv_obj_set_style_text_color(text_label_wind, lv_color_hex(0x90EE90), 0);
      if (text_label_time_location) lv_obj_set_style_text_color(text_label_time_location, lv_color_hex(0xADD8E6), 0);
      if (text_label_weather_description) lv_obj_set_style_text_color(text_label_weather_description, lv_color_hex(0xFFFFFF), 0);
    }
  }
  // DATUM AKTUALISIEREN (Wichtig: Nach der Farbwahl!)
  if (text_label_date) {
    current_date = get_formatted_date();
    if (current_date != "") {
      lv_label_set_text(text_label_date, current_date.c_str());
    } else {
      lv_label_set_text(text_label_date, "--.--.----");  // Fallback, falls leer
    }
  }
  // --- 2. TEMPERATUR (KONTRAST-LOGIK) ---
  if (text_label_temperature) {
    lv_label_set_text(text_label_temperature, String(temperature + degree_symbol).c_str());
    float t = temperature.toFloat();
    if (is_day == 1) {
      if (t <= 0) lv_obj_set_style_text_color(text_label_temperature, lv_color_hex(0x00A0FF), 0);
      else if (t > 28) lv_obj_set_style_text_color(text_label_temperature, lv_color_hex(0xFF4500), 0);
      else lv_obj_set_style_text_color(text_label_temperature, lv_color_hex(0x003d61), 0);
    } else {
      // Nacht-Kontrast für Temperatur
      if (t <= 0) lv_obj_set_style_text_color(text_label_temperature, lv_color_hex(0x00E0FF), 0);
      else if (t > 28) lv_obj_set_style_text_color(text_label_temperature, lv_color_hex(0xFF8C00), 0);
      else lv_obj_set_style_text_color(text_label_temperature, lv_color_hex(0xFFFFFF), 0);
    }
  }

  // --- 3. RESTLICHE WERTE & ICONS ---
  if (text_label_humidity) lv_label_set_text(text_label_humidity, String(humidity + " %").c_str());
  if (text_label_pressure) lv_label_set_text(text_label_pressure, String(pressure + " hPa").c_str());

  String Richtung = get_wind_direction_short(windrichtung_grad);
  if (text_label_wind) lv_label_set_text(text_label_wind, String(wind + " km/h -" + Richtung + " ").c_str());
  if (weather_wind_arrow) lv_img_set_angle(weather_wind_arrow, windrichtung_grad * 10);

  get_weather_description(weather_code);
  if (text_label_weather_description) {
    lv_label_set_text(text_label_weather_description, weather_description.c_str());
  }

  if (label_sunrise && label_sunset) {
    lv_label_set_text(label_sunrise, String(sunrise_time + " Uhr").c_str());
    lv_label_set_text(label_sunset, String(sunset_time + " Uhr").c_str());
  }
}



// ####################--- Die eigentliche Timer-Callback-Funktion ---#############################################
static void timer_cb(lv_timer_t *timer) {
  static uint32_t weather_counter = 0;

  // Uhrzeit jede Sekunde
  if (text_label_time) {
    lv_label_set_text(text_label_time, get_local_time().c_str());
  }

  // Wetter alle 10 Minuten
  if (weather_counter % 600 == 0) {
    get_weather_data();   // 1. Daten holen (is_day wird hier gesetzt!)
    update_weather_ui();  // 2. Alles anzeigen (inkl. Nachtmodus-Farben)
  }

  weather_counter++;
}




// ############################################ MAIN GUI  ###########################################
void lv_create_main_gui(void) {



  ui_screen_main = lv_scr_act();

  // 2. Hintergrund zuerst
  bg_image = lv_img_create(ui_screen_main);
  lv_img_set_src(bg_image, &bgbild);
  lv_obj_align(bg_image, LV_ALIGN_CENTER, 0, 0);

  // --- NEU: Ein unsichtbarer Riesen-Button über das ganze Display ---
  lv_obj_t *screen_btn = lv_btn_create(ui_screen_main);
  lv_obj_set_size(screen_btn, 320, 240);            // Volle Displaygröße
  lv_obj_align(screen_btn, LV_ALIGN_CENTER, 0, 0);  // Mittig platzieren

  // Macht den Button komplett unsichtbar (Hintergrund-Transparenz auf 0)
  lv_obj_set_style_bg_opa(screen_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(screen_btn, 0, 0);  // Schatten entfernen
  lv_obj_set_style_border_width(screen_btn, 0, 0);  // Rahmen entfernen

  // Das Klick-Event direkt an den Button hängen
  lv_obj_add_event_cb(
    screen_btn, [](lv_event_t *e) {
      Serial.println("[GUI] Vollbild-Button geklickt -> Wechsle zu Seite 2 (Forecast)...");
      if (screen_forecast) {
        update_forecast_ui();
        lv_scr_load_anim(screen_forecast, LV_SCR_LOAD_ANIM_FADE_ON, 350, 0, false);
      } else {
        Serial.println("[ERROR] screen_forecast ist NULL! Wurde create_forecast_gui() im setup() aufgerufen?");
      }
    },
    LV_EVENT_CLICKED, NULL);

  // 3. ALLE Objekte erstellen (mit Platzhalter-Texten!)
  // Wetter Icon Groß
  weather_image = lv_img_create(ui_screen_main);
  lv_img_set_src(weather_image, &sonne64);  // Default-Icon setzen
  lv_img_set_zoom(weather_image, 512);
  lv_obj_align(weather_image, LV_ALIGN_CENTER, -90, -20);

  // Datum
  text_label_date = lv_label_create(ui_screen_main);
  lv_label_set_text(text_label_date, "--.--.----");
  lv_obj_align(text_label_date, LV_ALIGN_TOP_LEFT, 15, 10);
  lv_obj_set_style_text_font(text_label_date, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(text_label_date, lv_color_hex(0x000099), 0);

  // Uhrzeit
  text_label_time = lv_label_create(ui_screen_main);
  lv_label_set_text(text_label_time, "--:-- Uhr");
  lv_obj_align(text_label_time, LV_ALIGN_TOP_RIGHT, -5, 10);
  lv_obj_set_style_text_font(text_label_time, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(text_label_time, lv_color_hex(0x000099), 0);

  // Temperatur Sektion
  lv_obj_t *img_temp_icon = lv_img_create(ui_screen_main);
  lv_img_set_src(img_temp_icon, &temp40);
  lv_obj_align(img_temp_icon, LV_ALIGN_CENTER, 0, -50);

  text_label_temperature = lv_label_create(ui_screen_main);
  lv_label_set_text(text_label_temperature, "--.-°C");
  lv_obj_align(text_label_temperature, LV_ALIGN_RIGHT_MID, -5, -50);
  lv_obj_set_style_text_font(text_label_temperature, &lv_font_montserrat_40, 0);
  lv_obj_set_style_text_color(text_label_temperature, lv_color_hex(0x003d61), 0);

  // Luftfeuchte Sektion
  lv_obj_t *img_hum_icon = lv_img_create(ui_screen_main);
  lv_img_set_src(img_hum_icon, &humi);
  lv_obj_align(img_hum_icon, LV_ALIGN_CENTER, 0, -15);

  text_label_humidity = lv_label_create(ui_screen_main);
  lv_label_set_text(text_label_humidity, "-- %");
  lv_obj_align(text_label_humidity, LV_ALIGN_RIGHT_MID, -5, -15);
  lv_obj_set_style_text_font(text_label_humidity, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(text_label_humidity, lv_color_hex(0x5959ff), 0);

  // Luftdruck Sektion
  lv_obj_t *img_press_icon = lv_img_create(ui_screen_main);
  lv_img_set_src(img_press_icon, &druck);
  lv_obj_align(img_press_icon, LV_ALIGN_CENTER, 0, 10);

  text_label_pressure = lv_label_create(ui_screen_main);
  lv_label_set_text(text_label_pressure, "---- hPa");
  lv_obj_align(text_label_pressure, LV_ALIGN_RIGHT_MID, -5, 10);
  lv_obj_set_style_text_font(text_label_pressure, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(text_label_pressure, lv_color_hex(0x476185), 0);

  // Wind Sektion
  weather_wind_arrow = lv_img_create(ui_screen_main);
  lv_img_set_src(weather_wind_arrow, &windzeig);
  lv_obj_align(weather_wind_arrow, LV_ALIGN_CENTER, 0, 35);

  text_label_wind = lv_label_create(ui_screen_main);
  lv_label_set_text(text_label_wind, "-- km/h");
  lv_obj_align(text_label_wind, LV_ALIGN_RIGHT_MID, -5, 35);
  lv_obj_set_style_text_font(text_label_wind, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(text_label_wind, lv_color_hex(0x478547), 0);

  // Wetterbeschreibung (Text unter Icon)
  text_label_weather_description = lv_label_create(ui_screen_main);
  lv_label_set_text(text_label_weather_description, "Lade...");
  lv_obj_align(text_label_weather_description, LV_ALIGN_LEFT_MID, 20, 65);
  lv_obj_set_style_text_font(text_label_weather_description, &lv_font_montserrat_18, 0);

  // --- SONNENAUFGANG GRUPPE ---
  // Das Icon erstellen
  lv_obj_t *img_sunrise = lv_img_create(ui_screen_main);
  lv_img_set_src(img_sunrise, &saufgang24);
  lv_obj_align(img_sunrise, LV_ALIGN_BOTTOM_LEFT, 30, -10);  // Ganz links unten

  // Das Label direkt daneben setzen
  label_sunrise = lv_label_create(ui_screen_main);
  lv_label_set_text(label_sunrise, "--:-- Uhr");
  lv_obj_set_style_text_font(label_sunrise, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(label_sunrise, lv_color_hex(0x0a2b63), 0);
  // Ausrichtung: Rechts neben das Aufgang-Icon
  lv_obj_align_to(label_sunrise, img_sunrise, LV_ALIGN_OUT_RIGHT_MID, 10, 0);


  // --- SONNENUNTERGANG GRUPPE ---
  // Das Icon erstellen
  lv_obj_t *img_sunset = lv_img_create(ui_screen_main);
  lv_img_set_src(img_sunset, &suntergang24);
  lv_obj_align(img_sunset, LV_ALIGN_BOTTOM_LEFT, 180, -10);
  label_sunset = lv_label_create(ui_screen_main);
  lv_label_set_text(label_sunset, "--:-- Uhr");
  lv_obj_set_style_text_font(label_sunset, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(label_sunset, lv_color_hex(0x0a2b63), 0);
  lv_obj_align_to(label_sunset, img_sunset, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  // 4. Timer für Uhrzeit und Wetter-Intervall (1 Sekunde)
  lv_timer_create(timer_cb, 1000, NULL);
}




/*
  WMO Weather interpretation codes (WW)- Code	Description
  0	Clear sky
  1, 2, 3	Mainly clear, partly cloudy, and overcast
  45, 48	Fog and depositing rime fog
  51, 53, 55	Drizzle: Light, moderate, and dense intensity
  56, 57	Freezing Drizzle: Light and dense intensity
  61, 63, 65	Rain: Slight, moderate and heavy intensity
  66, 67	Freezing Rain: Light and heavy intensity
  71, 73, 75	Snow fall: Slight, moderate, and heavy intensity
  77	Snow grains
  80, 81, 82	Rain showers: Slight, moderate, and violent
  85, 86	Snow showers slight and heavy
  95 *	Thunderstorm: Slight or moderate
  96, 99 *	Thunderstorm with slight and heavy hail
*/
// -------------------- Wetteranimationen ------------------------------------------------

// Regen ---------------------
#define NUM_DROPS 5
static lv_obj_t *rain_drops[NUM_DROPS];
// Callback für die Bewegung
static void rain_anim_cb(void *var, int32_t v) {
  lv_obj_set_y((lv_obj_t *)var, v);
}
void set_rain_animation(bool active) {
  for (int i = 0; i < NUM_DROPS; i++) {
    if (active && rain_drops[i] == NULL) {
      rain_drops[i] = lv_obj_create(lv_scr_act());
      lv_obj_set_size(rain_drops[i], 4, 7);  
      lv_obj_set_style_bg_color(rain_drops[i], lv_palette_main(LV_PALETTE_BLUE), 0);
      lv_obj_set_style_border_side(rain_drops[i], LV_BORDER_SIDE_NONE, 0);
      lv_obj_set_x(rain_drops[i], 60 + (i * 12));
      lv_anim_t a;
      lv_anim_init(&a);
      lv_anim_set_var(&a, rain_drops[i]);
      lv_anim_set_values(&a, 80, 150);    
      lv_anim_set_time(&a, 700 + (i * 100));  
      lv_anim_set_exec_cb(&a, rain_anim_cb);
      lv_anim_set_path_cb(&a, lv_anim_path_linear);
      lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
      lv_anim_set_delay(&a, i * 200);  // 
      lv_anim_start(&a);
    } else if (!active && rain_drops[i] != NULL) {
      lv_anim_del(rain_drops[i], NULL);
      lv_obj_del(rain_drops[i]);
      rain_drops[i] = NULL;
    }
  }
}
// regen ende ----------

// schnee --------------
#define NUM_FLAKES 7
static lv_obj_t *snow_flakes[NUM_FLAKES];
void set_snow_animation(bool active) {
  for (int i = 0; i < NUM_FLAKES; i++) {
    if (active && snow_flakes[i] == NULL) {
      snow_flakes[i] = lv_obj_create(lv_scr_act());
      lv_obj_set_size(snow_flakes[i], 5, 5);                               
      lv_obj_set_style_radius(snow_flakes[i], LV_RADIUS_CIRCLE, 0);          
      lv_obj_set_style_bg_color(snow_flakes[i], lv_color_hex(0xFFFFFF), 0);  
      lv_obj_set_style_radius(snow_flakes[i], LV_RADIUS_CIRCLE, 0);          
      lv_obj_set_style_border_side(snow_flakes[i], LV_BORDER_SIDE_NONE, 0);
      lv_obj_set_x(snow_flakes[i], 50 + (i * 12));
      lv_anim_t a;
      lv_anim_init(&a);
      lv_anim_set_var(&a, snow_flakes[i]);
      lv_anim_set_values(&a, 80, 150);
      lv_anim_set_time(&a, 2000 + (i * 300));  // VIEL LANGSAMER (2-3 Sek)
      lv_anim_set_exec_cb(&a, rain_anim_cb);
      lv_anim_set_path_cb(&a, lv_anim_path_linear);
      lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
      lv_anim_set_delay(&a, i * 400);
      lv_anim_start(&a);
    } else if (!active && snow_flakes[i] != NULL) {
      lv_anim_del(snow_flakes[i], NULL);
      lv_obj_del(snow_flakes[i]);
      snow_flakes[i] = NULL;
    }
  }
}
// schnee ende ----------------

// gewitter--------------------
static void cloud_flash_cb(void *var, int32_t v) {
  // Mischt Weiß (recolor) mit einer Intensität von v (0-255) in das Bild
  lv_obj_set_style_img_recolor((lv_obj_t *)var, lv_color_white(), 0);
  lv_obj_set_style_img_recolor_opa((lv_obj_t *)var, v, 0);
}

void trigger_cloud_glow(bool active) {
  if (active) {
    // Animation starten, falls noch nicht geschehen
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, weather_image);
    lv_anim_set_values(&a, 0, 200);     // 0 = normal, 180 = starkes Leuchten
    lv_anim_set_time(&a, 50);           // Schnelles Aufleuchten
    lv_anim_set_playback_time(&a, 80);  // Sanfteres Abklingen
    lv_anim_set_exec_cb(&a, cloud_flash_cb);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, 4000);  // Alle x Sekunden leuchten
    lv_anim_start(&a);
  } else {
    // Animation stoppen und Bild wieder normal setzen
    lv_anim_del(weather_image, cloud_flash_cb);
    lv_obj_set_style_img_recolor_opa(weather_image, 0, 0);
  }
}

// gewitter ende -------------------


// ----- Wettercode mit Animation ## Grosses Icon ##-----------------------------------------------------------------------------------
void get_weather_description(int code) {
  if (weather_image == NULL) return;  //
  set_rain_animation(false);          // <--- Regen aus !
  set_snow_animation(false);          // <--- Schnee aus !
  trigger_cloud_glow(false);          // <--- Gewitter aus !

  switch (code) {
    case 0:
      if (is_day == 1) {
        lv_img_set_src(weather_image, &sonne64); 
      } else {
        lv_img_set_src(weather_image, &mond64);  
      }

      weather_description = "Klarer Himmel";
      break;
    case 1:
      if (is_day == 1) {
        lv_img_set_src(weather_image, &sonneleicht64);
      } else {
        lv_img_set_src(weather_image, &mondleicht64);
      }
      weather_description = "Leicht bedeckt";
      break;
    case 2:
      if (is_day == 1) {
        lv_img_set_src(weather_image, &wolkeleicht64); 
      } else {
        lv_img_set_src(weather_image, &wolkeleicht64); 
      }
      weather_description = "Teils Wolkig";
      break;
    case 3:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "Bedeckt";
      break;
    case 45:
      lv_img_set_src(weather_image, &wolkeleicht64);
      weather_description = "Nebel";
      break;
    case 48:
      lv_img_set_src(weather_image, &wolkeleicht64);
      weather_description = "Reifnebel";
      break;
    case 51:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "leichter Niesel";
      break;
    case 53:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "Nieselregen";
      break;
    case 55:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "Starker Nieselregen";
      break;
    case 56:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "gefrierender Regen";
      break;
    case 57:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "leichte Graupel";
      break;
    case 61:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "Regnerisch";
      break;
    case 63:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "Regenschauer";
      set_rain_animation(true);  // <--- Animation
      break;
    case 65:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "Starker Regen";
      set_rain_animation(true);  // <--- Animation
      break;
    case 66:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "gefrierender Regen";
      break;
    case 67:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "gefrierende Schauer";
      break;
    case 71:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "leichter Schneefall";
      set_snow_animation(true);  // <--- Animation
      break;
    case 73:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "Schneefall";
      set_snow_animation(true);  // <--- Animation
      break;
    case 75:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "starker Schneefall";
      set_snow_animation(true);  // <--- Animation
      break;
    case 77:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "Schneegriesel";
      set_snow_animation(true);  // <--- Animation
      break;
    case 80:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "Regnerisch";
      set_rain_animation(true);  // <--- Animation
      break;
    case 81:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "Schauerregen";
      set_rain_animation(true);  // <--- Animation
      break;
    case 82:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "starke Schauer";
      set_rain_animation(true);  // <--- Animation
      break;
    case 85:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "leichte Schneeschauer";
      set_snow_animation(true);  // <--- Animation
      break;
    case 86:
      lv_img_set_src(weather_image, &wolkestark64);
      weather_description = "starke Schneeschauer";
      set_snow_animation(true);  // <--- Animation
      break;
    case 95:
      lv_img_set_src(weather_image, &gewitter64);
      weather_description = "Gewitter";
      set_rain_animation(true);  // <--- Animation
      trigger_cloud_glow(true);  // <--- Wolke leuchtet auf
      break;
    case 96:
      lv_img_set_src(weather_image, &gewitter64);
      weather_description = "Gewitter mit Hagelschauer";
      trigger_cloud_glow(true);  // <--- Wolke leuchtet auf
      break;
    case 99:
      lv_img_set_src(weather_image, &gewitter64);
      weather_description = "Starkes Gewitter ";
      trigger_cloud_glow(true);  // <--- Wolke leuchtet auf
      set_rain_animation(true);  // <--- Animation
      break;
    default:
      weather_description = "geh gucken :) ";
      break;
  }
}
// ############################# WETTERDATEN UND ABRUFE ###################################################################################
void get_weather_data() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    // API Code - im Browser prüfen auf Funktion
    String url = String("http://api.open-meteo.com/v1/forecast?latitude=" + latitude + "&longitude=" + longitude + "&current=temperature_2m,relative_humidity_2m,is_day,weather_code,pressure_msl,wind_speed_10m,wind_direction_10m&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset&timezone=Europe/Berlin&forecast_days=5");

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode > 0) {
      String payload = http.getString();
      JsonDocument doc; 
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        // --- 1. AKTUELLE DATEN  ---
        temperature = doc["current"]["temperature_2m"].as<String>();
        humidity = doc["current"]["relative_humidity_2m"].as<String>();
        is_day = doc["current"]["is_day"].as<int>();
        weather_code = doc["current"]["weather_code"].as<int>();
        pressure = doc["current"]["pressure_msl"].as<String>();
        wind = doc["current"]["wind_speed_10m"].as<String>();
        windrichtung_grad = doc["current"]["wind_direction_10m"].as<int>();

        // --- 2. TÄGLICHE DATEN (Sonnenzeiten & Forecast) ---
        JsonVariant daily = doc["daily"];
        if (doc.containsKey("daily")) {
          const char *s_rise = doc["daily"]["sunrise"][0];  // Erster Wert im Array (heute)
          const char *s_set = doc["daily"]["sunset"][0];

          // Die API liefert oft "2026-03-18T06:24". Wir brauchen nur ab Stelle 11 (Uhrzeit)
          if (s_rise) sunrise_time = String(s_rise).substring(11, 16);
          if (s_set) sunset_time = String(s_set).substring(11, 16);
        }

        // 3-Tage Arrays befüllen
        for (int i = 0; i < 5; i++) {
          forecast_max[i] = daily["temperature_2m_max"][i].as<float>();
          forecast_min[i] = daily["temperature_2m_min"][i].as<float>();
          forecast_code[i] = daily["weather_code"][i].as<int>();
        }

        // --- 3. DEBUG LOG (Serieller Monitor) ---
        Serial.println("---------- Wetter Update ----------");
        Serial.println("Datum:       " + get_formatted_date());

        if (is_day == 1) Serial.println("Modus:       Tagmodus ☀️");
        else Serial.println("Modus:       Nachtmodus 🌙");

        get_weather_description(weather_code);
        Serial.printf("Wetter:      %s (Code: %d)\n", weather_description.c_str(), weather_code);
        Serial.println("Temperatur:  " + temperature + "°C");
        Serial.printf("Sonne:      %s |      %s\n", sunrise_time.c_str(), sunset_time.c_str());

        Serial.println("--- 3-Tage Vorschau ---");
        for (int i = 0; i < 3; i++) {
          Serial.printf("Tag %d: Max %.1f°C | Min %.1f°C | Code %d\n", i, forecast_max[i], forecast_min[i], forecast_code[i]);
        }

        // Speicher-Check
        Serial.printf("Freies RAM:  %d KB\n", ESP.getFreeHeap() / 1024);
        Serial.println("------------------------------------");

      } else {
        Serial.println("JSON Error: " + String(error.c_str()));
      }
    } else {
      Serial.println("HTTP Error: " + String(httpCode));
    }
    http.end();
  }
}

// ------------ Touchfunktion --------------

void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  if (!touch_touched()) {
    data->state = LV_INDEV_STATE_REL;
  } else {
    data->state = LV_INDEV_STATE_PR;

    int raw_x = 320 - touch_last_y;
    int raw_y = 240 - touch_last_x;

    data->point.x = constrain(raw_x, 0, 319);
    data->point.y = constrain(raw_y, 0, 239);
  }
}

// ################################ setup ############################################
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Wetterstation wird initialisiert...");

  // 1. HARDWARE & DISPLAY INITIALISIEREN 
  tft.begin();
  tft.setRotation(1); 


  // 2. Kapazitiven I2C Touch 
  touch_init(SCREEN_WIDTH, SCREEN_HEIGHT, 3);
  Serial.println("[INFO] Kapazitiver Touch erfolgreich über touch.h gestartet.");

  // 2. LVGL CORE & TREIBER INITIALISIEREN
  lv_init();

  // 8MB PSRAM für einen Full-Screen-Buffer 
  draw_buf = (lv_color_t *)ps_malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(lv_color_t));
  if (draw_buf == NULL) {
    Serial.println("PSRAM Allokation fehlgeschlagen! Nutze internen RAM...");
    static lv_color_t backup_buf[SCREEN_WIDTH * 10];  
    draw_buf = backup_buf;
    lv_disp_draw_buf_init(&draw_buf_dsc, draw_buf, NULL, SCREEN_WIDTH * 10);
  } else {
    Serial.println("PSRAM erfolgreich für LVGL reserviert (Full-Screen).");
    lv_disp_draw_buf_init(&draw_buf_dsc, draw_buf, NULL, SCREEN_WIDTH * SCREEN_HEIGHT);
  }

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf_dsc;
  disp_drv.rotated = LV_DISP_ROT_90;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  lv_obj_set_scrollbar_mode(lv_scr_act(), LV_SCROLLBAR_MODE_OFF);

  lv_create_main_gui();
  create_forecast_gui();

  lv_timer_handler();

  // 4. NETZWERK STARTEN (Mit delay(500) im Loop, um den Watchdog zu füttern!)
  WiFi.begin(ssid, password);
  Serial.print("Verbinde mit WLAN...");
  uint32_t wifi_timeout = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifi_timeout < 8000) {
    delay(500);  
    Serial.print(".");
    lv_timer_handler(); 
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nVerbunden! IP: " + WiFi.localIP().toString());

    // 5. ZEIT-SYNCHRONISATION (NTP)
    configTime(0, 0, "pool.ntp.org");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    Serial.print("Warte auf NTP Zeit-Sync...");

    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 15) {
      delay(500);  // Watchdog-Pause
      Serial.print(".");
      retry++;
      lv_timer_handler();
    }
    Serial.println(" Zeit synchronisiert!");
  } else {
    Serial.println("\nWLAN-Timeout! Starte im Offline-Modus.");
  }

  lastWeatherUpdate = millis() - weatherInterval + 3000;

  Serial.println("Setup abgeschlossen. Übergebe an loop().");
}

// ####### LOOP ####################################################################

void loop() {

  lv_timer_handler();

  if (millis() - lastWeatherUpdate > weatherInterval) {
    lastWeatherUpdate = millis();

    if (WiFi.status() == WL_CONNECTED) {
      get_weather_data();
      update_weather_ui();
    } else {
      Serial.println("Wetter-Update übersprungen: Keine WLAN-Verbindung.");
    }
  }

  // ESP32-S3 Task Watchdog!
  delay(10);
}