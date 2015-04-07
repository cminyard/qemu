/*
 * Add and get ACPI tables registered by devices.
 *
 * Copyright (C) 2015  Corey Minyard <cminyard@mvista.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef QEMU_HW_ACPI_HOOKS_H
#define QEMU_HW_ACPI_HOOKS_H

#include <hw/acpi/aml-build.h>

void add_acpi_dev_table(void *data, int size, const char *sig, uint8_t rev);

struct acpi_dev_table;

struct acpi_dev_table *acpi_dev_table_first(void);
struct acpi_dev_table *acpi_dev_table_next(struct acpi_dev_table *current);
uint8_t *acpi_dev_table_data(struct acpi_dev_table *);
unsigned acpi_dev_table_len(struct acpi_dev_table *);
const char *acpi_dev_table_sig(struct acpi_dev_table *);
uint8_t acpi_dev_table_rev(struct acpi_dev_table *);

#endif /* QEMU_HW_ACPI_HOOKS_H */
