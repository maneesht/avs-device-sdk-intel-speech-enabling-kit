/**
 * AlsaHardwareController.cpp
 *
 * TODO: Add Intel copyright
 */

#include <AVSCommon/Utils/Logger/Logger.h>

#include "AlsaController/AlsaHardwareController.h"
#include <string>

namespace alexaClientSDK {
namespace kwd {

// Logging tag
static const std::string TAG("AlsaHardwareController");
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

// ALSA control device name for receving keyphrase detection events
#define CTL_DETECT_NAME "KP Detect Control"
#define CTL_CAP_STREAM_MODE "Capture Stream mode"
#define CTL_DSP_TOPO "DSP Load Topology Control"

// DSP unload/load values
#define DSP_UNLOAD 0
#define DSP_LOAD   1

// Multi-Turn modes
#define KP_WAKE_ON_VOICE     0
#define KP_CAPTURE_STREAMING 1
#define DEBUG

#ifdef DEBUG
# define debug_print(...) fprintf(__VA_ARGS__)
#else
# define debug_print(...) do {} while (0)
#endif

static char *card = "0";

static snd_ctl_t *ctl_handle;
static snd_output_t *snd_output;

static snd_ctl_elem_info_t *ctl_info;
static snd_ctl_elem_id_t *ctl_id;

static char *ctl_name;
static unsigned int *ctl_value_buffer;

char errmsg[256];

#define SND_CTL_READONLY                0x0004
#define TLV_DUMMY_TAG			0

static void err_exit(const char *err)
{
	fprintf(stderr, "Error (%s)\n%s", card, err);
	if (ctl_handle)
		snd_ctl_close(ctl_handle);
	if (ctl_value_buffer)
		free(ctl_value_buffer);
	exit(-1);
}

ESPData s1000_dump_bytes(const void *b, int size)
{
	int i = 0;
	const unsigned int *bytes = b;

	debug_print(stdout, "\n%.4x: ", i);
	debug_print(stdout, "0x%.8x ", bytes[i]);
	debug_print(stdout, "0x%.8x ", bytes[i+1]);
    unsigned int ambient = bytes[2]; //may get an indexOutOfBounds/SegFault
    unsigned int voice = bytes[3];
    return new ESPData(std::to_string(voice), std::to_string(ambient));
}

static int ctl_is_bytes_tlv(void)
{
	if (!snd_ctl_elem_info_is_tlv_readable(ctl_info) ||
	!snd_ctl_elem_info_is_tlv_writable(ctl_info) ||
	(snd_ctl_elem_info_get_type(ctl_info) != SND_CTL_ELEM_TYPE_BYTES))
		return 0;
	return 1;
}

static int s1000_print_event(int card, snd_ctl_t *ctl)
{
	snd_ctl_event_t *event;
	unsigned int mask;
	int err;

	snd_ctl_event_alloca(&event);
	err = snd_ctl_read(ctl, event);
	if (err < 0)
		return err;

	if (snd_ctl_event_get_type(event) !=
	    SND_CTL_EVENT_ELEM)
		return 0;

	if (card >= 0)
		printf("card %d, ", card);
		printf("#%d (%i,%i,%i,%s,%i)",
	       	snd_ctl_event_elem_get_numid(event),
	       	snd_ctl_event_elem_get_interface(event),
	       	snd_ctl_event_elem_get_device(event),
	       	snd_ctl_event_elem_get_subdevice(event),
	       	snd_ctl_event_elem_get_name(event),
	       	snd_ctl_event_elem_get_index(event));

	mask = snd_ctl_event_elem_get_mask(event);
	if (mask == SND_CTL_EVENT_MASK_REMOVE) {
		printf(" REMOVE\n");
		return 0;
	}

	if (mask & SND_CTL_EVENT_MASK_VALUE)
		printf(" VALUE");
	if (mask & SND_CTL_EVENT_MASK_INFO)
		printf(" INFO");
	if (mask & SND_CTL_EVENT_MASK_ADD)
		printf(" ADD");
	if (mask & SND_CTL_EVENT_MASK_TLV)
		printf(" TLV");
	printf("\n");
	return 0;
}

static ESPData s1000_tlv_monitor(void)
{
	int err = 0;
	int read_size = snd_ctl_elem_info_get_count(ctl_info);
	unsigned int *read_tlv;
	unsigned short revents;
	struct pollfd fds[1];

	err = snd_ctl_subscribe_events(ctl_handle, 1);
	if (err < 0) {
		fprintf(stderr, "Cannot open subscribe events to ctl\n");
		snd_ctl_close(ctl_handle);
	}

	snd_ctl_poll_descriptors(ctl_handle, &fds[0], 1);

poll:
	printf("KP notification polling\n");
	err = poll(fds, 1, -1);
	if (err <= 0) {
		err = 0;
		sprintf(errmsg, "kp notify: poll failed");
		printf("kp notify: poll failed\n");
	}

	snd_ctl_poll_descriptors_revents(ctl_handle, &fds[0], 1, &revents);

	if (revents & POLLIN) {
		printf("notification detected\n");
		s1000_print_event(0, ctl_handle);
	}

	if (!ctl_is_bytes_tlv()) {
		sprintf(errmsg, "set tlv: not a bytes tlv control");
		err_exit(errmsg);
	}

	read_tlv = calloc(1, read_size+8);

	debug_print(stdout, "\nTLV READ - %d bytes", read_size);
	err = snd_ctl_elem_tlv_read(ctl_handle, ctl_id, read_tlv, read_size);
	if (err) {
		sprintf(errmsg, "ctl tlv read: %s", strerror(err));
		err_exit(errmsg);
	}
	auto esp_data = s1000_dump_bytes(read_tlv, read_size);
	//goto poll; /* infinite loop to poll device, most likely not needed*/

	free(read_tlv);
    return esp_data;
}

static void s1000_setup_mixer_ctl(char *idstr)
{
	int err;

	err = snd_ctl_ascii_elem_id_parse(ctl_id, idstr);
	if (err) {
		sprintf(errmsg, "parse id err: %s", strerror(err));
		err_exit(errmsg);
	}

	snd_ctl_elem_info_set_id(ctl_info, ctl_id);
	err = snd_ctl_elem_info(ctl_handle, ctl_info);
	if (err) {
		sprintf(errmsg, "ctl info: %s", strerror(err));
		err_exit(errmsg);
	}

}
std::shared_ptr<AlsaHardwareController> AlsaHardwareController::create(
        std::string name, std::string keyword) 
{
    std::shared_ptr<AlsaHardwareController> ctrl = std::shared_ptr<AlsaHardwareController>(
            new AlsaHardwareController(name, keyword));

    if(!ctrl->init()) {
        ACSDK_ERROR(LX("createFailed").d("reason", "initHardwareControllerFailed"));
        return nullptr;
    }

    return ctrl;
}

std::unique_ptr<KeywordDetection> AlsaHardwareController::read(
        std::chrono::milliseconds timeout) { //this is where the data for the keyword gets loaded
    // Get the file descriptor
    struct pollfd fds[1];
    snd_ctl_poll_descriptors(m_ctl, &fds[0], 1);

    int ret = poll(fds, 1, timeout.count());
    
    if(ret == 0) {
        // 0 indicates a timeout
        return nullptr;
    } else if(ret < 0) {
        // < 0 indicates an error occurred
        ACSDK_ERROR(LX("readFailed")
                .d("reason", "pollFailed")
                .d("error_code", snd_strerror(ret)));
        return nullptr;
    }

    snd_ctl_event_t* event;
    snd_ctl_event_alloca(&event);
    if(snd_ctl_read(m_ctl, event) < 0) {
        ACSDK_ERROR(LX("readFailed")
                .d("reason", "sndReadFailed")
                .d("error_code", snd_strerror(ret)));
        return nullptr;
    }

    // Get reference to the KP detect control device
    snd_ctl_elem_value_t* control;
    snd_ctl_elem_value_alloca(&control);
    snd_ctl_elem_value_set_interface(control, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_value_set_name(control, CTL_DETECT_NAME);
    snd_ctl_elem_value_set_index(control, 0);
    
    if((ret = snd_ctl_elem_read(m_ctl, control)) != 0) {
        ACSDK_ERROR(LX("readFailed")
                .d("reason", "sndCtrlElemReadFailed")
                .d("error_code", snd_strerror(ret)));
        return nullptr;
    }

    uint32_t value = snd_ctl_elem_value_get_integer(control, 0);
    uint16_t* payload = reinterpret_cast<uint16_t*>(&value);
    
    //start grabbing the information from the TLV array and decode to get the ESP
    //Also, change the detection type to expose the ESP

    ACSDK_DEBUG(LX("read")
            .d("event", "keywordDetection")
            .d("begin", payload[0])
            .d("end", payload[1]));

	char dev_name[128];

	card = "0";
	ctl_name = "name=KP Detect Control";
	snprintf(dev_name, 127, "hw:%i", snd_card_get_index(card));
	dev_name[127] = '\0';
	snd_output_stdio_attach(&snd_output, stdout, 0);

	err = snd_ctl_open(&ctl_handle, dev_name, 0);
	if (err) {
		sprintf(errmsg, "control open: %s", strerror(err));
		err_exit(errmsg);
	}

	snd_ctl_elem_info_alloca(&ctl_info);
	snd_ctl_elem_id_alloca(&ctl_id);

	s1000_setup_mixer_ctl(ctl_name);
	auto esp_data = s1000_tlv_monitor();
    auto detection = KeywordDetection::create(payload[0], payload[1], m_keyword, esp_data);
    return detection;
}

void AlsaHardwareController::onStateChanged(AipState state) {
    if(state == AipState::EXPECTING_SPEECH) {
        ACSDK_DEBUG(LX("onStateChanged").d("event", "setCaptureStremingMode"));
        int ret = 0;
        // Create handle to the streaming mode control device 
        snd_ctl_elem_value_t* control;
        snd_ctl_elem_value_alloca(&control);
        snd_ctl_elem_value_set_interface(control, SND_CTL_ELEM_IFACE_MIXER);
        snd_ctl_elem_value_set_name(control, CTL_CAP_STREAM_MODE);
        snd_ctl_elem_value_set_index(control, 0);
        // Set mode to streaming mode
        snd_ctl_elem_value_set_integer(control, 0, KP_CAPTURE_STREAMING);
        if((ret = snd_ctl_elem_write(m_ctl, control)) != 0) {
            ACSDK_ERROR(LX("writeFailed")
                    .d("reason", "setCaptureStreamModeFailed")
                    .d("error_code", snd_strerror(ret)));
        }
        m_isWakeOnVoice = false;
    }
}

void AlsaHardwareController::onDialogUXStateChanged(DialogUXState state) {
    if(state == DialogUXState::IDLE && !m_isWakeOnVoice) {
        ACSDK_DEBUG(LX("onDialogUXStateChanged").d("event", "setWakeOnVoiceMode"));
        int ret = 0;
        // Create handle to the streaming mode control device 
        snd_ctl_elem_value_t* control;
        snd_ctl_elem_value_alloca(&control);
        snd_ctl_elem_value_set_interface(control, SND_CTL_ELEM_IFACE_MIXER);
        snd_ctl_elem_value_set_name(control, CTL_CAP_STREAM_MODE);
        snd_ctl_elem_value_set_index(control, 0);
        // Set mode to streaming mode
        snd_ctl_elem_value_set_integer(control, 0, KP_WAKE_ON_VOICE);
        if((ret = snd_ctl_elem_write(m_ctl, control)) != 0) {
            ACSDK_ERROR(LX("writeFailed")
                    .d("reason", "setWakeOnVoiceModeFailed")
                    .d("error_code", snd_strerror(ret)));
        }
        m_isWakeOnVoice = true;
    }
}

AlsaHardwareController::AlsaHardwareController(std::string name, std::string keyword) :
    m_name(name), m_keyword(keyword), m_ctl(NULL), m_isWakeOnVoice(false)
{}

AlsaHardwareController::~AlsaHardwareController() {
    // Note: snd_ctl_close both closes the connection and frees all of its
    // allocated resources (see ALSA docs for more info)
    if(m_ctl != NULL) {
        snd_ctl_close(m_ctl);
    }
}

bool AlsaHardwareController::init() {
    int ret = 0;

    if((ret = snd_ctl_open(&m_ctl, m_name.c_str(), SND_CTL_READONLY)) < 0) {
        ACSDK_ERROR(LX("initFailed")
                .d("reason", "openAlsaCtrlFailed")
                .d("error_code", snd_strerror(ret)));
        return false;
    }

    // Note: 1 tells the API to subscribe
    if((ret = snd_ctl_subscribe_events(m_ctl, 1)) < 0) {
        ACSDK_ERROR(LX("initFailed")
                .d("reason", "alsaCtrlSubscribeFailed")
                .d("error_code", snd_strerror(ret)));
        return false;
    }

    // Create handle to the DSP topology control device
    snd_ctl_elem_value_t* control;
    snd_ctl_elem_value_alloca(&control);
    snd_ctl_elem_value_set_interface(control, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_value_set_name(control, CTL_DSP_TOPO);
    snd_ctl_elem_value_set_index(control, 0);
    
    // Unload the DSP's topology
    ACSDK_DEBUG(LX("dspUnload")
            .d("message", "Unloading DSP topology"));
    snd_ctl_elem_value_set_integer(control, 0, DSP_UNLOAD);
    if((ret = snd_ctl_elem_write(m_ctl, control)) != 0) {
        ACSDK_ERROR(LX("writeFailed")
                .d("reason", "dspUnloadFailed")
                .d("error_code", snd_strerror(ret)));
        return false;
    }

    // Load the DSP's topology
    ACSDK_DEBUG(LX("dspLoad")
            .d("message", "Loading DSP topology"));
    snd_ctl_elem_value_set_integer(control, 0, DSP_LOAD);
    if((ret = snd_ctl_elem_write(m_ctl, control)) != 0) {
        ACSDK_ERROR(LX("writeFailed")
                .d("reason", "dspLoadFailed")
                .d("error_code", snd_strerror(ret)));
        return false;
    }

    return true;
}

} // kwd
} // alexaClientSDK
