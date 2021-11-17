#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <stdio.h>
#include <stdlib.h>
#include <minix/ds.h>
#include <minix/ioctl.h>
#include <sys/ioc_hello_queue.h>
#include "hello_queue.h"

static int hello_queue_open(devminor_t minor, int access, endpoint_t user_endpt);
static int hello_queue_close(devminor_t minor);
static ssize_t hello_queue_read(devminor_t minor, u64_t pos, endpoint_t ep,
	cp_grant_id_t gid, size_t size, int flags, cdev_id_t id);
static ssize_t hello_queue_write(devminor_t minor, u64_t pos, endpoint_t ep,
	cp_grant_id_t gid, size_t size, int flags, cdev_id_t id);
static int hello_queue_ioctl(devminor_t minor, unsigned long request, endpoint_t ep,
	cp_grant_id_t gid, int flags, endpoint_t user_ep, cdev_id_t id);

static void sef_local_startup(void);
static int sef_cb_init(int type, sef_init_info_t *info);
static int sef_cb_lu_state_save(int);
static int lu_state_restore(void);

static struct chardriver hello_queue_tab =
{
	.cdr_open	= hello_queue_open,
	.cdr_close	= hello_queue_close,
	.cdr_read	= hello_queue_read,
	.cdr_write	= hello_queue_write,
	.cdr_ioctl	= hello_queue_ioctl
};

static char *buffer;
static size_t buffer_size = DEVICE_SIZE, queue_size = 0;

static int hello_queue_open(devminor_t UNUSED(minor), int UNUSED(access), 
            endpoint_t UNUSED(user_endpt)) {
    return OK;
}

static int hello_queue_close(devminor_t UNUSED(minor)) {
    return OK;
}

static ssize_t hello_queue_read(devminor_t UNUSED(minor), u64_t UNUSED(pos), endpoint_t ep,
	cp_grant_id_t gid, size_t size, int UNUSED(flags), cdev_id_t UNUSED(id)) {

    int ret;
    size_t correct_size = size;
    

    if (queue_size == 0) return 0;
    if (size > queue_size) correct_size = queue_size;

    /* Copy the requested part to the caller. */
    if ((ret = sys_safecopyto(ep, gid, 0, (vir_bytes) buffer, correct_size)) != OK)
        return ret;

    int new_queue_size = queue_size - correct_size;

    for (int i = correct_size; i < queue_size; i++) {
        buffer[i - correct_size] = buffer[i];
    }

    /* The queue is too big. */
    if (new_queue_size < buffer_size / 4 && buffer_size > 1) {
        buffer = realloc(buffer, buffer_size / 2 * sizeof(char));
        if (buffer == NULL) {
            exit(1);
        }
        else {
            buffer_size /= 2;
        }
    }

    queue_size = new_queue_size;

    /* Return the number of bytes read. */
    return correct_size;
    
}

static ssize_t hello_queue_write(devminor_t UNUSED(minor), u64_t UNUSED(pos), endpoint_t ep,
	cp_grant_id_t gid, size_t size, int UNUSED(flags), cdev_id_t UNUSED(id)) {

    int ret;
    char *new_buffer;


    /* Not enough space in the buffer. */
    while (queue_size + size > buffer_size) {
        new_buffer = realloc(buffer, 2 * buffer_size * sizeof(char));
        if (new_buffer != NULL) {
            buffer = new_buffer;
            buffer_size *= 2;
        }
        else exit(1);
    }

    if ((ret = sys_safecopyfrom(ep, gid, 0, (vir_bytes) buffer + queue_size, size)) != OK)
        return ret;

    queue_size += size;

    /* Return the number of bytes written. */
    return size;
}


static void init_queue() {
    buffer = malloc(DEVICE_SIZE * sizeof(char));
        buffer_size = DEVICE_SIZE;
        queue_size = DEVICE_SIZE;
        if (buffer == NULL) exit(1);
        for (int i = 0; i < buffer_size; i++) {
            if (i % 3 == 0) {
                buffer[i] = 'x';
            }
            if (i % 3 == 1) {
                buffer[i] = 'y';
            }
            if (i % 3 == 2) {
                buffer[i] = 'z';
            }
        }
}

