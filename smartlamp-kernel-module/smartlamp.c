#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/usb.h>

MODULE_AUTHOR("DevTITANS <devtitans@icomp.ufam.edu.br>");
MODULE_DESCRIPTION("Driver de acesso ao SmartLamp (ESP32 com CP2102)");
MODULE_LICENSE("GPL");

#define MAX_RECV_LINE  100
#define MAX_CMD_SIZE   64
#define USB_TIMEOUT_MS 2000

static char recv_line[MAX_RECV_LINE];
static size_t recv_size;
static struct usb_device *smartlamp_device;
static unsigned int usb_in;
static unsigned int usb_out;
static char *usb_in_buffer;
static char *usb_out_buffer;
static int usb_max_size;
static bool smartlamp_connected;
static DEFINE_MUTEX(smartlamp_lock);

/*
 * O dispositivo ainda nao teve VID:PID identificado. A tabela vazia permite
 * registrar o ID real em /sys/bus/usb/drivers/smartlamp/new_id, sem usar IDs
 * ficticios no codigo-fonte.
 */
static const struct usb_device_id id_table[] = { { } };
MODULE_DEVICE_TABLE(usb, id_table);

static int usb_probe(struct usb_interface *interface,
                     const struct usb_device_id *id);
static void usb_disconnect(struct usb_interface *interface);
static int usb_send_cmd(const char *cmd, int param);

static ssize_t attr_show(struct kobject *sys_obj,
                         struct kobj_attribute *attr, char *buf);
static ssize_t attr_store(struct kobject *sys_obj,
                          struct kobj_attribute *attr,
                          const char *buf, size_t count);

static struct kobj_attribute led_attribute = __ATTR(led, 0644,
                                                     attr_show, attr_store);
static struct kobj_attribute ldr_attribute = __ATTR(ldr, 0444,
                                                     attr_show, NULL);
static struct kobj_attribute threshold_attribute = __ATTR(threshold, 0644,
                                                           attr_show, attr_store);
static struct attribute *attrs[] = {
    &led_attribute.attr,
    &ldr_attribute.attr,
    &threshold_attribute.attr,
    NULL,
};
static const struct attribute_group attr_group = { .attrs = attrs };
static struct kobject *sys_obj;

static int smartlamp_config_serial(struct usb_device *dev)
{
    int ret;
    u32 baudrate = 9600;

    ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x00, 0x41,
                          0x0001, 0, NULL, 0, USB_TIMEOUT_MS);
    if (ret < 0)
        return ret;

    return usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x1e, 0x41,
                           0, 0, &baudrate, sizeof(baudrate), USB_TIMEOUT_MS);
}

static struct usb_driver smartlamp_driver = {
    .name = "smartlamp",
    .probe = usb_probe,
    .disconnect = usb_disconnect,
    .id_table = id_table,
};
module_usb_driver(smartlamp_driver);

static int usb_probe(struct usb_interface *interface,
                     const struct usb_device_id *id)
{
    struct usb_endpoint_descriptor *endpoint_in;
    struct usb_endpoint_descriptor *endpoint_out;
    int ret;

    mutex_lock(&smartlamp_lock);
    if (smartlamp_connected) {
        ret = -EBUSY;
        goto out_unlock;
    }

    ret = usb_find_common_endpoints(interface->cur_altsetting, &endpoint_in,
                                    &endpoint_out, NULL, NULL);
    if (ret) {
        ret = -ENODEV;
        goto out_unlock;
    }

    usb_max_size = usb_endpoint_maxp(endpoint_in);
    if (!usb_max_size) {
        ret = -ENODEV;
        goto out_unlock;
    }

    usb_in_buffer = kmalloc(usb_max_size, GFP_KERNEL);
    usb_out_buffer = kmalloc(max_t(int, usb_max_size, MAX_CMD_SIZE), GFP_KERNEL);
    if (!usb_in_buffer || !usb_out_buffer) {
        ret = -ENOMEM;
        goto out_free_buffers;
    }

    smartlamp_device = usb_get_dev(interface_to_usbdev(interface));
    usb_in = endpoint_in->bEndpointAddress;
    usb_out = endpoint_out->bEndpointAddress;
    ret = smartlamp_config_serial(smartlamp_device);
    if (ret < 0)
        goto out_put_device;

    sys_obj = kobject_create_and_add("smartlamp", kernel_kobj);
    if (!sys_obj) {
        ret = -ENOMEM;
        goto out_put_device;
    }
    ret = sysfs_create_group(sys_obj, &attr_group);
    if (ret)
        goto out_put_kobject;

    usb_set_intfdata(interface, smartlamp_device);
    smartlamp_connected = true;
    mutex_unlock(&smartlamp_lock);
    return 0;

out_put_kobject:
    kobject_put(sys_obj);
    sys_obj = NULL;
out_put_device:
    usb_put_dev(smartlamp_device);
    smartlamp_device = NULL;
out_free_buffers:
    kfree(usb_out_buffer);
    kfree(usb_in_buffer);
    usb_out_buffer = NULL;
    usb_in_buffer = NULL;
out_unlock:
    mutex_unlock(&smartlamp_lock);
    return ret;
}

