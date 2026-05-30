#pragma once
#include <stdint.h>
#include <lvgl.h>
class WebServer;
namespace screen_mirror {
  constexpr int CAP_W = 80;
  constexpr int CAP_H = 120;
  void init();   // heap-allocate shadow buffer; call once before enable()
  void enable(bool on);
  bool enabled();
  void on_flush(const lv_area_t* area, const uint8_t* px_map);
  void dims(int& w, int& h);
  bool write_bmp(WebServer& server, int out_w, int out_h);  // stride-downsample shadow -> BMP response; false if disabled
}
