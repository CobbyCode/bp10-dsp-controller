#include "usb_host_ctrl.h"

bool usb_host_ctrl_select_transport(uint16_t vid, uint16_t pid,
                                    mvs_usb_transport_t *transport)
{
    if (!transport) return false;
    if (vid == 0x8888 && pid == 0x171E) {
        *transport = MVS_TRANSPORT_A800X;
        return true;
    }
    if (vid == 0x8888 && pid == 0x1719) {
        *transport = MVS_TRANSPORT_GENERIC_CLASSIC;
        return true;
    }
    *transport = (mvs_usb_transport_t){
        .kind = MVS_USB_PROFILE_NONE,
        .vid = vid,
        .pid = pid,
        .report_size = 256,
    };
    return false;
}

void mvs_usb_make_set_report_setup(const mvs_usb_transport_t *transport,
                                   mvs_usb_control_setup_t *setup)
{
    if (!transport || !setup) return;
    *setup = (mvs_usb_control_setup_t){
        .request_type = 0x21, .request = 0x09, .value = 0x0200,
        .index = transport->interface_number, .length = transport->report_size,
    };
}

void mvs_usb_make_get_report_setup(const mvs_usb_transport_t *transport,
                                   mvs_usb_control_setup_t *setup)
{
    if (!transport || !setup) return;
    *setup = (mvs_usb_control_setup_t){
        .request_type = 0xA1, .request = 0x01, .value = 0x0100,
        .index = transport->interface_number, .length = transport->report_size,
    };
}
