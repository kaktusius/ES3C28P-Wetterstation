#ifndef WEATHER_IMAGES_H
#define WEATHER_IMAGES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Diese Zeilen sagen deinem Hauptsketch, dass die Bilder im .c Tab existieren:
extern const lv_img_dsc_t bgbild;
extern const lv_img_dsc_t sonne64;
extern const lv_img_dsc_t sonneleicht64;
extern const lv_img_dsc_t mond64;
extern const lv_img_dsc_t mondleicht64;
extern const lv_img_dsc_t wolkeleicht64;
extern const lv_img_dsc_t wolkestark64;
extern const lv_img_dsc_t gewitter64;
extern const lv_img_dsc_t saufgang24;
extern const lv_img_dsc_t suntergang24;
extern const lv_img_dsc_t sonne32;
extern const lv_img_dsc_t sonneleicht32;
extern const lv_img_dsc_t mond32;
extern const lv_img_dsc_t mondleicht32;
extern const lv_img_dsc_t wolkeleicht32;
extern const lv_img_dsc_t wolkestark32;
extern const lv_img_dsc_t regen32;
extern const lv_img_dsc_t gewitter32;
extern const lv_img_dsc_t schnee32;
extern const lv_img_dsc_t temp40;
extern const lv_img_dsc_t humi;
extern const lv_img_dsc_t druck;
extern const lv_img_dsc_t windzeig;

#ifdef __cplusplus
}
#endif

#endif // WEATHER_IMAGES_H