#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "include/uapi/linux/pistorm.h"

#define DEFAULT_DEV "/dev/pistorm"
#define MAX_LINE 256

static int ps_fd = -1;

static int ps_setup(void)
{
	return ioctl(ps_fd, PISTORM_IOC_SETUP);
}

static int ps_open_dev(const char *path)
{
	if (ps_fd >= 0)
		return 0;

	ps_fd = open(path, O_RDWR | O_CLOEXEC);
	if (ps_fd < 0) {
		perror("open(/dev/pistorm)");
		return -1;
	}
	return 0;
}

static int ps_busop(int is_read, uint8_t width, uint32_t addr, uint32_t *val, uint16_t flags)
{
	struct pistorm_busop op = {
		.addr = addr,
		.value = val ? *val : 0,
		.width = width,
		.is_read = is_read,
		.flags = flags,
	};

	if (ioctl(ps_fd, PISTORM_IOC_BUSOP, &op) < 0) {
		perror("ioctl(BUSOP)");
		return -1;
	}
	if (is_read && val)
		*val = op.value;
	return 0;
}

static void cmd_help(FILE *out)
{
	fprintf(out, "Commands:\n");
	fprintf(out, "  setup                       Re-run IOC_SETUP (re-init bus GPIO/clock)\n");
	fprintf(out, "  r8|r16|r32 <addr> [count]   Read bytes/words/longs\n");
	fprintf(out, "  w8|w16|w32 <addr> <value>   Write\n");
	fprintf(out, "  status                      Read PiStorm status register\n");
	fprintf(out, "  pins                        Read GPIO levels (debug)\n");
	fprintf(out, "  reset_sm                    Reset state machine\n");
	fprintf(out, "  pulse_reset                 Pulse RESET\n");
	fprintf(out, "  help                        Show this help\n");
	fprintf(out, "  quit/exit                   Close session\n");
}

static int handle_line(char *line, FILE *io)
{
	char *cmd = strtok(line, " \t\r\n");
	if (!cmd)
		return 0;

	if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit"))
		return 1;

	if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
		cmd_help(io);
		return 0;
	}

	if (!strcmp(cmd, "reset_sm")) {
		if (ioctl(ps_fd, PISTORM_IOC_RESET_SM) < 0)
			perror("ioctl(RESET_SM)");
		/* Re-run setup to put bus/GPIO back into known-good state. */
		ps_setup();
		return 0;
	}

	if (!strcmp(cmd, "pulse_reset")) {
		if (ioctl(ps_fd, PISTORM_IOC_PULSE_RESET) < 0)
			perror("ioctl(PULSE_RESET)");
		ps_setup();
		return 0;
	}

	if (!strcmp(cmd, "pins")) {
		struct pistorm_pins pins = {0};
		if (ioctl(ps_fd, PISTORM_IOC_GET_PINS, &pins) < 0) {
			perror("ioctl(GET_PINS)");
			return 0;
		}
		fprintf(io, "GPLEV0=0x%08x GPLEV1=0x%08x\n", pins.gplev0, pins.gplev1);
		return 0;
	}

	if (!strcmp(cmd, "status")) {
		uint32_t v = 0;
		if (ps_busop(1, PISTORM_W16, 0, &v, PISTORM_BUSOP_F_STATUS) == 0)
			fprintf(io, "STATUS=0x%04x\n", v & 0xffff);
		return 0;
	}

	if (!strcmp(cmd, "setup")) {
		if (ps_setup() < 0)
			perror("ioctl(SETUP)");
		return 0;
	}

	int is_read = 0;
	uint8_t width = 0;
	if (!strcmp(cmd, "r8")) {
		is_read = 1;
		width = PISTORM_W8;
	} else if (!strcmp(cmd, "r16")) {
		is_read = 1;
		width = PISTORM_W16;
	} else if (!strcmp(cmd, "r32")) {
		is_read = 1;
		width = PISTORM_W32;
	} else if (!strcmp(cmd, "w8")) {
		is_read = 0;
		width = PISTORM_W8;
	} else if (!strcmp(cmd, "w16")) {
		is_read = 0;
		width = PISTORM_W16;
	} else if (!strcmp(cmd, "w32")) {
		is_read = 0;
		width = PISTORM_W32;
	} else {
		fprintf(io, "Unknown command: %s (type 'help')\n", cmd);
		return 0;
	}

	char *addr_s = strtok(NULL, " \t\r\n");
	if (!addr_s) {
		fprintf(io, "Address required\n");
		return 0;
	}
	char *end = NULL;
	uint32_t addr = strtoul(addr_s, &end, 0);
	if (end == addr_s) {
		fprintf(io, "Bad address\n");
		return 0;
	}

	if (is_read) {
		char *count_s = strtok(NULL, " \t\r\n");
		unsigned count = count_s ? strtoul(count_s, NULL, 0) : 1;
		if (!count)
			count = 1;
		for (unsigned i = 0; i < count; i++) {
			uint32_t v = 0;
			if (ps_busop(1, width, addr + i * width, &v, 0) < 0)
				break;
			fprintf(io, "0x%08x: 0x%0*x\n", addr + i * width,
			        width * 2, v & ((width == 1) ? 0xff : (width == 2 ? 0xffff : 0xffffffff)));
		}
	} else {
		char *val_s = strtok(NULL, " \t\r\n");
		if (!val_s) {
			fprintf(io, "Value required\n");
			return 0;
		}
		uint32_t v = strtoul(val_s, NULL, 0);
		ps_busop(0, width, addr, &v, 0);
	}
	return 0;
}

static void repl(FILE *io)
{
	char line[MAX_LINE];

	fprintf(io, "PiStorm monitor ready. Type 'help' for commands.\n");
	fprintf(io, "> ");
	fflush(io);
	while (fgets(line, sizeof(line), io)) {
		if (handle_line(line, io))
			break;
		fprintf(io, "> ");
		fflush(io);
	}
}

static int run_server(int port)
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket");
		return -1;
	}

	int opt = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(sock);
		return -1;
	}

	if (listen(sock, 1) < 0) {
		perror("listen");
		close(sock);
		return -1;
	}

	printf("Listening on 127.0.0.1:%d ...\n", port);
	for (;;) {
		int client = accept(sock, NULL, NULL);
		if (client < 0) {
			if (errno == EINTR)
				continue;
			perror("accept");
			break;
		}
		FILE *fp = fdopen(client, "r+");
		if (!fp) {
			perror("fdopen");
			close(client);
			continue;
		}
		fprintf(fp, "Connected. Type 'help' for commands.\n> ");
		fflush(fp);
		repl(fp);
		fclose(fp);
	}

	close(sock);
	return 0;
}

int main(int argc, char **argv)
{
	const char *dev = DEFAULT_DEV;
	int listen_port = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--dev") && i + 1 < argc) {
			dev = argv[++i];
		} else if (!strcmp(argv[i], "--listen") && i + 1 < argc) {
			listen_port = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			printf("Usage: %s [--dev /dev/pistorm] [--listen port]\n", argv[0]);
			return 0;
		}
	}

	if (ps_open_dev(dev) < 0)
		return 1;

	if (ps_setup() < 0) {
		perror("ioctl(SETUP)");
		return 1;
	}

	if (listen_port > 0)
		return run_server(listen_port);

	printf("Connected to %s\n> ", dev);
	fflush(stdout);
	repl(stdin);
	return 0;
}
