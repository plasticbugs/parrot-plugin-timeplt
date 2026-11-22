// tusb.h -- the simulator's stand-in for TinyUSB: a USB port with nothing
// plugged in that TinyUSB would see. The models answer for the SDK's USB
// calls (cvpad, the USB keyboard and mouse), so a file that talks to
// TinyUSB itself is left out of the build (sim/CMakeLists.txt), as
// test/host leaves it out, unless it holds the plugin's main(). Then it is
// built against this: just the names such a file uses (Pad Test's), so it
// can run and show its screen. No device ever mounts.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TUSB_DESC_DEVICE = 1, TUSB_DESC_CONFIGURATION = 2, TUSB_DESC_STRING = 3,
    TUSB_DESC_INTERFACE = 4, TUSB_DESC_ENDPOINT = 5,
};

typedef enum {
    XFER_RESULT_SUCCESS, XFER_RESULT_FAILED, XFER_RESULT_STALLED, XFER_RESULT_TIMEOUT, XFER_RESULT_INVALID,
} xfer_result_t;

typedef struct tuh_xfer_s tuh_xfer_t;
typedef void (*tuh_xfer_cb_t)(tuh_xfer_t *xfer);
struct tuh_xfer_s {
    uint8_t daddr, ep_addr;
    xfer_result_t result;
    uint32_t actual_len;
    uint8_t *buffer;
    tuh_xfer_cb_t complete_cb;
    uintptr_t user_data;
};

typedef struct __attribute__((packed)) {
    uint8_t bLength, bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces, bConfigurationValue, iConfiguration, bmAttributes, bMaxPower;
} tusb_desc_configuration_t;

static inline uint16_t tu_le16toh(uint16_t v) { return v; }   // (every machine this builds for is little-endian)

static inline bool tusb_init(void) { return true; }
static inline bool tuh_init(uint8_t rhport) { (void)rhport; return true; }
static inline void tuh_task(void) {}
static inline bool tuh_mounted(uint8_t daddr) { (void)daddr; return false; }
static inline bool tuh_vid_pid_get(uint8_t daddr, uint16_t *vid, uint16_t *pid) {
    (void)daddr;
    *vid = *pid = 0;
    return false;
}
static inline bool tuh_descriptor_get_configuration(uint8_t daddr, uint8_t index, void *buf, uint16_t len,
                                                    tuh_xfer_cb_t cb, uintptr_t user_data) {
    (void)daddr; (void)index; (void)buf; (void)len; (void)cb; (void)user_data;
    return false;
}
