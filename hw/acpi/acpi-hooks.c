/*
 * ACPI hooks for inserting table entries from devices into the SSDT table.
 *
 * Copyright (c) 2015 Corey Minyard, MontaVista Software, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <hw/acpi/acpi-hooks.h>
#include <qemu/queue.h>

struct ssdt_device_encoder {
    void (*encode)(Aml *ssdt, void *opaque);
    void *opaque;
    QSLIST_ENTRY(ssdt_device_encoder) next;
};

static QSLIST_HEAD(ssdt_device_encoders, ssdt_device_encoder)
     ssdt_device_encoders = QSLIST_HEAD_INITIALIZER(&ssdt_device_encoders);

void
add_device_ssdt_encoder(void (*encode)(Aml *ssdt, void *opaque), void *opaque)
{
    struct ssdt_device_encoder *e = g_new0(struct ssdt_device_encoder, 1);

    e->encode = encode;
    e->opaque = opaque;
    QSLIST_INSERT_HEAD(&ssdt_device_encoders, e, next);
}

void
call_device_ssdt_encoders(Aml *ssdt)
{
    struct ssdt_device_encoder *e;

    QSLIST_FOREACH(e, &ssdt_device_encoders, next) {
        e->encode(ssdt, e->opaque);
    }
}
