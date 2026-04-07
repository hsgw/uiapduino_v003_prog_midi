#ifndef _USB_CONFIG_H
#define _USB_CONFIG_H

//Defines the number of endpoints for this device. (Always add one for EP0). For two EPs, this should be 3.
#define ENDPOINTS 3

#define USB_PORT D     // [A,C,D] GPIO Port to use with D+, D- and DPU
#define USB_PIN_DP 3   // [0-4] GPIO Number for USB D+ Pin
#define USB_PIN_DM 4   // [0-4] GPIO Number for USB D- Pin
//#define USB_PIN_DPU 5  // [0-7] GPIO for feeding the 1.5k Pull-Up on USB D- Pin; Comment out if not used / tied to 3V3!

#define RV003USB_DEBUG_TIMING      0
#define RV003USB_OPTIMIZE_FLASH    1
#define RV003USB_EVENT_DEBUGGING   0
#define RV003USB_HANDLE_IN_REQUEST 1
#define RV003USB_OTHER_CONTROL     1
#define RV003USB_HANDLE_USER_DATA  1
#define RV003USB_HID_FEATURES      1
#define RV003USB_USER_DATA_HANDLES_TOKEN 1

#ifndef __ASSEMBLER__

#include <tinyusb_hid.h>
#include <tusb_types.h>

struct descriptor_list_struct {
	uint32_t	lIndexValue;
	const uint8_t	*addr;
	uint8_t		length;
};

extern const struct descriptor_list_struct * active_descriptor_list;
extern uint8_t active_descriptor_list_entries;

void set_usb_mode_programmer();
void set_usb_mode_midi();

#define descriptor_list active_descriptor_list
#define DESCRIPTOR_LIST_ENTRIES active_descriptor_list_entries

#ifdef INSTANCE_DESCRIPTORS

// Programmer Descriptors
static const uint8_t programmer_device_descriptor[] = {
	18, //Length
	1,  //Type (Device)
	0x10, 0x01, //Spec
	0x0, //Device Class
	0x0, //Device Subclass
	0x0, //Device Protocol  (000 = use config descriptor)
	0x08, //Max packet size for EP0
	0x06, 0x12, //ID Vendor
	0x10, 0x5d, //ID Product
	0x02, 0x00, //ID Rev
	1, //Manufacturer string
	2, //Product string
	3, //Serial string
	1, //Max number of configurations
};

static const uint8_t programmer_special_hid_desc[] = { 
	HID_USAGE_PAGE ( 0xff ), // Vendor-defined page.
	HID_USAGE      ( 0x00 ),
	HID_REPORT_SIZE ( 8 ),
	HID_COLLECTION ( HID_COLLECTION_LOGICAL ),
		HID_REPORT_COUNT   ( 7 ),
		HID_REPORT_ID      ( 0xaa )
		HID_USAGE          ( 0x01 ),
		HID_FEATURE        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),
		HID_REPORT_COUNT   ( 63 ),
		HID_REPORT_ID      ( 0xab )
		HID_USAGE          ( 0x01 ),
		HID_FEATURE        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),
		HID_REPORT_COUNT   ( 127 ),
		HID_REPORT_ID      ( 0xac )
		HID_USAGE          ( 0x01 ),
		HID_FEATURE        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),
		HID_REPORT_COUNT   ( 78 ),
		HID_REPORT_ID      ( 0xad )
		HID_USAGE          ( 0x01 ),
		HID_FEATURE        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),
    HID_REPORT_COUNT_N ( 263, 2 ),
		HID_REPORT_ID      ( 0xae )
		HID_USAGE          ( 0x01 ),
		HID_FEATURE        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),
	HID_COLLECTION_END,
};

static const uint8_t programmer_config_descriptor[] = {
	9, 					// bLength;
	2,					// bDescriptorType;
	0x22, 0x00,			// wTotalLength  	
	0x01,					// bNumInterfaces
	0x01,					// bConfigurationValue
	0x00,					// iConfiguration
	0x80,					// bmAttributes
	0x64,					// bMaxPower (200mA)

	9,					// bLength
	4,					// bDescriptorType
	0,		            // bInterfaceNumber
	0,					// bAlternateSetting
	1,					// bNumEndpoints
	0x03,				// bInterfaceClass (0x03 = HID)
	0x00,				// bInterfaceSubClass
	0xff,				// bInterfaceProtocol
	0,					// iInterface

	9,					// bLength
	0x21,					// bDescriptorType (HID)
	0x10,0x01,	  // bcd 1.1
	0x00,         //country code
	0x01,         // Num descriptors
	0x22,         // DescriptorType[0] (HID)
	sizeof(programmer_special_hid_desc), 0x00, 

	7,            // endpoint descriptor
	0x05,         // Endpoint Descriptor
	0x81,         // Endpoint Address
	0x03,         // Attributes
	0x01,	0x00, // Size
	100,          // Interval
};

