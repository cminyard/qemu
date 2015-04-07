/*
 * Hooks for dynamically construct ACPI tables in devices
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

void add_device_ssdt_encoder(void (*encode)(Aml *ssdt, void *opaque),
                             void *opaque);
void call_device_ssdt_encoders(Aml *ssdt);

#endif /* QEMU_HW_ACPI_HOOKS_H */
