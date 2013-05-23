/*
 * QEMU IPMI ACPI handling
 *
 * Copyright (c) 2015 Corey Minyard <cminyard@mvista.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef HW_ACPI_IPMI_H
#define HW_ACPI_IPMI_H

#include "hw/acpi/aml-build.h"

void acpi_add_ipmi(Aml *ssdt);

#endif /* HW_ACPI_IPMI_H */
