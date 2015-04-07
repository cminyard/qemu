/*
 * Add and get ACPI tables registered by devices.
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

#include <string.h>
#include <hw/acpi/acpi-dev-tables.h>
#include <qemu/queue.h>

struct acpi_dev_table {
    void *data;
    int size;
    const char *sig;
    uint8_t rev;
    QSLIST_ENTRY(acpi_dev_table) next;
};

static QSLIST_HEAD(, acpi_dev_table) acpi_table_entries;

void
add_acpi_dev_table(void *data, int size, const char *sig, uint8_t rev)
{
    struct acpi_dev_table *e = g_malloc(sizeof(*e));

    e->data = g_malloc(size);
    memcpy(e->data, data, size);
    e->size = size;
    e->sig = sig;
    e->rev = rev;
    QSLIST_INSERT_HEAD(&acpi_table_entries, e, next);
}

struct acpi_dev_table *acpi_dev_table_first(void)
{
    return QSLIST_FIRST(&acpi_table_entries);
}

struct acpi_dev_table *acpi_dev_table_next(struct acpi_dev_table *current)
{
    return QSLIST_NEXT(current, next);
}

uint8_t *acpi_dev_table_data(struct acpi_dev_table *e)
{
    return e->data;
}

unsigned acpi_dev_table_len(struct acpi_dev_table *e)
{
    return e->size;
}

const char *acpi_dev_table_sig(struct acpi_dev_table *e)
{
    return e->sig;
}

uint8_t acpi_dev_table_rev(struct acpi_dev_table *e)
{
    return e->rev;
}
