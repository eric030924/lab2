
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <systemd/sd-bus.h>

#define SERVICE "org.example.Lab2Notify"
#define OBJECT "/org/example/Lab2Notify"
#define INTERFACE "org.example.Lab2Notify"

static int handle_notify(sd_bus_message *m,
                         void *userdata,
                         sd_bus_error *error) {
    (void)userdata;
    (void)error;

    const char *message;
    int r = sd_bus_message_read(m, "s", &message);

    if (r < 0) {
        return r;
    }

    printf("[Server] Notification received: %s\n", message);
    fflush(stdout);

    return sd_bus_reply_method_return(
        m, "s", "Notification received successfully"
    );
}

static const sd_bus_vtable notify_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD(
        "Notify",
        "s",
        "s",
        handle_notify,
        SD_BUS_VTABLE_UNPRIVILEGED
    ),
    SD_BUS_VTABLE_END
};

int main(void) {
    sd_bus *bus = NULL;
    sd_bus_slot *slot = NULL;
    int r;

    r = sd_bus_default_user(&bus);

    if (r < 0) {
        fprintf(stderr, "Bus connection failed: %s\n",
                strerror(-r));
        return 1;
    }

    r = sd_bus_add_object_vtable(
        bus, &slot, OBJECT, INTERFACE,
        notify_vtable, NULL
    );

    if (r < 0) {
        fprintf(stderr, "Object registration failed: %s\n",
                strerror(-r));
        goto finish;
    }

    r = sd_bus_request_name(bus, SERVICE, 0);

    if (r < 0) {
        fprintf(stderr, "Service registration failed: %s\n",
                strerror(-r));
        goto finish;
    }

    printf("[Server] D-Bus service started.\n");
    printf("[Server] Waiting for notifications...\n");
    fflush(stdout);

    while (1) {
        r = sd_bus_process(bus, NULL);

        if (r < 0) {
            fprintf(stderr, "Process error: %s\n",
                    strerror(-r));
            break;
        }

        if (r == 0) {
            r = sd_bus_wait(bus, UINT64_MAX);

            if (r < 0) {
                fprintf(stderr, "Wait error: %s\n",
                        strerror(-r));
                break;
            }
        }
    }

finish:
    sd_bus_slot_unref(slot);
    sd_bus_unref(bus);

    return r < 0 ? 1 : 0;
}