// MIDI Descriptors
static const uint8_t midi_device_descriptor[] = {
	18, //Length
	TUSB_DESC_DEVICE,  //Type (Device)
	0x10, 0x01, //Spec
	0x0, //Device Class
	0x0, //Device Subclass
	0x0, //Device Protocol
	0x08, //Max packet size for EP0
	0x09, 0x12, //ID Vendor
	0x03, 0xe0, //ID Product
	0x02, 0x00, //ID Rev
	1, //Manufacturer string
	2, //Product string
	3, //Serial string
	1, //Max number of configurations
};

static const uint8_t midi_config_descriptor[] = {
	9,                        // bLength;
	TUSB_DESC_CONFIGURATION,  // bDescriptorType;
	101, 0x00,                // wTotalLength
	0x02,                     // bNumInterfaces
	0x01,                     // bConfigurationValue
	0x00,                     // iConfiguration
	0x80,                     // bmAttributes
	0x64,                     // bMaxPower (200mA)

	9,                        // bLength
	TUSB_DESC_INTERFACE,      // bDescriptorType
	0,                        // bInterfaceNumber
	0,                        // bAlternateSetting
	0,                        // bNumEndpoints
	TUSB_CLASS_AUDIO,         // bInterfaceClass
	1,                        // bInterfaceSubClass
	0,                        // bInterfaceProtocol
	0,                        // iInterface

	9,                        // bLength
	TUSB_DESC_CS_INTERFACE,   // descriptor type
	1,                        // header functional descriptor
	0x0, 0x01,                // bcdADC
	9, 0,                     // wTotalLength
	1,                        // bInCollection
	1,                        // MIDIStreaming interface 1

	9,                        // bLength
	TUSB_DESC_INTERFACE,
	1,                        // Index of this interface.
	0,                        // Index of this alternate setting.
	2,                        // bNumEndpoints = 2
	1,                        // AUDIO
	3,                        // MIDISTREAMING
	0,                        // Unused bInterfaceProtocol
	0,                        // Unused string index

	7,                        // bLength
	TUSB_DESC_CS_INTERFACE,   // CS_INTERFACE descriptor
	1,                        // MS_HEADER subtype.
	0x0, 0x01,                // Revision
	0x41, 0,                  // Total size

	6,                        // bLength
    TUSB_DESC_CS_INTERFACE,   // CS_INTERFACE descriptor
	0x02,                     // bDescriptorSubtype = MIDI_IN_JACK
	0x01,                     // bJackType = EMBEDDED
	0x01,                     // bJackID
	0x00,                     // iJack

	6,                        // bLength
    TUSB_DESC_CS_INTERFACE,   // CS_INTERFACE descriptor
	0x02,                     // bDescriptorSubtype = MIDI_IN_JACK
	0x02,                     // bJackType = EXTERNAL
	0x02,                     // bJackID
	0x00,                     // iJack

	9,                        // bLength
    TUSB_DESC_CS_INTERFACE,   // CS_INTERFACE descriptor
	0x03,                     // bDescriptorSubtype = MIDI_OUT_JACK
	0x01,                     // bJackType = EMBEDDED
	0x03,                     // bJackID
	0x01,                     // bNrPins = 1
	0x02,                     // BaSourceID
	0x01,                     // BaSourcePin
	0x00,                     // iJack

	9,                        // bLength
    TUSB_DESC_CS_INTERFACE,   // CS_INTERFACE descriptor
	0x03,                     // bDescriptorSubtype = MIDI_OUT_JACK
	0x02,                     // bJackType = EXTERNAL
	0x04,                     // bJackID
	0x01,                     // bNrPins = 1
	0x01,                     // BaSourceID
	0x01,                     // BaSourcePin
	0x00,                     // iJack

	9,                  // bLength
	TUSB_DESC_ENDPOINT,
	0x1,                // bEndpointAddress
	3,                  // bmAttributes = Interrupt
	8, 0,               // wMaxPacketSize 
	1,                  // bIntervall
	0,                  // bRefresh
	0,                  // bSynchAddress

	5,                  // bLength
	TUSB_DESC_CS_ENDPOINT,
	1,                  // bDescriptorSubtype = MS_GENERAL
	1,                  // bNumEmbMIDIJack
	1,                  // BaAssocJackID = 1

	9,                  // bLength
	TUSB_DESC_ENDPOINT,
	0x81,               // bEndpointAddress
	3,                  // bmAttributes: 2: Bulk, 3: Interrupt endpoint
	8, 0,               // wMaxPacketSize
	1,                  // bIntervall in ms
	0,                  // bRefresh
	0,                  // bSyncAddress

	5,                  // bLength
	TUSB_DESC_CS_ENDPOINT,
	1,                  // bDescriptorSubtype
	1,                  // bNumEmbMIDIJack
	3,                  // baAssocJackID (0)
};

