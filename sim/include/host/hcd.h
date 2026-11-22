// host/hcd.h -- the simulator's stand-in for TinyUSB's host controller
// header (see tusb.h here): the event numbers tuh_event_hook_cb() sees.
#pragma once
enum { HCD_EVENT_DEVICE_ATTACH, HCD_EVENT_DEVICE_REMOVE, HCD_EVENT_XFER_COMPLETE };