static int hello_queue_ioctl(devminor_t UNUSED(minor), unsigned long request, endpoint_t ep,
	cp_grant_id_t gid, int UNUSED(flags), endpoint_t UNUSED(user_ep), cdev_id_t UNUSED(id)) {
    int rc;
    char buff[MSG_SIZE];
    char *new_b;

    switch(request) {
    case HQIOCRES: {
        free(buffer);
        init_queue();
        break;
    }
    case HQIOCSET: {
        rc = sys_safecopyfrom(ep, gid, 0, (vir_bytes) buff, MSG_SIZE);
        if (rc != OK) return rc;
        int j = 0;

        if (queue_size >= MSG_SIZE) {
            for (int i = 0; i < MSG_SIZE; i++) {
                buffer[i + (queue_size - MSG_SIZE)] = buff[i];
            }
        }
        else {
            if (queue_size < MSG_SIZE) queue_size = MSG_SIZE;
            
            while (buffer_size < MSG_SIZE) {
                new_b = realloc(buffer, buffer_size * 2);
                if (new_b != NULL) {
                    buffer = new_b;
                    buffer_size *= 2;
                }
                else {
                    exit(1);
                }
            }

            for (int i = 0; i < MSG_SIZE; i++) {
                buffer[i] = buff[i];
            }
        }
        break;
    }
    case HQIOCXCH: {
        rc = sys_safecopyfrom(ep, gid, 0, (vir_bytes) buff, 2);
        if (rc != OK) return rc;

        for (int i = 0; i < queue_size; i++) {
            if (buffer[i] == buff[0])
                buffer[i] = buff[1];
        }
        break;
    }

    case HQIOCDEL: {
        char *new_b = malloc(sizeof(char) * buffer_size);
        int new_q_size = 0;
        for (int i = 0; i < buffer_size; i++) {
            if (i % 3 != 2) {
                new_b[new_q_size] = buffer[i];
                new_q_size++;
            }
        }
        free(buffer);
        buffer = new_b;
        queue_size = new_q_size;
        break;
    }
    default: {
        rc = ENOTTY;
    }
    }

    return rc;
}

static int sef_cb_lu_state_save(int UNUSED(state)) {
    /* Save the state. */
    ds_publish_mem("hq_buffer", buffer, buffer_size, DSF_OVERWRITE);
    free(buffer);
    ds_publish_u32("hq_buffer_size", buffer_size, DSF_OVERWRITE);
    ds_publish_u32("hq_queue_size", queue_size, DSF_OVERWRITE);

    return OK;
}

static int lu_state_restore() {
    u32_t q_size, b_size;
    char *r_buff;

    ds_retrieve_u32("hq_buffer_size", &b_size);
    ds_delete_u32("hq_buffer_size");
    ds_retrieve_u32("hq_queue_size", &q_size);
    ds_delete_u32("hq_queue_size");
    buffer_size = b_size;
    queue_size = q_size;
    buffer = malloc(buffer_size);
    ds_retrieve_mem("hq_buffer", buffer, &buffer_size);
	ds_delete_mem("hq_buffer");

    return OK;
}

static int sef_cb_init(int type, sef_init_info_t *info) {
    /* Initialize the hello driver. */
    int do_announce_driver = TRUE;

    switch(type) {
        case SEF_INIT_FRESH:
            init_queue();
        break;

        case SEF_INIT_LU:
            /* Restore the state. */
            lu_state_restore();
            do_announce_driver = FALSE;
        break;

        case SEF_INIT_RESTART:
        break;
    }

    /* Announce we are up when necessary. */
    if (do_announce_driver) {
        chardriver_announce();
    }

    /* Initialization completed successfully. */
    return OK;
}

static void sef_local_startup()
{
    /* Register init callbacks. Use the same function for all event types. */
    sef_setcb_init_fresh(sef_cb_init);
    sef_setcb_init_lu(sef_cb_init);
    sef_setcb_init_restart(sef_cb_init);

    /* Register live update callbacks. */
    /* - Agree to update immediately when LU is requested in a valid state. */
    sef_setcb_lu_prepare(sef_cb_lu_prepare_always_ready);
    /* - Support live update starting from any standard state. */
    sef_setcb_lu_state_isvalid(sef_cb_lu_state_isvalid_standard);
    /* - Register a custom routine to save the state. */
    sef_setcb_lu_state_save(sef_cb_lu_state_save);

    /* Let SEF perform startup. */
    sef_startup();
}

int main() {
    
    sef_local_startup();

    chardriver_task(&hello_queue_tab);

    free(buffer);

    return OK;
}