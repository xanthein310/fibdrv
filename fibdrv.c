#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 100

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static struct kobject *fib_time;
static DEFINE_MUTEX(fib_mutex);
long long time;

#define bigN_base 100000000
#define bigN_num 8

typedef struct bigN_t {
    long long part[bigN_num];
} bigN;

void bigN_copy(bigN *x, bigN *y)
{
    for (int i = 0; i < bigN_num; i++)
        x->part[i] = y->part[i];
}

static void bigN_add(bigN x, bigN y, bigN *result)
{
    memset(result, 0, sizeof(bigN));

    long long carry = 0;
    for (int i = 0; i < bigN_num; i++) {
        long long tmp = carry + x.part[i] + y.part[i];
        result->part[i] = tmp % bigN_base;
        carry = tmp / bigN_base;
    }
}

static void bigN_sub(bigN x, bigN y, bigN *result)
{
    memset(result, 0, sizeof(bigN));

    for (int i = 0; i < bigN_num; i++) {
        result->part[i] += x.part[i] - y.part[i];
        if (result->part[i] < 0) {
            result->part[i] += bigN_base;
            result->part[i + 1]--;
        }
    }
}

static void bigN_mul(bigN x, bigN y, bigN *result)
{
    memset(result, 0, sizeof(bigN));

    for (int i = 0; i < bigN_num; i++) {
        long long carry = 0;
        for (int j = 0; i + j < bigN_num; j++) {
            long long tmp = x.part[i] * y.part[j] + carry + result->part[i + j];
            result->part[i + j] = tmp % bigN_base;
            carry = tmp / bigN_base;
        }
    }
}

static bigN fib_sequence(long long k)
{
#if 0
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    bigN x, y, result;

    memset(&x, 0, sizeof(bigN));
    memset(&y, 0, sizeof(bigN));
    memset(&result, 0, sizeof(bigN));

    x.part[0] = 0;
    y.part[0] = 1;

    for (int i = 2; i <= k; i++) {
        bigN_add(x, y, &result);
        bigN_copy(&x, &y);
        bigN_copy(&y, &result);
    }

    return result;
#else
    bigN fn, fn1, f2n, f2n1;
    bigN two;
    bigN temp[2];
    int count = 0;
    long long temp_k = k;

    memset(&fn, 0, sizeof(bigN));
    memset(&fn1, 0, sizeof(bigN));
    memset(&f2n, 0, sizeof(bigN));
    memset(&f2n1, 0, sizeof(bigN));
    memset(&two, 0, sizeof(bigN));
    memset(temp, 0, 2 * sizeof(bigN));

    two.part[0] = 2;

    fn1.part[0] = 1;

    if (k == 0)
        return fn;

    while ((temp_k & 0x8000000000000000) == 0) {
        count++;
        temp_k = temp_k << 1;
    }

    k = k << count;
    for (count = 64 - count; count > 0; count--) {
        bigN_mul(fn, fn, &temp[0]);
        bigN_mul(fn1, fn1, &temp[1]);
        bigN_add(temp[0], temp[1], &f2n1);
        bigN_mul(fn1, two, &temp[0]);
        bigN_sub(temp[0], fn, &temp[1]);
        bigN_mul(fn, temp[1], &f2n);
        if (k & 0x8000000000000000) {
            bigN_copy(&fn, &f2n1);
            bigN_add(f2n, f2n1, &fn1);
        } else {
            bigN_copy(&fn, &f2n);
            bigN_copy(&fn1, &f2n1);
        }
        k = k << 1;
    }

    return fn;
#endif
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    bigN result;
    ssize_t bigNSize = sizeof(bigN);
    ktime_t ktime;

    if (size < bigNSize)
        return 0;

    ktime = ktime_get();
    result = fib_sequence(*offset);
    copy_to_user(buf, &result, bigNSize);
    ktime = ktime_sub(ktime_get(), ktime);
    time = (long long) ktime_to_ns(ktime);

    return bigNSize;
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static ssize_t time_show(struct kobject *kobj,
                         struct kobj_attribute *attr,
                         char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%lld", time);
}

static struct kobj_attribute time_attribute =
    __ATTR(time, 0644, time_show, NULL);

static struct attribute *attrs[] = {
    &time_attribute.attr,
    NULL,
};

static struct attribute_group attr_group = {
    .attrs = attrs,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    cdev_init(fib_cdev, &fib_fops);
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }

    fib_time = kobject_create_and_add("fib_time", kernel_kobj);
    if (!fib_time) {
        printk(KERN_ALERT "Failed to create kobj");
        rc = -5;
        goto failed_kobj_create;
    }
    if (sysfs_create_group(fib_time, &attr_group)) {
        printk(KERN_ALERT "Failed to create group");
        rc = -6;
        goto failed_group_create;
    }

    return rc;
failed_group_create:
    kobject_put(fib_time);
failed_kobj_create:
    device_destroy(fib_class, fib_dev);
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    kobject_put(fib_time);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
