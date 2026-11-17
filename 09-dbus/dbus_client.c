
#include <stdio.h>
#include <string.h>
#include <systemd/sd-bus.h>

#define SERVICE "org.example.Lab2Notify"
#define OBJECT "/org/example/Lab2Notify"
#define INTERFACE "org.example.Lab2Notify"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s \"message\"\n", argv[0]);
        return 1;
    }

    sd_bus *bus = NULL;
    sd_bus_message *reply = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    const char *response;
    int r;

    r = sd_bus_default_user(&bus);

    if (r < 0) {
        fprintf(stderr, "Bus connection failed: %s\n",
                strerror(-r));
        goto finish;
    }

    r = sd_bus_call_method(
        bus,
        SERVICE,
        OBJECT,
        INTERFACE,
        "Notify",
        &error,
        &reply,
        "s",
        argv[1]
    );

    if (r < 0) {
        fprintf(stderr, "Method call failed: %s\n",
                error.message ? error.message : strerror(-r));
        goto finish;
    }

    r = sd_bus_message_read(reply, "s", &response);

    if (r < 0) {
        fprintf(stderr, "Reply error: %s\n",
                strerror(-r));
        goto finish;
    }

    printf("[Client] Sent: %s\n", argv[1]);
    printf("[Client] Server reply: %s\n", response);

finish:
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);

    return r < 0 ? 1 : 0;
}