static void usb_disconnect(struct usb_interface *interface)
{
    mutex_lock(&smartlamp_lock);
    if (usb_get_intfdata(interface) != smartlamp_device) {
        mutex_unlock(&smartlamp_lock);
        return;
    }

    usb_set_intfdata(interface, NULL);
    smartlamp_connected = false;
    if (sys_obj) {
        sysfs_remove_group(sys_obj, &attr_group);
        kobject_put(sys_obj);
        sys_obj = NULL;
    }
    usb_put_dev(smartlamp_device);
    smartlamp_device = NULL;
    kfree(usb_in_buffer);
    kfree(usb_out_buffer);
    usb_in_buffer = NULL;
    usb_out_buffer = NULL;
    recv_size = 0;
    mutex_unlock(&smartlamp_lock);
}

/* Envia cmd e retorna o valor de "RES <cmd> <valor>". Chamador segura mutex. */
static int usb_send_cmd(const char *cmd, int param)
{
    char response_cmd[32];
    int actual_size;
    int command_size;
    int ret;
    int value;
    int i;

    if (!smartlamp_connected)
        return -ENODEV;

    if (param < 0)
        command_size = scnprintf(usb_out_buffer, MAX_CMD_SIZE, "%s\n", cmd);
    else
        command_size = scnprintf(usb_out_buffer, MAX_CMD_SIZE, "%s %d\n",
                                 cmd, param);
    if (command_size == MAX_CMD_SIZE - 1)
        return -ENOSPC;

    ret = usb_bulk_msg(smartlamp_device,
                       usb_sndbulkpipe(smartlamp_device, usb_out),
                       usb_out_buffer, command_size, &actual_size,
                       USB_TIMEOUT_MS);
    if (ret)
        return ret;
    if (actual_size != command_size)
        return -EIO;

    for (;;) {
        ret = usb_bulk_msg(smartlamp_device,
                           usb_rcvbulkpipe(smartlamp_device, usb_in),
                           usb_in_buffer, usb_max_size, &actual_size,
                           USB_TIMEOUT_MS);
        if (ret)
            return ret;

        for (i = 0; i < actual_size; i++) {
            if (usb_in_buffer[i] == '\r')
                continue;
            if (usb_in_buffer[i] == '\n') {
                recv_line[recv_size] = '\0';
                if (sscanf(recv_line, "RES %31s %d", response_cmd,
                           &value) == 2 && !strcmp(response_cmd, cmd)) {
                    recv_size = 0;
                    return value;
                }
                recv_size = 0;
                continue;
            }
            if (recv_size >= MAX_RECV_LINE - 1) {
                recv_size = 0;
                continue;
            }
            recv_line[recv_size++] = usb_in_buffer[i];
        }
    }
}

static ssize_t attr_show(struct kobject *sys_obj,
                         struct kobj_attribute *attr, char *buf)
{
    const char *command;
    int value;

    if (!strcmp(attr->attr.name, "led"))
        command = "GET_LED";
    else if (!strcmp(attr->attr.name, "ldr"))
        command = "GET_LDR";
    else if (!strcmp(attr->attr.name, "threshold"))
        command = "GET_THRESHOLD";
    else
        return -EINVAL;

    mutex_lock(&smartlamp_lock);
    value = usb_send_cmd(command, -1);
    mutex_unlock(&smartlamp_lock);
    if (value < 0)
        return value;

    return sysfs_emit(buf, "%d\n", value);
}

static ssize_t attr_store(struct kobject *sys_obj,
                          struct kobj_attribute *attr,
                          const char *buf, size_t count)
{
    const char *command;
    int value;
    int ret;

    if (!strcmp(attr->attr.name, "led"))
        command = "SET_LED";
    else if (!strcmp(attr->attr.name, "threshold"))
        command = "SET_THRESHOLD";
    else
        return -EACCES;

    ret = kstrtoint(buf, 10, &value);
    if (ret)
        return ret;

    mutex_lock(&smartlamp_lock);
    ret = usb_send_cmd(command, value);
    mutex_unlock(&smartlamp_lock);
    if (ret < 0)
        return ret;

    return count;
}
