/*
 * laptop.h -- Various structures about supported hardware
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Written by Soós Péter <sp@osb.hu>, 2002-2004
 * Written by Mathieu Bérard <mathieu.berard@crans.org>, 2006
 */


#define HP_SIGNATURE	"Hewlett-Packard"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
static int __init dmi_matched(struct dmi_system_id *dmi);
#else
static int __init dmi_matched(const struct dmi_system_id *dmi);
#endif

static struct  dmi_system_id omnibook_ids[] __initdata = {
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook XE3 GF",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook XE3 GF"),
		},
		.driver_data = (void *) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook XT1000",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook XT1000"),
		},
		.driver_data = (void *) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook XE2 DC",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook XE2 DC"),
		},
		.driver_data = (void *) XE2
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook XE3 GC",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook XE3 GC"),
		},
		.driver_data = (void*) XE3GC
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook XE3 GD / Pavilion N5430",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook XE3 GD"),
		},
		.driver_data = (void*) XE3GC
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook XE3 GE / Pavilion N5415",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook XE3 GE"),
		},
		.driver_data = (void*) XE3GC
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook 500 FA",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook 500 FA"),
		},
		.driver_data = (void*) OB500
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook 510 FB",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook 510 FB"),
		},
		.driver_data = (void*) OB510
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook 4150",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook 4150"),
		},
		.driver_data = (void*) OB4150
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook 900 B",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook 900 B"),
		},
		.driver_data = (void*) OB4150
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook 6000 EA",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook 6000 EA"),
		},
		.driver_data = (void*) OB6000
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook 6100 EB",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook 6100 EB"),
		},
		.driver_data = (void*) OB6100
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook xe4000/xe4100",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook xe4000"),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook xe4400",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook xe4400"),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook xe4500",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook xe4500"),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook 6200 EG / vt6200 / xt 6200",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook 6200 EG"),
		},
		.driver_data = (void*) XE4500
	},
	/* There are no model specific strings of some HP OmniBook XT1500 */
	{
		.callback = dmi_matched,
		.ident = "HP OmniBook XT1500",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP OmniBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP OmniBook"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion ze4000 / ze4125",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP NoteBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP NoteBook ze4000"),
		},
		.driver_data = (void*) XE4500
	},
	/* There are no model specific strings of some HP Pavilion xt155 and some HP Pavilion ze4100 
	 * There are no model specific strings of some HP nx9000 */
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion xt155 / ze4100 / nx9000",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP NoteBook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP NoteBook PC"),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion ZU1000 FA / ZU1000 FA / ZU1175",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion Notebook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP Pavilion ZU1000 FA"),
		},
		.driver_data = (void*) OB500
	},
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion Notebook XE3 GC / N5290",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion Notebook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP Pavilion Notebook XE3 GC"),
		},
		.driver_data = (void*) XE3GC
	},
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion Notebook GD / N5441",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion Notebook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP Pavilion Notebook Model GD"),
		},
		.driver_data = (void*) XE3GC
	},
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion Notebook GE / XH545",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion Notebook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP Pavilion Notebook Model GE"),
		},
		.driver_data = (void*) XE3GC
	},
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion Notebook ZT1000 / ZT1141",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion Notebook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP Pavilion Notebook ZT1000"),
		},
		.driver_data = (void*) XE3GF
	},
	/* There are no model specific strings of some HP Pavilion ZT1175 and ZT1195 notebooks */
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion ZT1175 / ZT1195",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion Notebook PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "HP Pavilion Notebook"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion ze4200 series",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Pavilion ze4200"),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion ze4300 series",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Pavilion ze4300"),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion ze4500 series",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Pavilion ze4500"),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP Pavilion ze8500 series",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Pavilion ze8500"),
		},
		.driver_data = (void*) XE4500
	},
	/* Compaq nx9000 */
	{
		.callback = dmi_matched,
		.ident = "HP Compaq nx9000",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP nx9000"),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP Compaq nx9005",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP nx9005"),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP Compaq nx9010",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "HP nx9010"),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1000",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1000"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1005",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1005"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1110",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1110"),
		},
		.driver_data = (void*) XE3GF
	},	
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1115",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1115"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1130",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 1130"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1700-100",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1700-100"),
		},
		.driver_data = (void*) AMILOD
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1700-200",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1700-200"),
		},
		.driver_data = (void*) AMILOD
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1700-300",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1700-300"),
		},
		.driver_data = (void*) AMILOD
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1700-400",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1700-400"),
		},
		.driver_data = (void*) AMILOD
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1700-500",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1700-500"),
		},
		.driver_data = (void*) AMILOD
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1900",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1900"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1905",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1905"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1950",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1950"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1955",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S1955"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 2430",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S2430"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 2435",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S2435"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 3000",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S3000"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 3005",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "S3005"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1000",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 1000"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1005",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 1005"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1110",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 1110"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1115",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 1115"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1115",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Toshiba 1115"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1900",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 1900"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1905",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 1905"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1950",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 1950"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 1955",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 1955"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 2430",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 2430"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 2435",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 2435"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 3000",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 3000"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite 3005",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite 3005"),
		},
		.driver_data = (void*) XE3GF,
	},
	{
                .callback = dmi_matched,
                .ident = "Toshiba Satellite A70",
                .matches = {
                        DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
                        DMI_MATCH(DMI_PRODUCT_NAME, "Satellite A70"),
                },
                .driver_data = (void*) TSM70
        },
	{
                .callback = dmi_matched,
                .ident = "Toshiba Satellite A75",
                .matches = {
                        DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
                        DMI_MATCH(DMI_PRODUCT_NAME, "Satellite A75"),
                },
                .driver_data = (void*) TSM70
        },
	{
                .callback = dmi_matched,
                .ident = "Toshiba Satellite A80",
                .matches = {
                        DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
                        DMI_MATCH(DMI_PRODUCT_NAME, "Satellite A80"),
                },
                .driver_data = (void*) TSM70
        },
        {
                .callback = dmi_matched,
                .ident = "Toshiba Satellite A105",
                .matches = {
                        DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
                        DMI_MATCH(DMI_PRODUCT_NAME, "Satellite A105"),
                },
                .driver_data = (void*) TSA105
        },
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite A100",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite A100"),
		},
		.driver_data = (void*) TSA105
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite P100",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite P100"),
		},
		.driver_data = (void*) TSA105
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite P10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite P10"),
		},
		.driver_data = (void*) TSP10
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite P15",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite P15"),
		},
		.driver_data = (void*) TSP10
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite P20",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite P20"),
		},
		.driver_data = (void*) TSP10
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite P25",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite P25"),
		},
		.driver_data = (void*) TSM70
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite M30X",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite M30X"),
		},
		.driver_data = (void*) TSM30X
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite M35X",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite M35X"),
		},
		.driver_data = (void*) TSM70
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite M50",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite M50"),
		},
		.driver_data = (void*) TSM70
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite M60",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite M60"),
		},
		.driver_data = (void*) TSM70
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite M70",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite M70"),
		},
		.driver_data = (void*) TSM70
	},
        {
                .callback = dmi_matched,
                .ident = "Toshiba Satellite M100",
                .matches = {
                        DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
                        DMI_MATCH(DMI_PRODUCT_NAME, "SATELLITE M100"),
                },
                .driver_data = (void*) TSM70
        },
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite M100",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite M100"),
		},
		.driver_data = (void*) TSM70
	},
		{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite M115",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite M115"),
		},
		.driver_data = (void*) TSA105
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite M40X",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite M40X"),
		},
		.driver_data = (void*) TSM70
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite M40",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite M40"),
		},
		.driver_data = (void*) TSM40
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite M45",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite M45"),
		},
		.driver_data = (void*) TSM40
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Satellite X205-S9800",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite X205"),
		},
		.driver_data = (void*) TSX205
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Tecra S1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TECRA S1"),
		},
		.driver_data = (void*) TSM40
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Tecra S1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Tecra S1"),
		},
		.driver_data = (void*) TSM40
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Tecra S2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Tecra S2"),
		},
		.driver_data = (void*) TSM70
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Tecra A4",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Tecra A4"),
		},
		.driver_data = (void*) TSM40
	},
	{
		.callback = dmi_matched,
		.ident = "Toshiba Tecra A6",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TECRA A6"),
		},
		.driver_data = (void*) TSM70
	},
	{
                .callback = dmi_matched,
                .ident = "Toshiba Equium A110",
                .matches = {
                        DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
                        DMI_MATCH(DMI_PRODUCT_NAME, "EQUIUM A110"),
                },
                .driver_data = (void*) TSM30X /* FIXME: provisional */
        },
	{
		.callback = dmi_matched,
		.ident = "Compal ACL00",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "COMPAL"),
			DMI_MATCH(DMI_BOARD_NAME, "ACL00"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Compal ACL10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "COMPAL"),
			DMI_MATCH(DMI_BOARD_NAME, "ACL10"),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "Fujitsu-Siemens Amilo D series",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Amilo D-Series"),
		},
		.driver_data = (void*) AMILOD
	},
