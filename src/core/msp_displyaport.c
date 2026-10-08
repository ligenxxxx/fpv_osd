
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "driver/font.h"
#include "driver/uart.h"
#include "msp_displayport.h"
#include "msp_protocol.h"
#include "osd.h"

typedef enum {
    SUBCMD_HEARTBEAT,
    SUBCMD_RELEASE,
    SUBCMD_CLEAR,
    SUBCMD_WRITE,
    SUBCMD_DRAW,
    SUBCMD_CONFIG,
} displayport_subcmd_e;

typedef enum {
    MSP_HEADER_START,
    MSP_HEADER_M,
    MSP_PACKAGE_REPLAY1,
    MSP_LENGTH,
    MSP_CMD,
    MSP_RX1,
    MSP_CRC1,
    MSP_HEADER_M2,
    MSP_PACKAGE_REPLAY2,
    MSP_ZERO,
    MSP_CMD_L,
    MSP_CMD_H,
    MSP_LEN_L,
    MSP_LEN_H,
    MSP_RX2,
    MSP_CRC2,
} msp_rx_status_e;

typedef enum {
    FC_LOCK_VARIANT = 1 << 0,
    FC_LOCK_OSD_CANVAS = 1 << 1,
} fc_lock_e;

static const uint8_t crc8tab[256] = {
    0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54, 0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
    0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06, 0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
    0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0, 0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
    0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2, 0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
    0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9, 0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
    0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B, 0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
    0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D, 0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
    0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F, 0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
    0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB, 0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
    0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9, 0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
    0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F, 0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
    0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D, 0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
    0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26, 0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
    0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74, 0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
    0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82, 0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
    0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0, 0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9};

static int fd = 0;
static unsigned char fc_variant[5] = "    ";
static uint8_t fc_osd_canvas_cols = 0;
static uint8_t fc_osd_canvas_rows = 0;
static uint8_t fc_lock = 0;

#define msp_tx(a)             uart_write_byte(fd, a)
#define msp_tx_buf(tbuf, len) uart_write(fd, tbuf, len)

static void msp_send_cmd_tx(uint8_t cmd) {
    uint8_t tbuf[8] = {0};
    const uint8_t len = 0;
    const uint8_t crc = cmd;

    tbuf[0] = MSP_HEADER_FRAMER;
    tbuf[1] = MSP_HEADER_V1;
    tbuf[2] = MSP_HEADER_COMMAND;
    tbuf[3] = len;
    tbuf[4] = cmd;
    tbuf[5] = crc;

    msp_tx_buf(tbuf, 6);

    printf("msp send cmd: %d\n", cmd);
}

static uint8_t msp_calc_crc(uint8_t *tbuf) {
    uint8_t crc = 0;
    uint8_t len = tbuf[3];
    for (int i = 0; i < len + 2; i++) {
        crc ^= tbuf[3 + i];
    }

    return crc;
}

static void msp_set_osd_canvas(uint8_t cols, uint8_t rows) {
    uint8_t tbuf[8] = {0};
    const uint8_t len = 2;
    uint8_t crc = 0;

    tbuf[0] = MSP_HEADER_FRAMER;
    tbuf[1] = MSP_HEADER_V1;
    tbuf[2] = MSP_HEADER_COMMAND;
    tbuf[3] = len;
    tbuf[4] = MSP_SET_OSD_CANVAS;
    tbuf[5] = cols;
    tbuf[6] = rows;
    tbuf[7] = msp_calc_crc(tbuf);

    msp_tx_buf(tbuf, 8);
    printf("msp send osd canvas: %dx%d\n", cols, rows);
}

static void msp_config_fc() {
    static uint8_t state = 0;

    switch (state) {
    case 0: // get fc_variant
        msp_send_cmd_tx(MSP_FC_VARIANT);
        state = 1;
        break;

    case 1: // wait fc_variant and set/get osd canvas
        if (fc_lock & FC_LOCK_VARIANT) {
            if (strcmp(fc_variant, "BTFL") == 0) {
                msp_set_osd_canvas(OSD_HD_COLS, OSD_HD_ROWS);
                msp_send_cmd_tx(MSP_OSD_CANVAS);
                state = 2;
            }

            // load font
            char font_path[256];
            snprintf(font_path, sizeof(font_path), "/tmp/%c%c%c%c.bmp", fc_variant[0], fc_variant[1], fc_variant[2], fc_variant[3]);
            font_init(font_path);
            // printf("pixel:0x%08X\n", osd_font[0]);
        }
        break;
    case 2:
        if (fc_lock & FC_LOCK_OSD_CANVAS) {
        }
        break;
    default:
        break;
    }
}

static void msp_parse_fc_variant(uint8_t *payload) {
    uint8_t is_v2 = payload[0];
    uint8_t len = payload[1];
    uint8_t cmd = payload[2];

    if (is_v2)
        return;

    if (cmd != MSP_FC_VARIANT)
        return;

    for (int i = 0; i < 4; i++) {
        fc_variant[i] = payload[3 + i];
    }
    fc_lock |= FC_LOCK_VARIANT;
    printf("> FC VARIANT: %s\n", fc_variant);
}

static void msp_parse_osd_canvas(uint8_t *payload) {
    uint8_t is_v2 = payload[0];
    uint8_t len = payload[1];
    uint8_t cmd = payload[2];

    if (is_v2)
        return;

    if (cmd != MSP_OSD_CANVAS)
        return;

    fc_osd_canvas_cols = payload[3];
    fc_osd_canvas_rows = payload[4];

    fc_lock |= FC_LOCK_OSD_CANVAS;
    printf("> FC OSD CANVAS: %dx%d\n", fc_osd_canvas_cols, fc_osd_canvas_rows);
}