// Common Strings
#define STR_MANUFACTURER u"CNLohr"
#define STR_PRODUCT_PROG u"RV003 RVSWDIO Programmer"
#define STR_PRODUCT_MIDI u"RV003USB Example MIDI Device"
#ifndef STR_SERIAL
#define STR_SERIAL       u"RVSWDIO003-01"
#endif

struct usb_string_descriptor_struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wString[];
};
const static struct usb_string_descriptor_struct string0 __attribute__((section(".rodata"))) = { 4, 3, {0x0409} };
const static struct usb_string_descriptor_struct string1 __attribute__((section(".rodata")))  = { sizeof(STR_MANUFACTURER), 3, STR_MANUFACTURER };
const static struct usb_string_descriptor_struct string2_prog __attribute__((section(".rodata")))  = { sizeof(STR_PRODUCT_PROG), 3, STR_PRODUCT_PROG };
const static struct usb_string_descriptor_struct string2_midi __attribute__((section(".rodata")))  = { sizeof(STR_PRODUCT_MIDI), 3, STR_PRODUCT_MIDI };
const static struct usb_string_descriptor_struct string3 __attribute__((section(".rodata")))  = { sizeof(STR_SERIAL), 3, STR_SERIAL };

// Descriptor Lists
const static struct descriptor_list_struct descriptor_list_programmer[] = {
	{0x00000100, programmer_device_descriptor, sizeof(programmer_device_descriptor)},
	{0x00000200, programmer_config_descriptor, sizeof(programmer_config_descriptor)},
	{0x00002200, programmer_special_hid_desc, sizeof(programmer_special_hid_desc)},
	{0x00002100, programmer_config_descriptor + 18, 9 },
	{0x00000300, (const uint8_t *)&string0, 4},
	{0x04090301, (const uint8_t *)&string1, sizeof(STR_MANUFACTURER)},
	{0x04090302, (const uint8_t *)&string2_prog, sizeof(STR_PRODUCT_PROG)},	
	{0x04090303, (const uint8_t *)&string3, sizeof(STR_SERIAL)}
};

const static struct descriptor_list_struct descriptor_list_midi[] = {
	{0x00000100, midi_device_descriptor, sizeof(midi_device_descriptor)},
	{0x00000200, midi_config_descriptor, sizeof(midi_config_descriptor)},
	{0x00000300, (const uint8_t *)&string0, 4},
	{0x04090301, (const uint8_t *)&string1, sizeof(STR_MANUFACTURER)},
	{0x04090302, (const uint8_t *)&string2_midi, sizeof(STR_PRODUCT_MIDI)},	
	{0x04090303, (const uint8_t *)&string3, sizeof(STR_SERIAL)}
};

// Global pointers to be set at startup
const struct descriptor_list_struct * active_descriptor_list;
uint8_t active_descriptor_list_entries;

void set_usb_mode_programmer() {
	active_descriptor_list = descriptor_list_programmer;
	active_descriptor_list_entries = sizeof(descriptor_list_programmer)/sizeof(struct descriptor_list_struct);
}

void set_usb_mode_midi() {
	active_descriptor_list = descriptor_list_midi;
	active_descriptor_list_entries = sizeof(descriptor_list_midi)/sizeof(struct descriptor_list_struct);
}

#endif // INSTANCE_DESCRIPTORS

#endif // __ASSEMBLER__

#endif 
