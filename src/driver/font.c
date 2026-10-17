#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"

uint32_t osd_font[384 * 1152];

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPFileHeader;

typedef struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMPInfoHeader;
#pragma pack(pop)

void font_init(unsigned char *bmp_path) {
    FILE *fp = fopen((const char *)bmp_path, "rb");
    if (!fp) {
        printf("Error: Cannot open file %s\n", bmp_path);
        return;
    }

    BMPFileHeader file_header;
    BMPInfoHeader info_header;

    if (fread(&file_header, sizeof(BMPFileHeader), 1, fp) != 1 ||
        fread(&info_header, sizeof(BMPInfoHeader), 1, fp) != 1) {
        printf("Error: Failed to read BMP headers.\n");
        fclose(fp);
        return;
    }

    if (file_header.bfType != 0x4D42) {
        printf("Error: Not a valid BMP file.\n");
        fclose(fp);
        return;
    }

    int img_w = info_header.biWidth;
    int img_h = abs(info_header.biHeight);
    int bpp = info_header.biBitCount;
    int is_bottom_up = (info_header.biHeight > 0);

    if (img_w != 384 || img_h != 1152 || (bpp != 24 && bpp != 32)) {
        printf("Error: Unexpected BMP format. Expected 384x1152 (24bpp/32bpp), got %dx%d %dbpp\n",
               img_w, img_h, bpp);
        fclose(fp);
        return;
    }

    size_t img_pixels_count = (size_t)img_w * img_h;
    uint32_t *raw_image = (uint32_t *)malloc(img_pixels_count * sizeof(uint32_t));
    if (!raw_image) {
        printf("Error: Memory allocation failed.\n");
        fclose(fp);
        return;
    }

    fseek(fp, file_header.bfOffBits, SEEK_SET);

    int bytes_per_pixel = bpp / 8;
    int row_stride = ((img_w * bytes_per_pixel + 3) / 4) * 4;
    int padding = row_stride - (img_w * bytes_per_pixel);

    uint8_t *row_buffer = (uint8_t *)malloc(row_stride);
    if (!row_buffer) {
        printf("Error: Memory allocation failed for row buffer.\n");
        free(raw_image);
        fclose(fp);
        return;
    }

    for (int i = 0; i < img_h; i++) {
        int target_row = is_bottom_up ? (img_h - 1 - i) : i;

        if (fread(row_buffer, 1, row_stride, fp) != (size_t)row_stride) {
            printf("Error: Read pixel data failed at row %d\n", i);
            free(row_buffer);
            free(raw_image);
            fclose(fp);
            return;
        }

        uint32_t *dst_row = &raw_image[target_row * img_w];

        if (bpp == 24) {
            for (int col = 0; col < img_w; col++) {
                uint8_t b = row_buffer[col * 3 + 0];
                uint8_t g = row_buffer[col * 3 + 1];
                uint8_t r = row_buffer[col * 3 + 2];

                if (b == 0x7F && g == 0x7F && r == 0x7F)
                    dst_row[col] = 0x00000000; // transparent
                else
                    dst_row[col] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        } else {
            // TODO
            memcpy(dst_row, row_buffer, img_w * sizeof(uint32_t));
        }
    }

    free(row_buffer);
    fclose(fp);

    const int cols = 16;
    const int rows = 32;
    // const int char_w = img_w / cols; // 24
    // const int char_h = img_h / rows; // 36

    uint32_t *dst_ptr = osd_font;

    for (int ry = 0; ry < rows; ry++) {
        for (int rx = 0; rx < cols; rx++) {
            int char_start_x = rx * FONT_WIDTH;
            int char_start_y = ry * FONT_HEIGHT;

            for (int cy = 0; cy < FONT_HEIGHT; cy++) {
                int src_y = char_start_y + cy;
                int src_x = char_start_x;

                uint32_t *src_row_ptr = &raw_image[src_y * img_w + src_x];
                memcpy(dst_ptr, src_row_ptr, FONT_WIDTH * sizeof(uint32_t));
                dst_ptr += FONT_WIDTH;
            }
        }
    }

    free(raw_image);

    printf("Font init successfully! Loaded 512 characters (converted %dbpp to 32bpp XRGB).\n", bpp);
}