static void msp_parse_displayport(uint8_t *payload) {
    uint8_t is_v2 = payload[0];
    uint8_t len = payload[1];
    uint8_t cmd = payload[2];
    displayport_subcmd_e subcmd = payload[3];
    uint8_t row = payload[4];
    uint8_t col = payload[5];
    uint8_t attr = payload[6];

    if (is_v2)
        return;

    if (cmd != MSP_DISPLAYPORT)
        return;

    switch (subcmd) {
    case SUBCMD_HEARTBEAT:
    case SUBCMD_RELEASE:
        break;

    case SUBCMD_CLEAR:
        osd_clear();
        printf("C");
        break;

    case SUBCMD_WRITE:
        uint8_t osd_len = len - 4;
        if (osd_len != 0) {
            osd_write(row, col, &payload[7], osd_len);
            printf("W");
        }
        break;

    case SUBCMD_DRAW:
        osd_draw();
        printf("D\n");
        break;

    case SUBCMD_CONFIG:
        uint8_t font = payload[1];
        uint8_t resolution = payload[2];
        printf("config:%d,%d\n", font, resolution);
        break;
    }
}

static uint8_t msp_receive_package(uint8_t rx, uint8_t *payload) {
    static msp_rx_status_e state = MSP_HEADER_START;
    static uint16_t len;
    static uint8_t ptr; // write ptr of payload
    static uint8_t crc8;
    uint8_t ret;

    switch (state) {
    case MSP_HEADER_START:
        if (rx == MSP_HEADER_FRAMER) {
            ptr = 0;
            state = MSP_HEADER_M;
        }
        break;

    case MSP_HEADER_M:
        if (rx == MSP_HEADER_V1) {
            state = MSP_PACKAGE_REPLAY1;
            payload[ptr++] = 0; // is_V2
        } else if (rx == MSP_HEADER_V2) {
            payload[ptr++] = 1; // is_V2
            state = MSP_PACKAGE_REPLAY2;
        } else {
            state = MSP_HEADER_START;
        }
        break;

    case MSP_PACKAGE_REPLAY1: // 0x3E
        if (rx == MSP_HEADER_RESPONSE) {
            state = MSP_LENGTH;
        } else {
            state = MSP_HEADER_START;
        }
        break;

    case MSP_LENGTH:
        payload[ptr++] = rx; // len
        len = rx;
        crc8 = rx;
        state = MSP_CMD;
        len = rx;
        break;

    case MSP_CMD:
        payload[ptr++] = rx; // cmd
        crc8 ^= rx;
        if (len == 0)
            state = MSP_CRC1;
        else
            state = MSP_RX1;
        break;

    case MSP_RX1:
        crc8 ^= rx;
        payload[ptr++] = rx;
        ptr &= 255;
        len--;
        if (len == 0)
            state = MSP_CRC1;
        break;

    case MSP_CRC1:
        if (rx == crc8) {
            ret = 1;
        }
        state = MSP_HEADER_START;
        break;

    case MSP_PACKAGE_REPLAY2: // 0x3E
        if (rx == MSP_HEADER_RESPONSE) {
            state = MSP_ZERO;
        } else {
            state = MSP_HEADER_START;
        }
        break;

    case MSP_ZERO:          // 0x00
        crc8 = crc8tab[rx]; // 0 ^ rx = rx
        if (rx == 0x00) {
            state = MSP_CMD_L;
        } else {
            state = MSP_HEADER_START;
        }
        break;

    case MSP_CMD_L:
        payload[ptr++] = rx; // cmd_l
        crc8 = crc8tab[crc8 ^ rx];
        state = MSP_CMD_H;
        break;

    case MSP_CMD_H:
        payload[ptr++] = rx; // cmd_h
        crc8 = crc8tab[crc8 ^ rx];
        state = MSP_LEN_L;
        break;

    case MSP_LEN_L:
        payload[ptr++] = rx; // len_l
        crc8 = crc8tab[crc8 ^ rx];
        len = rx;
        state = MSP_LEN_H;
        break;

    case MSP_LEN_H:
        payload[ptr++] = rx; // len_h
        len += ((uint16_t)rx << 8);
        crc8 = crc8tab[crc8 ^ rx];
        if (len == 0) {
            state = MSP_CRC2;
        } else {
            ptr = 0;
            state = MSP_RX2;
        }
        break;

    case MSP_RX2:
        payload[ptr++] = rx;
        ptr &= 255;
        crc8 = crc8tab[crc8 ^ rx];
        len--;
        if (len == 0) {
            state = MSP_CRC2;
        }
        break;

    case MSP_CRC2:
        if (crc8 == rx) {
            ret = 1;
        }
        state = MSP_HEADER_START;
        break;

    default:
        state = MSP_HEADER_START;
        break;

    } // switch(state)

    return ret;
}

void *pthread_msp_displayport(void *arg) {
    uint8_t uart_rbuf[256];
    uint8_t payload[256];

    fd = uart_open();
    if (fd < 0) {
        uart_close(fd);
        return NULL;
    }
    printf("msp uart is opened: 0x%08x\n", fd);

    osd_init();

    while (1) {
        int len = read(fd, uart_rbuf, sizeof(uart_rbuf));

        if (len <= 0) {
            usleep(1000);
            msp_config_fc();
            continue;
        }

        for (int i = 0; i < len; i++) {
            if (msp_receive_package(uart_rbuf[i], payload)) {
                msp_parse_displayport(payload);
                msp_parse_fc_variant(payload);
                msp_parse_osd_canvas(payload);
            }
        }
    }

    uart_close(fd);
    return NULL;
}
