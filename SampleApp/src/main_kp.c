#include <alsa/asoundlib.h>

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

void s1000_dump_bytes(const void *b, int size)
{
	int i = 0;
	const unsigned int *bytes = b;

	debug_print(stdout, "\n%.4x: ", i);
	debug_print(stdout, "0x%.8x ", bytes[i]);
	debug_print(stdout, "0x%.8x ", bytes[i+1]);

	for (i = 2; i < size/sizeof(unsigned int); i++) {
		if ((i+2) % 4 == 0)
			debug_print(stdout, "\n%.4x: ", i);
		debug_print(stdout, "0x%.8x ", bytes[i]);
	}
	debug_print(stdout, "\n");
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

static void s1000_tlv_monitor(void)
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
	s1000_dump_bytes(read_tlv, read_size);
	goto poll;

	free(read_tlv);
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


int main(int argc, char *argv[])
{
	int err = 0;
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
	s1000_tlv_monitor();

	return 0;
}
