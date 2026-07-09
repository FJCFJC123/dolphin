// OG Xbox port: stub replacement for the prebuilt libusb-1.0.lib, which was
// compiled against the VS2010 CRT (___iob_func) and cannot link with the
// modern UCRT. Only the Wii /dev/usb/hid device (WII_IPC_HLE_Device_hid.cpp)
// uses libusb; GameCube titles never touch it and the Xbox target has no
// libusb at all. Every entry point reports "no devices / not supported".

#include <stdlib.h>
#include <string.h>
#include <libusb/include/libusb.h>

extern "C" {

int LIBUSB_CALL libusb_init(libusb_context** ctx)
{
	if (ctx)
		*ctx = NULL;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}

void LIBUSB_CALL libusb_exit(libusb_context*) {}

ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context*, libusb_device*** list)
{
	if (list)
		*list = NULL;
	return 0; // no devices
}

void LIBUSB_CALL libusb_free_device_list(libusb_device**, int) {}

int LIBUSB_CALL libusb_get_device_descriptor(libusb_device*, libusb_device_descriptor* desc)
{
	if (desc)
		memset(desc, 0, sizeof(*desc));
	return LIBUSB_ERROR_NOT_SUPPORTED;
}

int LIBUSB_CALL libusb_get_config_descriptor(libusb_device*, uint8_t, libusb_config_descriptor** config)
{
	if (config)
		*config = NULL;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}

void LIBUSB_CALL libusb_free_config_descriptor(libusb_config_descriptor*) {}

uint8_t LIBUSB_CALL libusb_get_bus_number(libusb_device*) { return 0; }
uint8_t LIBUSB_CALL libusb_get_device_address(libusb_device*) { return 0; }

int LIBUSB_CALL libusb_open(libusb_device*, libusb_device_handle** handle)
{
	if (handle)
		*handle = NULL;
	return LIBUSB_ERROR_NOT_SUPPORTED;
}

void LIBUSB_CALL libusb_close(libusb_device_handle*) {}

int LIBUSB_CALL libusb_claim_interface(libusb_device_handle*, int) { return LIBUSB_ERROR_NOT_SUPPORTED; }
int LIBUSB_CALL libusb_kernel_driver_active(libusb_device_handle*, int) { return 0; }
int LIBUSB_CALL libusb_detach_kernel_driver(libusb_device_handle*, int) { return LIBUSB_ERROR_NOT_SUPPORTED; }

struct libusb_transfer* LIBUSB_CALL libusb_alloc_transfer(int iso_packets)
{
	// Return a real allocation so the libusb_fill_* header inlines can write
	// into it even if a caller ignores our error returns.
	size_t size = sizeof(libusb_transfer) +
	              (size_t)iso_packets * sizeof(libusb_iso_packet_descriptor);
	return (libusb_transfer*)calloc(1, size);
}

int LIBUSB_CALL libusb_submit_transfer(libusb_transfer*) { return LIBUSB_ERROR_NOT_SUPPORTED; }

int LIBUSB_CALL libusb_handle_events_timeout(libusb_context*, timeval*)
{
	return LIBUSB_ERROR_NOT_SUPPORTED;
}

} // extern "C"
