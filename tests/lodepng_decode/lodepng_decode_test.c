#include <lvgl.h>
#include <src/draw/lv_image_decoder_private.h>
#include <src/libs/lodepng/lodepng.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char* read_file(const char* root, const char* name, const char* suffix, size_t* size)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.%s", root, name, suffix);
    FILE* file = fopen(path, "rb");
    if(!file) return NULL;
    if(fseek(file, 0, SEEK_END)) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if(length < 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    unsigned char* data = malloc((size_t)length);
    if(!data) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(data, 1, (size_t)length, file);
    int close_result = fclose(file);
    if(read != (size_t)length || close_result) {
        free(data);
        return NULL;
    }
    *size = (size_t)length;
    return data;
}

static int verify_case(const char* root, const char* name)
{
    size_t png_size = 0, expected_size = 0;
    unsigned char* png = read_file(root, name, "png", &png_size);
    unsigned char* expected = read_file(root, name, "rgba", &expected_size);
    if(!png || !expected) return 10;

    unsigned char* output = NULL;
    unsigned width = 0, height = 0;
    unsigned error = lodepng_decode32(&output, &width, &height, png, png_size);
    if(error || !output || expected_size != (size_t)width * height * 4) return 11;
    lv_draw_buf_t* direct = (lv_draw_buf_t*)output;
    if(direct->header.stride != width * 4 || direct->data_size < expected_size ||
       memcmp(direct->data, expected, expected_size) != 0) return 12;
    lv_draw_buf_destroy(direct);

    lv_image_dsc_t source = {0};
    source.header.magic = LV_IMAGE_HEADER_MAGIC;
    source.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    source.data = png;
    source.data_size = png_size;
    lv_image_header_t header = {0};
    if(lv_image_decoder_get_info(&source, &header) != LV_RESULT_OK ||
       header.w != width || header.h != height) return 13;
    source.header.w = header.w;
    source.header.h = header.h;
    lv_image_decoder_dsc_t decoder = {0};
    lv_image_decoder_args_t args = {0};
    args.no_cache = true;
    if(lv_image_decoder_open(&decoder, &source, &args) != LV_RESULT_OK || !decoder.decoded) return 14;
    lv_draw_buf_t* decoded = (lv_draw_buf_t*)decoder.decoded;
    for(size_t i = 0; i < expected_size; i += 4) {
        if(decoded->data[i] != expected[i + 2] || decoded->data[i + 1] != expected[i + 1] ||
           decoded->data[i + 2] != expected[i] || decoded->data[i + 3] != expected[i + 3]) return 15;
    }
    lv_image_decoder_close(&decoder);
    free(expected);
    free(png);
    return 0;
}

static int verify_geometry_rejection(const char* root, const char* name)
{
    size_t size = 0;
    unsigned char* png = read_file(root, name, "png", &size);
    if(!png) return 20;
    unsigned char* output = NULL;
    unsigned width = 0, height = 0;
    unsigned error = lodepng_decode32(&output, &width, &height, png, size);
    free(png);
    if(output) lv_draw_buf_destroy((lv_draw_buf_t*)output);
    return error == 92 && output == NULL ? 0 : 21;
}

int main(int argc, char** argv)
{
    static const char* cases[] = {
        "gray16", "ga16", "rgb8", "rgba8", "rgb16", "rgba16",
        "rgb16-adam7", "rgba16-adam7"
    };
    if(argc != 2) return 2;
    lv_init();
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int result = verify_case(argv[1], cases[i]);
        if(result) {
            fprintf(stderr, "%s failed: %d\n", cases[i], result);
            return result;
        }
    }

    if(verify_geometry_rejection(argv[1], "oversize")) return 20;
    if(verify_geometry_rejection(argv[1], "stride-overflow")) return 21;
    lv_deinit();
    printf("%zu PNG variants and 2 geometry rejections passed\n",
           sizeof(cases) / sizeof(cases[0]));
    return 0;
}
