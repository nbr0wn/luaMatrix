
#pragma once

#ifdef __cplusplus
extern "C" {
#endif


void display_init(void);
void clear_display(void);
void set_pixel(int x, int y, int r, int g, int b);
void vert_line(int x, int y, int len, int r, int g, int b);
void horiz_line(int x, int y, int len, int r, int g, int b);
void fill_rect(int x, int y, int w, int h, int r, int g, int b);
int get_width(void);
int get_height(void);
void set_brightness(int b);
                   
#ifdef __cplusplus
}
#endif