/* HP Technology code Matching:
 * Technology code appears in the first two chracters of BIOS version string
 * ended by a dot, but it prefixed a space character on some models and BIOS
 * versions.
 * New HP/Compaq models use more characters (eg. KF_KH.).
 */
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code CI",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "CI."),
		},
		.driver_data = (void*) OB4150
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code CL",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "CL."),
		},
		.driver_data = (void*) OB4150
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code DC",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "DC."),
		},
		.driver_data = (void*) XE2
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code EA",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "EA."),
		},
		.driver_data = (void*) OB6000
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code EB",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "EB."),
		},
		.driver_data = (void*) OB6100
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code EG",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "EG."),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code FA",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "FA."),
		},
		.driver_data = (void*) OB500
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code FB",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "FB."),
		},
		.driver_data = (void*) OB510
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code GC",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "GC."),
		},
		.driver_data = (void*) XE3GC
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code GD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "GD."),
		},
		.driver_data = (void*) XE3GC
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code GE",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "GE."),
		},
		.driver_data = (void*) XE3GC
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code GF",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "GF."),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code IB",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "IB."),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code IC",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "IC."),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code ID",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "ID."),
		},
		.driver_data = (void*) XE3GF
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code KA",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "KA."),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code KB",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "KB."),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code KC",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "KC."),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code KD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "KD."),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code KE",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "KE."),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code KE_KG",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "KE_KG."),
		},
		.driver_data = (void*) XE4500
	},
	{
		.callback = dmi_matched,
		.ident = "HP model with technology code KF_KH",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, HP_SIGNATURE),
			DMI_MATCH(DMI_BIOS_VERSION, "KF_KH."),
		},
		.driver_data = (void*) XE4500
	},
	{ NULL, }
};
