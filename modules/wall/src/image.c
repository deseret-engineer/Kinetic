#include "lw.h"
#include <stdio.h>
#include <jpeglib.h>
#include <png.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <webp/decode.h>

static uint8_t *load_png(const char *path, int *w, int *h) {
  png_image img = {.version = PNG_IMAGE_VERSION};
  if (!png_image_begin_read_from_file(&img, path)) return NULL;
  img.format = PNG_FORMAT_RGBA;
  uint8_t *buf = malloc(PNG_IMAGE_SIZE(img));
  if (!buf || !png_image_finish_read(&img, NULL, buf, 0, NULL)) {
    png_image_free(&img);
    free(buf);
    return NULL;
  }
  *w = img.width;
  *h = img.height;
  return buf;
}

struct jerr {
  struct jpeg_error_mgr mgr;
  jmp_buf jmp;
};

static void jpeg_fail(j_common_ptr c) { longjmp(((struct jerr *)c->err)->jmp, 1); }

static uint8_t *load_jpeg(const char *path, int *w, int *h) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  struct jpeg_decompress_struct c;
  struct jerr err;
  uint8_t *volatile buf = NULL;
  c.err = jpeg_std_error(&err.mgr);
  err.mgr.error_exit = jpeg_fail;
  if (setjmp(err.jmp)) {
    jpeg_destroy_decompress(&c);
    fclose(f);
    free(buf);
    return NULL;
  }
  jpeg_create_decompress(&c);
  jpeg_stdio_src(&c, f);
  jpeg_read_header(&c, TRUE);
  c.out_color_space = JCS_EXT_RGBA;
  jpeg_start_decompress(&c);
  *w = c.output_width;
  *h = c.output_height;
  buf = malloc((size_t)*w * *h * 4);
  while (c.output_scanline < c.output_height) {
    JSAMPROW row = buf + (size_t)c.output_scanline * *w * 4;
    jpeg_read_scanlines(&c, &row, 1);
  }
  jpeg_finish_decompress(&c);
  jpeg_destroy_decompress(&c);
  fclose(f);
  return buf;
}

static uint8_t *load_webp(const char *path, int *w, int *h) {
  size_t len;
  char *data = lw_read_file(path, &len);
  if (!data) return NULL;
  uint8_t *out = WebPDecodeRGBA((const uint8_t *)data, len, w, h);
  free(data);
  if (!out) return NULL;
  /* hand back plain malloc memory so callers can free() it */
  size_t n = (size_t)*w * *h * 4;
  uint8_t *copy = malloc(n);
  memcpy(copy, out, n);
  WebPFree(out);
  return copy;
}

uint8_t *lw_image_load(const char *path, int *w, int *h) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  uint8_t magic[12] = {0};
  size_t n = fread(magic, 1, sizeof magic, f);
  fclose(f);
  if (n < 12) return NULL;
  if (!memcmp(magic, "\x89PNG", 4)) return load_png(path, w, h);
  if (magic[0] == 0xff && magic[1] == 0xd8) return load_jpeg(path, w, h);
  if (!memcmp(magic, "RIFF", 4) && !memcmp(magic + 8, "WEBP", 4)) return load_webp(path, w, h);
  return NULL;
}

bool lw_image_save_png(const char *path, const uint8_t *rgba, int w, int h, bool flip) {
  png_image img = {.version = PNG_IMAGE_VERSION, .width = w, .height = h, .format = PNG_FORMAT_RGBA};
  return png_image_write_to_file(&img, path, 0, rgba, flip ? -w * 4 : w * 4, NULL) != 0;
}
